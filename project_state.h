// project_state.h — Pipeline identity and active project (frontier)
//
// Two related things a turn needs to know: WHICH pipeline is loaded (and where
// its files live), and WHICH project/file is currently open for work. Kept
// together because they move together — a pipeline load establishes both the
// pipeline identity and the project it operates on.
//
// PIPELINE IDENTITY (HANDOVER_Session24 §4.1; standing instruction #21)
//   pipeline_name — a matrix file name. First-class frontend state: it names the
//                   pipeline, drives [pipeline_name]-rules.json and
//                   [pipeline_name]-temps.json resolution, and is the key the
//                   engine loads against. NOT a pool name, NOT a disk path.
//   config_path   — the stamped config.json the engine resolves agents from.
//   storage_root  — a disk path: where per-pipeline files (rules, temps,
//                   backups) live. NOT a pool name, NOT the pipeline name. The
//                   three are never conflated (standing instruction #21).
//
// ACTIVE PROJECT (carried forward — real, in-use state)
//   active_project + file_buffer are load-bearing for existing behaviour: the
//   read-only gate on writes, the audit baseline the Analyst reasons against,
//   and project identity for storage keying and workspace restore. They survive
//   the conversion because they do real work, not because the legacy file had
//   them.
//
//   NOTE — projectTokenCount is NOT carried. It existed only to hand a fixed
//   token count to Vulkan for static pool sizing. Pools are dynamic now and take
//   any size, so the count has no consumer. Dropped deliberately, on that
//   reasoning — not merely "unreferenced".

#pragma once

#include <optional>
#include <string>

namespace prime::frontend {

struct ActiveProject {
    std::string name;
    std::string path;
    bool        read_only = false;

    bool loaded() const { return !path.empty(); }
};

class ProjectState {
public:
    ProjectState() = default;

    void set_pipeline(const std::string& pipeline_name,
                      const std::string& config_path,
                      const std::string& storage_root);

    const std::string& pipeline_name() const { return pipeline_name_; }
    const std::string& config_path()   const { return config_path_; }
    const std::string& storage_root()  const { return storage_root_; }
    bool               has_pipeline()  const { return !pipeline_name_.empty(); }

    void open_project(const std::string& name, const std::string& path,
                      bool read_only);
    const ActiveProject& project() const { return project_; }

    void set_file_buffer(std::string content);
    void clear_file_buffer();
    const std::optional<std::string>& file_buffer() const { return file_buffer_; }

    void clear();

private:
    std::string   pipeline_name_;
    std::string   config_path_;
    std::string   storage_root_;

    ActiveProject               project_;
    std::optional<std::string>  file_buffer_;
};

}
