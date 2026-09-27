// project_state.cpp — Pipeline identity and active project implementation (frontier)

#include "project_state.h"

namespace prime::frontend {

void ProjectState::set_pipeline(const std::string& pipeline_name,
                                const std::string& config_path,
                                const std::string& storage_root) {
    pipeline_name_ = pipeline_name;
    config_path_   = config_path;
    storage_root_  = storage_root;
}

void ProjectState::open_project(const std::string& name, const std::string& path,
                                bool read_only) {
    project_ = ActiveProject{name, path, read_only};
    file_buffer_.reset(); // a newly-opened project has no buffer until loaded
}

void ProjectState::set_file_buffer(std::string content) {
    file_buffer_ = std::move(content);
}

void ProjectState::clear_file_buffer() {
    file_buffer_.reset();
}

void ProjectState::clear() {
    pipeline_name_.clear();
    config_path_.clear();
    storage_root_.clear();
    project_ = ActiveProject{};
    file_buffer_.reset();
}

} // namespace prime::frontend
