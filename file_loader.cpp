// file_loader.cpp — THE file mechanism implementation
//
// The raw disk open, the byte pull, the staged rename, the docking
// lifecycle, and the structured-text scanning all live HERE — one file
// doing the whole job it is named for. Nothing about what the bytes mean
// lives here; meaning is the executing content's alone.

#include "file_loader.h"

#include "id_generation.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace prime {

namespace fs = std::filesystem;

namespace {

// Three attempts is the sign it will not load — and simultaneously
// unnoticeable in load times. Absent is never retried: absent is a state,
// not a fault.
constexpr int kAttempts = 3;

// ---------------------------------------------------------------------------
// Disk internals — the Loader's own hands. Not shared, not exported: every
// read and every write in the system comes through here, inside a docking,
// and nowhere else.
// ---------------------------------------------------------------------------

FileRead read_whole_file(const std::string& path, std::string& out) {
    out.clear();
    if (path.empty()) return FileRead::Absent;

    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return FileRead::Absent;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return FileRead::Unreadable;

    std::ostringstream buf;
    buf << f.rdbuf();
    if (f.bad()) return FileRead::Unreadable;

    out = buf.str();
    return FileRead::Ok;
}

bool write_whole_file(const std::string& path, const std::string& text) {
    if (path.empty()) return false;

    fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) return false;
    }

    // Staged write: a crash part-way through leaves the temporary behind
    // and the real file untouched, rather than a truncated file that would
    // load as if it were complete.
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f << text;
        if (!f.good()) return false;
    }

    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) return false;
    }
    return true;
}

FileRead read_with_attempts(const std::string& path, std::string& text_out) {
    FileRead read = FileRead::Unreadable;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        read = read_whole_file(path, text_out);
        if (read != FileRead::Unreadable) break;
    }
    return read;
}

// ---------------------------------------------------------------------------
// The docking. One of these exists per top-level Loader call and nowhere
// else — it IS the call's identity, scoped to the call's own stack frame.
//
// Coming up: take the docking source's canonical path (Loader HAS it; the
// mechanism behind that is out of this file's scope), mint a fresh
// identity — new and random every docking, never looked up, never reused —
// and log the (path, identity) row into the identity table.
//
// Going down: the destructor erases the row and the identity dies with the
// object — on return, on failure, on throw, all the same. The scope ending
// IS the undock; that is the one honest signal that nothing more claiming
// this identity is coming.
//
// The one refusal here predates this design and stands unchanged: no
// identity issued means no disk access of any kind.
// ---------------------------------------------------------------------------
class Docking {
public:
    Docking() {
        // Fresh and random, every docking. Never derived from anything a
        // request could learn, never stored anywhere it could be fetched
        // back from.
        id_ = IdGeneration::instance().mint_layer_id();
        if (id_.empty()) {
            // No identity, no disk access of any kind.
            // ERROR LOG HOOK — handed to the error log system once it exists.
            failure_ = "docking refused: no identity issued for this docking";
            return;
        }

        // The log. Loader keeps the table true and enforces nothing on the
        // back of it — verification is the OS filesystem's job, not built
        // yet.
        identity_table_dock(docked_layer_path(), id_);
        logged_ = true;
    }

    ~Docking() {
        if (logged_) {
            // The undock. A failure on the registry's side is handed to the
            // error log system once it exists; the docking is already over
            // either way, and the identity dies with this object regardless.
            identity_table_undock(id_);
        }
    }

    Docking(const Docking&)            = delete;
    Docking& operator=(const Docking&) = delete;

    bool               ok() const      { return logged_; }
    const std::string& failure() const { return failure_; }
    const LayerId&     id() const      { return id_; }

private:
    LayerId     id_;
    std::string failure_;
    bool        logged_ = false;
};

// ---------------------------------------------------------------------------
// Scanning internals — shared mechanics of the public toolkit below.
// ---------------------------------------------------------------------------

// Skip whitespace forward from `i`, bounded by `end`.
size_t skip_space(const std::string& text, size_t i, size_t end) {
    while (i < end && (text[i] == ' '  || text[i] == '\t' ||
                       text[i] == '\n' || text[i] == '\r'))
        ++i;
    return i;
}

// Read a quoted string starting at the opening quote at `i`. On success `i`
// is left just past the closing quote.
bool take_quoted(const std::string& text, size_t& i, size_t end,
                 std::string& out) {
    out.clear();
    if (i >= end || text[i] != '"') return false;
    ++i;
    while (i < end) {
        const char c = text[i];
        if (c == '\\' && i + 1 < end) {
            const char esc = text[i + 1];
            switch (esc) {
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += esc;  break;   // covers \" and \\
            }
            i += 2;
            continue;
        }
        if (c == '"') { ++i; return true; }
        out += c;
        ++i;
    }
    return false;   // ran off the end without closing
}

// Find the position of a field name within [begin, end), skipping over any
// nested block so a name inside a child record is not mistaken for this
// record's own field.
bool find_field(const std::string& text, size_t begin, size_t end,
                const std::string& name, size_t& value_pos) {
    const std::string quoted = "\"" + name + "\"";

    size_t i = begin;
    int depth = 0;
    while (i < end) {
        const char c = text[i];

        if (c == '"') {
            // A string at depth 0 relative to this record may be our field name.
            if (depth == 0 && text.compare(i, quoted.size(), quoted) == 0) {
                size_t after = skip_space(text, i + quoted.size(), end);
                if (after < end && text[after] == ':') {
                    value_pos = skip_space(text, after + 1, end);
                    return true;
                }
            }
            // Step over the whole string either way, so a colon or brace inside
            // it cannot be read as structure.
            std::string discard;
            size_t j = i;
            if (!take_quoted(text, j, end, discard)) return false;
            i = j;
            continue;
        }

        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') --depth;
        ++i;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// The disk mechanics as handed to executing content — the same hands,
// reachable only from inside a docking.
// ---------------------------------------------------------------------------
FileRead Disk::read(const std::string& path, std::string& out) const {
    // The identity travels with the read for the OS-level permission check
    // that does not exist yet. Loader neither interprets it nor checks it.
    if (id_.empty()) {
        // ERROR LOG HOOK — handed to the error log system once it exists.
        return FileRead::Unreadable;
    }
    return read_with_attempts(path, out);
}

bool Disk::save(const std::string& path, const std::string& text) const {
    // The identity travels with the write for the OS-level permission check
    // that does not exist yet. Loader neither interprets it nor checks it.
    if (id_.empty()) {
        // ERROR LOG HOOK — handed to the error log system once it exists.
        return false;
    }
    return write_whole_file(path, text);
}

// ---------------------------------------------------------------------------
// Load — one docking. One request, entries independent, every entry's
// content executed exactly as written, in parallel. This function knows
// nothing about what any entry does: it stands up the docking, hands over
// the disk mechanics, runs the content, and states per entry whether it
// completed. The docking object's own scope ending — after every entry has
// finished, however this function exits — erases the identity row and
// discards the identity. That IS the undock.
// ---------------------------------------------------------------------------
LoaderReport FileLoader::load(const LoadRequest& request) {
    LoaderReport report;

    if (request.entries.empty()) {
        // ERROR LOG HOOK — handed to the error log system here once it
        // exists. A Layer is fully formed and plug-and-play; a call with
        // no entries is not ordinary and is never treated as ok.
        report.ok      = false;
        report.failure = "load refused: request carries no entries";
        return report;
    }

    // The Layer docked with content that has no identity in it. The docking
    // mints this call's identity and logs its row here, before any of the
    // content runs. The Layer never held it, never sent it, and never sees
    // it. It lives as long as this call and no longer.
    Docking docking;
    if (!docking.ok()) {
        report.ok      = false;
        report.failure = "load refused: " + docking.failure();
        return report;
    }

    report.entries.resize(request.entries.size());

    Disk disk(docking.id());

    // Entries run in parallel — the system-wide default. Each worker owns
    // exactly its own slot of the report; no lock needed here. Sequence,
    // where content genuinely needs it, lives inside a single entry's own
    // steps — never imposed between entries. The docking spans ALL of them:
    // one identity for the whole call, however many disk touches the
    // content directs, and it is not over until every entry is.
    std::vector<std::thread> workers;
    workers.reserve(request.entries.size());

    for (size_t i = 0; i < request.entries.size(); ++i) {
        workers.emplace_back([&request, &report, &disk, i]() {
            EntryReport&     er    = report.entries[i];
            const LoadEntry& entry = request.entries[i];

            if (!entry.run) {
                // ERROR LOG HOOK — handed to the error log system here
                // once it exists.
                er.ok      = false;
                er.failure = "load refused: entry carries no content";
                return;
            }

            try {
                const EntryOutcome out = entry.run(disk);
                er.ok      = out.ok;
                er.failure = out.failure;
                // ERROR LOG HOOK — a content-stated failure is handed to
                // the error log system here once it exists.
            } catch (const std::exception& e) {
                // Content that throws instead of stating its failure is
                // still a loud, per-entry failure — never silent, and
                // never the whole call torn down.
                // ERROR LOG HOOK — as above, once the system exists.
                er.ok      = false;
                er.failure = std::string("entry content threw: ") + e.what();
            } catch (...) {
                // ERROR LOG HOOK — as above, once the system exists.
                er.ok      = false;
                er.failure = "entry content threw a non-standard exception";
            }
        });
    }

    for (auto& w : workers) w.join();

    report.ok = true;
    for (const auto& er : report.entries) {
        if (er.ok) continue;
        report.ok = false;
        if (report.failure.empty()) report.failure = er.failure;
    }
    return report;
    // Docking destructs here: row erased, identity discarded. The undock.
}

// ---------------------------------------------------------------------------
// Save — one docking, the direct write door standing beside load. The
// content arrives already fully formed; nothing is executed. The same
// minting, the same row, the same staged write by Loader's own hands, and
// the same undock when this call's scope ends — however it ends.
// ---------------------------------------------------------------------------
SaveReport FileLoader::save(const std::string& path, const std::string& text) {
    SaveReport report;

    if (path.empty()) {
        // ERROR LOG HOOK — handed to the error log system once it exists.
        report.ok      = false;
        report.failure = "save refused: no destination path given";
        return report;
    }

    Docking docking;
    if (!docking.ok()) {
        report.ok      = false;
        report.failure = "save refused: " + docking.failure();
        return report;
    }

    Disk disk(docking.id());

    if (!disk.save(path, text)) {
        // ERROR LOG HOOK — handed to the error log system once it exists.
        report.ok      = false;
        report.failure = "save failed: could not write " + path;
        return report;
    }

    report.ok = true;
    return report;
    // Docking destructs here: row erased, identity discarded. The undock.
}

// ---------------------------------------------------------------------------
// Scanning the structured text — the public toolkit. ONE implementation,
// every configuration that parses this shape calls these.
// ---------------------------------------------------------------------------
bool find_block(const std::string& text, size_t from,
                char open_ch, char close_ch,
                size_t& begin_out, size_t& end_out) {
    size_t i = from;
    while (i < text.size() && text[i] != open_ch) {
        if (text[i] == '"') {
            std::string discard;
            size_t j = i;
            if (!take_quoted(text, j, text.size(), discard)) return false;
            i = j;
            continue;
        }
        ++i;
    }
    if (i >= text.size()) return false;

    begin_out = i + 1;

    int depth = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '"') {
            std::string discard;
            size_t j = i;
            if (!take_quoted(text, j, text.size(), discard)) return false;
            i = j;
            continue;
        }
        if (c == open_ch)  ++depth;
        if (c == close_ch) {
            --depth;
            if (depth == 0) { end_out = i; return true; }
        }
        ++i;
    }
    return false;
}

bool read_string_field(const std::string& text, size_t begin, size_t end,
                       const std::string& name, std::string& out) {
    out.clear();
    size_t pos = 0;
    if (!find_field(text, begin, end, name, pos)) return false;
    return take_quoted(text, pos, end, out);
}

bool read_number_field(const std::string& text, size_t begin, size_t end,
                       const std::string& name, double& out) {
    size_t pos = 0;
    if (!find_field(text, begin, end, name, pos)) return false;

    size_t stop = pos;
    while (stop < end && text[stop] != ',' && text[stop] != '}' &&
           text[stop] != ']' && text[stop] != '\n')
        ++stop;

    try {
        out = std::stod(text.substr(pos, stop - pos));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool read_bool_field(const std::string& text, size_t begin, size_t end,
                     const std::string& name, bool& out) {
    size_t pos = 0;
    if (!find_field(text, begin, end, name, pos)) return false;

    if (text.compare(pos, 4, "true")  == 0) { out = true;  return true; }
    if (text.compare(pos, 5, "false") == 0) { out = false; return true; }
    return false;
}

bool read_string_list(const std::string& text, size_t begin, size_t end,
                      const std::string& name, std::vector<std::string>& out) {
    out.clear();
    size_t pos = 0;
    if (!find_field(text, begin, end, name, pos)) return false;
    if (pos >= end || text[pos] != '[') return false;

    size_t list_begin = 0, list_end = 0;
    if (!find_block(text, pos, '[', ']', list_begin, list_end)) return false;

    size_t i = list_begin;
    while (i < list_end) {
        i = skip_space(text, i, list_end);
        if (i >= list_end) break;
        if (text[i] == ',') { ++i; continue; }
        if (text[i] != '"') break;

        std::string value;
        if (!take_quoted(text, i, list_end, value)) return false;
        out.push_back(std::move(value));
    }
    return true;
}

std::vector<std::string> read_object_keys(const std::string& text,
                                          size_t begin, size_t end) {
    // The file's own contents ARE the list. Nothing is maintained anywhere
    // saying which keys should be here — whatever is written is what exists.
    std::vector<std::string> out;

    size_t i = begin;
    while (i < end) {
        i = skip_space(text, i, end);
        if (i >= end) break;

        if (text[i] != '"') { ++i; continue; }

        std::string key;
        size_t j = i;
        if (!take_quoted(text, j, end, key)) break;

        size_t after = skip_space(text, j, end);
        if (after < end && text[after] == ':') {
            out.push_back(key);
            // Step over this key's whole value, so keys inside it are not
            // collected as if they were siblings.
            size_t value_start = skip_space(text, after + 1, end);
            if (value_start < end &&
                (text[value_start] == '{' || text[value_start] == '[')) {
                const char open_ch  = text[value_start];
                const char close_ch = (open_ch == '{') ? '}' : ']';
                size_t vb = 0, ve = 0;
                if (find_block(text, value_start, open_ch, close_ch, vb, ve)) {
                    i = ve + 1;
                    continue;
                }
            }
            i = value_start;
            continue;
        }
        i = j;
    }
    return out;
}

std::string escape_text(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

} // namespace prime
