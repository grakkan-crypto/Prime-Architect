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

constexpr int kAttempts = 3;

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

class Docking {
public:
    Docking() {

        id_ = IdGeneration::instance().mint_layer_id();
        if (id_.empty()) {

            failure_ = "docking refused: no identity issued for this docking";
            return;
        }

        identity_table_dock(docked_layer_path(), id_);
        logged_ = true;
    }

    ~Docking() {
        if (logged_) {

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

size_t skip_space(const std::string& text, size_t i, size_t end) {
    while (i < end && (text[i] == ' '  || text[i] == '\t' ||
                       text[i] == '\n' || text[i] == '\r'))
        ++i;
    return i;
}

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
                default:   out += esc;  break;
            }
            i += 2;
            continue;
        }
        if (c == '"') { ++i; return true; }
        out += c;
        ++i;
    }
    return false;
}

bool find_field(const std::string& text, size_t begin, size_t end,
                const std::string& name, size_t& value_pos) {
    const std::string quoted = "\"" + name + "\"";

    size_t i = begin;
    int depth = 0;
    while (i < end) {
        const char c = text[i];

        if (c == '"') {

            if (depth == 0 && text.compare(i, quoted.size(), quoted) == 0) {
                size_t after = skip_space(text, i + quoted.size(), end);
                if (after < end && text[after] == ':') {
                    value_pos = skip_space(text, after + 1, end);
                    return true;
                }
            }

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

}

FileRead Disk::read(const std::string& path, std::string& out) const {

    if (id_.empty()) {

        return FileRead::Unreadable;
    }
    return read_with_attempts(path, out);
}

bool Disk::save(const std::string& path, const std::string& text) const {

    if (id_.empty()) {

        return false;
    }
    return write_whole_file(path, text);
}

LoaderReport FileLoader::load(const LoadRequest& request) {
    LoaderReport report;

    if (request.entries.empty()) {

        report.ok      = false;
        report.failure = "load refused: request carries no entries";
        return report;
    }

    Docking docking;
    if (!docking.ok()) {
        report.ok      = false;
        report.failure = "load refused: " + docking.failure();
        return report;
    }

    report.entries.resize(request.entries.size());

    Disk disk(docking.id());

    std::vector<std::thread> workers;
    workers.reserve(request.entries.size());

    for (size_t i = 0; i < request.entries.size(); ++i) {
        workers.emplace_back([&request, &report, &disk, i]() {
            EntryReport&     er    = report.entries[i];
            const LoadEntry& entry = request.entries[i];

            if (!entry.run) {

                er.ok      = false;
                er.failure = "load refused: entry carries no content";
                return;
            }

            try {
                const EntryOutcome out = entry.run(disk);
                er.ok      = out.ok;
                er.failure = out.failure;

            } catch (const std::exception& e) {

                er.ok      = false;
                er.failure = std::string("entry content threw: ") + e.what();
            } catch (...) {

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

}

SaveReport FileLoader::save(const std::string& path, const std::string& text) {
    SaveReport report;

    if (path.empty()) {

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

        report.ok      = false;
        report.failure = "save failed: could not write " + path;
        return report;
    }

    report.ok = true;
    return report;

}

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

}
