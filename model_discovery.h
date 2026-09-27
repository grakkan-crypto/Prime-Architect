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
    SingleGGUF,   // one .gguf file directly in the department folder
    SplitGGUF,    // subfolder of .gguf parts; load_path is the first part
    Malformed     // subfolder with no .gguf — reported, not loadable
};

struct DiscoveredModel {
    std::string name;        // file stem (single) or folder name (split/malformed)
    std::string department;  // owning department (top-level folder name)
    ModelKind   kind = ModelKind::SingleGGUF;

    // Absolute path to bind for loading. For SingleGGUF, the file. For SplitGGUF,
    // the first part by sorted name. Empty for Malformed — nothing to load.
    std::string load_path;

    // The directory the entry was found at (the subfolder for split/malformed,
    // the file's parent for single). Present for every kind, so a Malformed
    // entry can point the operator at the folder that failed to classify.
    std::string source_dir;

    // Split models only: number of .gguf parts found. 1 for SingleGGUF, 0 for
    // Malformed.
    int parts_count = 1;

    // Malformed only: why it could not be classified. Empty otherwise.
    std::string error;
};

class ModelDiscovery {
public:
    explicit ModelDiscovery(std::string root_path);

    // Full walk from scratch. Clears prior results and re-reads the disk. Called
    // on every settings-page open — the walk itself is the mechanism by which
    // on-disk bloat becomes visible, so it is intentionally uncached.
    void scan();

    // Per-department discovered models, in scan order. Includes Malformed entries.
    const std::unordered_map<std::string, std::vector<DiscoveredModel>>&
        by_department() const { return by_department_; }

    // The authoritative department list for the panel's grouping order (folder
    // scan order, first-seen). Includes departments with zero loadable models.
    //
    // FLAGGED: a folder dropped in here is a new department the instant it's
    // scanned — but that alone does NOT give it a kernel call. Contract, and
    // therefore which KernelBackend method it runs through, is resolved by a
    // fixed, closed list in dispatch.cpp's contract_from_department(), bound
    // once per agent at pipeline_routes.cpp's bind_fleet(). That list's default
    // for anything it doesn't name is TextToText — silently. A new department
    // meant to be a specialist (image, audio, video) that isn't added to
    // contract_from_department() will bind as an ordinary text agent and never
    // reach the kernel call it actually needs, with no error anywhere. Adding a
    // department here is not the whole job; contract_from_department() is the
    // second half of it, every time.
    std::vector<std::string> departments() const;

    // Includes Malformed entries.
    const std::vector<DiscoveredModel>& all() const { return all_; }

    // Convenience: loadable models (SingleGGUF | SplitGGUF) for one department,
    // Malformed excluded. Empty vector if the department has none or is unknown.
    std::vector<DiscoveredModel> loadable_for(const std::string& department) const;

private:
    std::vector<DiscoveredModel> scan_department(const std::string& dept_name,
                                                 const std::string& dept_path) const;

    std::string root_path_;
    std::unordered_map<std::string, std::vector<DiscoveredModel>> by_department_;
    std::vector<DiscoveredModel> all_;
};

} // namespace prime
