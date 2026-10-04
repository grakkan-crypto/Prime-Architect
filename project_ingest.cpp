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

constexpr const char* kProjectClass = "Project";

bool is_hidden(const fs::path& p) {
    const std::string name = p.filename().string();
    return !name.empty() && name.front() == '.';
}

enum class ChunkStyle {
    Braces,
    Indentation,
};

struct CodeBlock {
    uint32_t    first_line = 0;
    uint32_t    last_line  = 0;
    std::string text;
};

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
    blk.first_line = static_cast<uint32_t>(a + 1);
    blk.last_line  = static_cast<uint32_t>(b + 1);
    blk.text       = join(lines, a, b);
    return blk;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

struct BraceCount {
    int opened = 0;
    int net    = 0;
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
            if (c == '\\') { ++i; continue; }
            if (c == '"')  in_string = false;
            continue;
        }
        if (in_char) {
            if (c == '\\') { ++i; continue; }
            if (c == '\'') in_char = false;
            continue;
        }

        if (c == '/' && n == '/') break;
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

    long pending_start = -1;
    long pending_last  = -1;
    long unit_start    = -1;

    for (size_t i = 0; i < lines.size(); ++i) {
        const bool blank = is_blank(lines[i]);

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
                if (depth <= 0) {
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

    if (unit_start >= 0)
        out.push_back(make_block(lines, static_cast<size_t>(unit_start), lines.size() - 1));
    else if (pending_start >= 0)
        out.push_back(make_block(lines, static_cast<size_t>(pending_start),
                                        static_cast<size_t>(pending_last)));
    return out;
}

size_t indent_of(const std::string& s) {
    size_t n = 0;
    for (char c : s) {
        if (c == ' ')       ++n;
        else if (c == '\t') n += 4;
        else break;
    }
    return n;
}

std::vector<CodeBlock> chunk_indentation(const std::vector<std::string>& lines) {

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
        if (is_blank(lines[i])) continue;
        if (indent_of(lines[i]) == 0) {
            close_unit();
            cur_first = cur_last = static_cast<long>(i);
        } else {
            if (cur_first < 0) { cur_first = static_cast<long>(i); }
            cur_last = static_cast<long>(i);
            has_body = true;
        }
    }
    close_unit();

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

std::optional<ChunkStyle> chunk_style_for_path(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::nullopt;
    const std::string ext = lower(path.substr(dot));

    if (ext == ".cpp" || ext == ".cc"  || ext == ".cxx" || ext == ".c"   ||
        ext == ".h"   || ext == ".hpp" || ext == ".hxx" ||
        ext == ".cs"  || ext == ".kt"  || ext == ".kts" ||
        ext == ".java"||
        ext == ".js"  || ext == ".jsx" || ext == ".ts"  || ext == ".tsx")
        return ChunkStyle::Braces;

    if (ext == ".py") return ChunkStyle::Indentation;

    return std::nullopt;
}

std::vector<CodeBlock> chunk_source(const std::string& text, ChunkStyle style) {
    const std::vector<std::string> lines = split_lines(text);
    if (lines.empty()) return {};

    std::vector<CodeBlock> out = (style == ChunkStyle::Braces)
        ? chunk_braces(lines)
        : chunk_indentation(lines);

    if (out.empty()) out.push_back(make_block(lines, 0, lines.size() - 1));
    return out;
}

LoadEntry entry_for(const std::string& path, ChunkStyle style) {
    LoadEntry e;
    e.path       = path;
    e.class_name = kProjectClass;
    e.facts      = UnitMaskFacts{false,
                                 false};

    e.process = [style](const std::string& text) {
        std::vector<LoadUnit> units;
        if (text.empty()) return units;

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

}

IngestReport ProjectIngest::ingest(const std::string& root_path) {
    IngestReport report;

    std::lock_guard<std::mutex> lock(mutex_);

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

        for (const auto& entry : request.entries) loader_.unload(entry.path);
        report.failure = loaded.failure;
        return report;
    }

    sources_.clear();
    for (const auto& er : loaded.entries) {
        if (er.pool_ids.empty()) continue;
        sources_.push_back(er.path);
        report.blocks_minted += er.pool_ids.size();
        ++report.files_ingested;
    }

    ready_    = true;
    report.ok = true;
    return report;
}

IngestReport ProjectIngest::reload_file(const std::string& path) {
    IngestReport report;

    std::lock_guard<std::mutex> lock(mutex_);

    loader_.unload(path);
    sources_.erase(std::remove(sources_.begin(), sources_.end(), path),
                   sources_.end());

    const auto style = chunk_style_for_path(path);
    std::error_code ec;
    if (!style || !fs::is_regular_file(path, ec)) {
        report.ok = true;
        return report;
    }

    LoadRequest request;
    request.entries.push_back(entry_for(path, *style));

    const LoaderReport loaded = loader_.load(request);
    if (!loaded.ok) {
        loader_.unload(path);
        report.failure = loaded.failure;
        return report;
    }

    const EntryReport& er = loaded.entries.front();
    if (!er.pool_ids.empty()) {
        sources_.push_back(path);
        report.blocks_minted  = er.pool_ids.size();
        report.files_ingested = 1;
    }
    report.ok = true;
    return report;
}

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

}
