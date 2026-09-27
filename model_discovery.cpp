// model_discovery.cpp — Prime Architect frontend model discovery implementation

#include "model_discovery.h"

#include <algorithm>
#include <filesystem>

namespace prime {

namespace fs = std::filesystem;

namespace {

// The one reserved top-level name that is not a department.
constexpr const char* kReservedStaging = "Raw_Models";

bool is_gguf(const fs::path& p) {
    if (!p.has_extension()) return false;
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".gguf";
}

} // namespace

ModelDiscovery::ModelDiscovery(std::string root_path)
    : root_path_(std::move(root_path)) {}

void ModelDiscovery::scan() {
    by_department_.clear();
    all_.clear();

    fs::path root(root_path_);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        // Root itself is absent or not a directory. Nothing to report — an empty
        // picture is a truthful picture of "no Models root here". Callers that
        // expected models will see an empty result and can surface that at their
        // own layer; discovery does not invent entries.
        return;
    }

    for (const auto& dept_entry : fs::directory_iterator(root)) {
        if (!dept_entry.is_directory()) continue;

        const std::string dept_name = dept_entry.path().filename().string();
        if (dept_name == kReservedStaging) continue;

        std::vector<DiscoveredModel> models =
            scan_department(dept_name, dept_entry.path().string());

        for (const auto& m : models) all_.push_back(m);
        by_department_.emplace(dept_name, std::move(models));
    }
}

std::vector<DiscoveredModel>
ModelDiscovery::scan_department(const std::string& dept_name,
                                const std::string& dept_path) const {
    std::vector<DiscoveredModel> models;
    fs::path dept(dept_path);

    for (const auto& item : fs::directory_iterator(dept)) {
        const fs::path& item_path = item.path();
        const std::string item_name = item_path.filename().string();

        // Skip dotfiles / dot-directories.
        if (!item_name.empty() && item_name.front() == '.') continue;

        if (item.is_directory()) {
            // Collect .gguf parts inside the subfolder, sorted by name so the
            // first part is a stable load handle for split models.
            std::vector<fs::path> parts;
            for (const auto& sub : fs::directory_iterator(item_path))
                if (sub.is_regular_file() && is_gguf(sub.path()))
                    parts.push_back(sub.path());

            if (parts.empty()) {
                // Under GGUF-only, a subfolder with no .gguf cannot be classified.
                // Surface it as a Malformed entry rather than dropping it.
                DiscoveredModel m;
                m.name        = item_name;
                m.department  = dept_name;
                m.kind        = ModelKind::Malformed;
                m.source_dir  = item_path.string();
                m.parts_count = 0;
                m.error       = "Subfolder contains no .gguf files "
                                "(ONNX retired; nothing loadable here).";
                models.push_back(std::move(m));
                continue;
            }

            std::sort(parts.begin(), parts.end());

            DiscoveredModel m;
            m.name        = item_name;
            m.department  = dept_name;
            m.kind        = ModelKind::SplitGGUF;
            m.load_path   = parts.front().string();
            m.source_dir  = item_path.string();
            m.parts_count = static_cast<int>(parts.size());
            models.push_back(std::move(m));
        }
        else if (item.is_regular_file() && is_gguf(item_path)) {
            DiscoveredModel m;
            m.name        = item_path.stem().string();
            m.department  = dept_name;
            m.kind        = ModelKind::SingleGGUF;
            m.load_path   = item_path.string();
            m.source_dir  = dept_path;
            m.parts_count = 1;
            models.push_back(std::move(m));
        }
        // Any other file type (including stray .onnx) is not a recognised model
        // shape and is not reported — it is not a subfolder that failed to
        // classify, it is simply not a model. Only unclassifiable *subfolders*
        // surface as Malformed.
    }

    return models;
}

std::vector<std::string> ModelDiscovery::departments() const {
    std::vector<std::string> out;
    out.reserve(by_department_.size());
    for (const auto& kv : by_department_) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<DiscoveredModel>
ModelDiscovery::loadable_for(const std::string& department) const {
    std::vector<DiscoveredModel> out;
    auto it = by_department_.find(department);
    if (it == by_department_.end()) return out;
    for (const auto& m : it->second)
        if (m.kind != ModelKind::Malformed)
            out.push_back(m);
    return out;
}

} // namespace prime
