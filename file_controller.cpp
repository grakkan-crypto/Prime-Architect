// file_controller.cpp — File controller implementation. Logic only; data is
// the registry's.

#include "file_controller.h"

#include "live_registry.h"

namespace prime {

void FileController::tag(const std::string& pool_name, const std::string& file) {
    registry_.set_pool_file_tag(pool_name, file);
}

std::optional<std::string> FileController::file_of(const std::string& pool_name) const {
    return registry_.pool_file_tag(pool_name);
}

std::vector<std::string> FileController::pools_of(const std::string& file) const {
    return registry_.pools_with_file_tag(file);
}

void FileController::untag(const std::string& pool_name) {
    registry_.clear_pool_file_tag(pool_name);
}

void FileController::untag_file(const std::string& file) {
    registry_.clear_file_tags(file);
}

} // namespace prime
