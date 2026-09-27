// project_ingest.cpp — the Project configuration of the file loader
//
// The chunking lives HERE. It is Project's own processing — the logic this
// configuration plugs into the Loader — so it sits in this file, not in a
// file of its own. Text in, block list out; it reads nothing, writes
// nothing, and knows nothing about pools, agents or disk.
//
// WHY THE CHUNKING IS NOT A PARSER, AND MUST NOT BECOME ONE
//   The project spans C++, C#, Kotlin, Python and JavaScript today and will
//   span more. A real parser per language is a permanent maintenance burden
//   with no payoff here, because nothing downstream needs correct semantic
//   function boundaries. The requirements are only:
//     - do not split something that is likely to be edited as one unit
//     - keep blocks small
//   Both are satisfied by counting structural depth, which needs no
//   knowledge of any language's keywords.
//
//   THE TWO FAMILIES
//     Braces      — C++, C#, Kotlin, Java, JavaScript, TypeScript, C. A unit
//                   is whatever opens a brace at top level plus everything
//                   until the matching close.
//     Indentation — Python. A unit is a line at column zero plus every line
//                   indented under it.
//
//   That is two counting rules, not two parsers. A new language is one line
//   saying which family its extension belongs to — never new logic.
//
// WHAT IT GETS WRONG, AND WHY THAT IS FINE
//   Braces inside string literals and comments are skipped, but exotic forms
//   — C++ raw strings, JS/Kotlin template literals interpolating braces,
//   Python triple-quoted text at column zero — can still fool the count and
//   produce an oddly-placed cut. Nothing downstream is load-bearing on the
//   cut being in the semantically correct place, so this is accepted rather
//   than defended against with machinery that would cost more than the
//   error does.
//
// COMMENTS TRAVEL WITH THEIR CODE
//   A run of top-level lines immediately above a unit, with no blank line
//   between, is folded into that unit. That is what keeps a doc comment, an
//   attribute or a decorator attached to the thing it describes — those are
//   edited together, so they are one block.
//
// NO SIZE CAP
//   A block is one structural unit however large it is. A cap would sever an
//   edit boundary to solve a problem that does not exist: function-level
//   blocks in this codebase land an order of magnitude below any context
//   concern.

#include "project_ingest.h"

#include "../foundation/file_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>

namespace fs = std::filesystem;

namespace prime {

namespace {

// The class every project pool belongs to — what the loader gives the pool.
// The pool does its own class lookup from this; nothing here resolves it.
constexpr const char* kProjectClass = "Project";

// Hidden entries are skipped: .git and its like are not the project.
bool is_hidden(const fs::path& p) {
    const std::string name = p.filename().string();
    return !name.empty() && name.front() == '.';
}

// ===========================================================================
// PROJECT'S PROCESSING — splitting one source file into function-sized blocks
// ===========================================================================

enum class ChunkStyle {
    Braces,      // depth counted with { and }
    Indentation, // depth counted with leading whitespace
};

// One block of a source file. Lines are 1-based and inclusive, so a block can
// be reported to a human without off-by-one translation.
struct CodeBlock {
    uint32_t    first_line = 0;
    uint32_t    last_line  = 0;
    std::string text;
};

// -------------------------------------------------------------------------
// Shared helpers
// -------------------------------------------------------------------------
std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') { cur += c; }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

bool is_blank(const std::string& s) {
    return std::all_of(s.begin(), s.end(),
                       [](unsigned char c) { return std::isspace(c) != 0; });
}

std::string join(const std::vector<std::string>& lines, size_t a, size_t b) {
    std::string out;
    for (size_t i = a; i <= b && i < lines.size(); ++i) {
        out += lines[i];
        if (i < b) out += '\n';
    }
    return out;
}

CodeBlock make_block(const std::vector<std::string>& lines, size_t a, size_t b) {
    CodeBlock blk;
    blk.first_line = static_cast<uint32_t>(a + 1); // 1-based for humans
    blk.last_line  = static_cast<uint32_t>(b + 1);
    blk.text       = join(lines, a, b);
    return blk;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// -------------------------------------------------------------------------
// Brace family
// -------------------------------------------------------------------------

// Count braces on one line, skipping anything inside a string, a character
// literal or a comment. in_block_comment carries across lines because /* */
// does. This is a scanner, not a parser: it knows quotes, escapes and comment
// markers, and nothing else about any language.
struct BraceCount {
    int opened = 0; // count of '{' seen — not net; needed to spot a one-line unit
    int net    = 0; // '{' minus '}'
};

BraceCount scan_braces(const std::string& line, bool& in_block_comment) {
    BraceCount bc;
    bool in_string = false;
    bool in_char   = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        const char n = (i + 1 < line.size()) ? line[i + 1] : '\0';

        if (in_block_comment) {
            if (c == '*' && n == '/') { in_block_comment = false; ++i; }
            continue;
        }
        if (in_string) {
            if (c == '\\') { ++i; continue; }   // escaped anything
            if (c == '"')  in_string = false;
            continue;
        }
        if (in_char) {
            if (c == '\\') { ++i; continue; }
            if (c == '\'') in_char = false;
            continue;
        }

        if (c == '/' && n == '/') break;                       // rest of line is comment
        if (c == '/' && n == '*') { in_block_comment = true; ++i; continue; }
        if (c == '"')  { in_string = true; continue; }
        if (c == '\'') { in_char   = true; continue; }

        if (c == '{') { ++bc.opened; ++bc.net; }
        else if (c == '}') { --bc.net; }
    }
    return bc;
}

std::vector<CodeBlock> chunk_braces(const std::vector<std::string>& lines) {
    std::vector<CodeBlock> out;

    int  depth            = 0;
    bool in_block_comment = false;

    // A run of top-level lines that opened nothing — includes, usings, globals,
    // a doc comment. Held rather than emitted immediately, because if the next
    // unit starts on the very next line this run belongs to it.
    long pending_start = -1;
    long pending_last  = -1;
    long unit_start    = -1;

    for (size_t i = 0; i < lines.size(); ++i) {
        const bool blank = is_blank(lines[i]);

        // A blank line at top level breaks adjacency: whatever was pending is
        // its own thing, not a preamble to whatever comes next.
        if (blank && unit_start < 0) {
            if (pending_start >= 0) {
                out.push_back(make_block(lines, static_cast<size_t>(pending_start),
                                                static_cast<size_t>(pending_last)));
                pending_start = pending_last = -1;
            }
            continue;
        }

        const BraceCount bc = scan_braces(lines[i], in_block_comment);

        if (unit_start < 0) {
            if (bc.opened > 0) {
                // This line opens a unit. Fold in an immediately-preceding run
                // so a doc comment or attribute stays with what it describes.
                long start = static_cast<long>(i);
                if (pending_start >= 0 && pending_last == static_cast<long>(i) - 1) {
                    start = pending_start;
                } else if (pending_start >= 0) {
                    out.push_back(make_block(lines, static_cast<size_t>(pending_start),
                                                    static_cast<size_t>(pending_last)));
                }
                pending_start = pending_last = -1;

                unit_start = start;
                depth     += bc.net;
                if (depth <= 0) { // opened and closed on one line
                    out.push_back(make_block(lines, static_cast<size_t>(unit_start), i));
                    unit_start = -1;
                    depth      = 0;
                }
            } else if (!blank) {
                if (pending_start < 0) pending_start = static_cast<long>(i);
                pending_last = static_cast<long>(i);
            }
        } else {
            depth += bc.net;
            if (depth <= 0) {
                out.push_back(make_block(lines, static_cast<size_t>(unit_start), i));
                unit_start = -1;
                depth      = 0;
            }
        }
    }

    // Whatever is still open at end of file is a block in its own right —
    // unbalanced braces are somebody else's problem, not a reason to lose text.
    if (unit_start >= 0)
        out.push_back(make_block(lines, static_cast<size_t>(unit_start), lines.size() - 1));
    else if (pending_start >= 0)
        out.push_back(make_block(lines, static_cast<size_t>(pending_start),
                                        static_cast<size_t>(pending_last)));
    return out;
}

// -------------------------------------------------------------------------
// Indentation family
// -------------------------------------------------------------------------
size_t indent_of(const std::string& s) {
    size_t n = 0;
    for (char c : s) {
        if (c == ' ')       ++n;
        else if (c == '\t') n += 4; // a tab is four columns for depth purposes only
        else break;
    }
    return n;
}

std::vector<CodeBlock> chunk_indentation(const std::vector<std::string>& lines) {
    // A raw unit is one column-zero line plus everything under it. first/last are
    // trimmed to non-blank so trailing blank lines never create false adjacency.
    struct Unit { size_t first; size_t last; bool has_body; };
    std::vector<Unit> units;

    long cur_first = -1;
    long cur_last  = -1;
    bool has_body  = false;

    auto close_unit = [&]() {
        if (cur_first < 0) return;
        units.push_back({static_cast<size_t>(cur_first),
                         static_cast<size_t>(cur_last), has_body});
        cur_first = cur_last = -1;
        has_body  = false;
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        if (is_blank(lines[i])) continue; // blanks belong to nothing in particular
        if (indent_of(lines[i]) == 0) {
            close_unit();
            cur_first = cur_last = static_cast<long>(i);
        } else {
            if (cur_first < 0) { cur_first = static_cast<long>(i); } // indented with no header
            cur_last = static_cast<long>(i);
            has_body = true;
        }
    }
    close_unit();

    // Body-less units (imports, globals, decorators) accumulate; a unit WITH a
    // body absorbs an immediately-adjacent run of them, which is what keeps a
    // decorator with its function.
    std::vector<CodeBlock> out;
    long pending_first = -1;
    long pending_last  = -1;

    auto flush_pending = [&](const std::vector<std::string>& src) {
        if (pending_first < 0) return;
        out.push_back(make_block(src, static_cast<size_t>(pending_first),
                                      static_cast<size_t>(pending_last)));
        pending_first = pending_last = -1;
    };

    for (const Unit& u : units) {
        if (!u.has_body) {
            if (pending_first < 0) pending_first = static_cast<long>(u.first);
            else if (pending_last + 1 != static_cast<long>(u.first)) {
                flush_pending(lines);
                pending_first = static_cast<long>(u.first);
            }
            pending_last = static_cast<long>(u.last);
            continue;
        }

        size_t start = u.first;
        if (pending_first >= 0 && pending_last + 1 == static_cast<long>(u.first)) {
            start         = static_cast<size_t>(pending_first);
            pending_first = pending_last = -1;
        } else {
            flush_pending(lines);
        }
        out.push_back(make_block(lines, start, u.last));
    }
    flush_pending(lines);
    return out;
}

// -------------------------------------------------------------------------
// The two public faces of the chunking (public to this file only)
// -------------------------------------------------------------------------

// Which family a file belongs to, by extension. nullopt means "not code this
// chunking handles" — the walk skips the file rather than chunking it
// wrongly. Extension matching is case-insensitive.
std::optional<ChunkStyle> chunk_style_for_path(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::nullopt;
    const std::string ext = lower(path.substr(dot));

    // The whole language table. Adding a language is one entry here — never new
    // logic, which is the point of the two-family design.
    if (ext == ".cpp" || ext == ".cc"  || ext == ".cxx" || ext == ".c"   ||
        ext == ".h"   || ext == ".hpp" || ext == ".hxx" ||
        ext == ".cs"  || ext == ".kt"  || ext == ".kts" ||
        ext == ".java"||
        ext == ".js"  || ext == ".jsx" || ext == ".ts"  || ext == ".tsx")
        return ChunkStyle::Braces;

    if (ext == ".py") return ChunkStyle::Indentation;

    return std::nullopt; // not code this chunking handles — the walk skips it
}

// Split source text into blocks. Never returns an empty list for non-empty
// input: text that has no structure at all comes back as a single block, which
// is the honest answer rather than nothing.
std::vector<CodeBlock> chunk_source(const std::string& text, ChunkStyle style) {
    const std::vector<std::string> lines = split_lines(text);
    if (lines.empty()) return {};

    std::vector<CodeBlock> out = (style == ChunkStyle::Braces)
        ? chunk_braces(lines)
        : chunk_indentation(lines);

    // Text with no structure at all is one block. Returning nothing would lose
    // the file silently, which is worse than a single oversized block.
    if (out.empty()) out.push_back(make_block(lines, 0, lines.size() - 1));
    return out;
}

// ===========================================================================
// The entry — Project's processing plugged into the Loader
// ===========================================================================

// The Project configuration for one file: one entry, one function. The
// chunking above decides how many pools the file becomes — FUNCTIONAL
// BLOCKS, its call entirely. Blocks carry NO audience: project pools are
// the lever-operated kind, their visibility granted live by masking, the
// same for every block.
LoadEntry entry_for(const std::string& path, ChunkStyle style) {
    LoadEntry e;
    e.path       = path;
    e.class_name = kProjectClass;
    e.facts      = UnitMaskFacts{/*fixed_no_holder=*/false,
                                 /*cascade_exempt=*/false};

    e.process = [style](const std::string& text) {
        std::vector<LoadUnit> units;
        if (text.empty()) return units;   // nothing to load loads nothing

        const std::vector<CodeBlock> blocks = chunk_source(text, style);
        units.reserve(blocks.size());
        for (const auto& block : blocks) {
            LoadUnit u;
            u.text = block.text;
            units.push_back(std::move(u));
        }
        return units;
    };

    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// Ingest — one request, one entry per code file.
// ---------------------------------------------------------------------------
IngestReport ProjectIngest::ingest(const std::string& root_path) {
    IngestReport report;

    std::lock_guard<std::mutex> lock(mutex_);

    // Not ready from the first instant, not from the end. Anything gating on
    // this is held off for the whole operation, not just its tail.
    ready_ = false;
    root_  = root_path;

    std::error_code ec;
    if (!fs::is_directory(root_path, ec)) {
        report.failure = "project path is not a directory: " + root_path;
        return report;
    }

    LoadRequest request;

    fs::recursive_directory_iterator it(root_path,
                                        fs::directory_options::skip_permission_denied,
                                        ec);
    if (ec) {
        report.failure = "cannot walk project: " + ec.message();
        return report;
    }

    for (const auto& dirent : it) {
        if (is_hidden(dirent.path())) {
            if (dirent.is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!dirent.is_regular_file(ec)) continue;

        const std::string path  = dirent.path().string();
        const auto        style = chunk_style_for_path(path);
        if (!style) { ++report.files_skipped; continue; }

        request.entries.push_back(entry_for(path, *style));
    }

    const LoaderReport loaded = loader_.load(request);
    if (!loaded.ok) {
        // The loader reports the partial load precisely; this configuration's
        // policy is all-or-nothing — a partially-resident project is the
        // exact state the readiness gate exists to prevent, so everything
        // any entry stood up leaves RAM before the failure is reported.
        for (const auto& entry : request.entries) loader_.unload(entry.path);
        report.failure = loaded.failure;
        return report;
    }

    sources_.clear();
    for (const auto& er : loaded.entries) {
        if (er.pool_ids.empty()) continue;  // empty file: nothing resident, correctly
        sources_.push_back(er.path);
        report.blocks_minted += er.pool_ids.size();
        ++report.files_ingested;
    }

    ready_    = true;
    report.ok = true;
    return report;
}

// ---------------------------------------------------------------------------
// Single-file reload — the file-grain motion: every pool tied to the file
// destroyed, the file reloaded from disk whole. Fired by the confirmed
// write path (deferred) once the edited Pool IDs have been cross-referenced
// to their file through the registry's files list.
// ---------------------------------------------------------------------------
IngestReport ProjectIngest::reload_file(const std::string& path) {
    IngestReport report;

    std::lock_guard<std::mutex> lock(mutex_);

    // Old first, always. A file that is no longer code never reaches a load
    // at all, so the teardown is explicit here.
    loader_.unload(path);
    sources_.erase(std::remove(sources_.begin(), sources_.end(), path),
                   sources_.end());

    const auto style = chunk_style_for_path(path);
    std::error_code ec;
    if (!style || !fs::is_regular_file(path, ec)) {
        report.ok = true; // deleted or no longer code: gone is the whole outcome
        return report;
    }

    LoadRequest request;
    request.entries.push_back(entry_for(path, *style));

    const LoaderReport loaded = loader_.load(request);
    if (!loaded.ok) {
        loader_.unload(path); // nothing partially standing
        report.failure = loaded.failure;
        return report;
    }

    const EntryReport& er = loaded.entries.front();
    if (!er.pool_ids.empty()) {
        sources_.push_back(path);
        report.blocks_minted  = er.pool_ids.size();
        report.files_ingested = 1;
    }
    report.ok = true; // an emptied file leaving RAM is also the correct outcome
    return report;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
bool ProjectIngest::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_;
}

void ProjectIngest::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : sources_) loader_.unload(s);
    sources_.clear();
    root_.clear();
    ready_ = false;
}

} // namespace prime
