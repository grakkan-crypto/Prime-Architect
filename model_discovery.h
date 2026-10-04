// model_discovery.h — Prime Architect frontend model discovery
//
// Walks the Models root and reports what is physically on disk, department by
// department. Pure filesystem observation: it maps folders to departments and
// files/subfolders to model entries. It does not load weights, assign agents,
// choose compute targets, dedup, or persist anything. Discovery in, a truthful
// picture of the disk out.
//
// SINGLE RESPONSIBILITY
//   Scan Models root -> per-department lists of discovered models. Every entry
//   the walk produces is reported; nothing is collapsed or hidden at this layer.
//
// DEPARTMENT AUTHORITY IS THE FOLDER
//   Each top-level directory under the root IS a department, named by the
//   folder. There is no hardcoded department list here — dropping a new folder
//   in (prompts, rules, models inside) is how a department comes into being.
//   The single reserved name "Raw_Models" is skipped: it is a staging area, not
//   a department.
//
// GGUF ONLY
//   Two shapes are recognised as loadable models:
//     - a single .gguf file sitting directly in a department folder;
//     - a subfolder containing one or more .gguf files (a split model; the
//       first part by sorted name is the load handle, part count recorded).
//   ONNX is retired. There is no ONNX/hybrid/NPU detection path.
//
// MALFORMED ENTRIES ARE SURFACED, NOT SWALLOWED
//   A subfolder that contains NO .gguf is not silently ignored — under a
//   GGUF-only world it cannot be classified, so it is reported as an entry with
//   kind == Malformed and a stated reason. It is excluded from the loadable set
//   (no path an agent could bind to) but remains visible in discovery output so
//   an expected-but-absent model has a traceable cause rather than a silent gap.
//
// NON-LOSSY
//   No deduplication here. Two folders holding the same weight file are two
//   entries. Collapsing by path is a downstream concern (the config load view
//   owns it); this layer's job is to show the true on-disk state, including
//   redundancy the operator may want to prune.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace prime {

enum class ModelKind {
    SingleGGUF,
    SplitGGUF,
    Malformed
};

struct DiscoveredModel {
    std::string name;
    std::string department;
    ModelKind   kind = ModelKind::SingleGGUF;

    std::string load_path;

    std::string source_dir;

    int parts_count = 1;

    std::string error;
};

class ModelDiscovery {
public:
    explicit ModelDiscovery(std::string root_path);

    void scan();

    const std::unordered_map<std::string, std::vector<DiscoveredModel>>&
        by_department() const { return by_department_; }

    std::vector<std::string> departments() const;

    const std::vector<DiscoveredModel>& all() const { return all_; }

    std::vector<DiscoveredModel> loadable_for(const std::string& department) const;

private:
    std::vector<DiscoveredModel> scan_department(const std::string& dept_name,
                                                 const std::string& dept_path) const;

    std::string root_path_;
    std::unordered_map<std::string, std::vector<DiscoveredModel>> by_department_;
    std::vector<DiscoveredModel> all_;
};

}
