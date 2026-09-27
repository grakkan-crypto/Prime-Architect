// file_controller.h — The file-tag controller.
//
// PURE CONTROLLER. Creates the tag, reads the tag, holds NOTHING. The data —
// which pool carries which file tag — lives in LiveRegistry's files section
// (ONE place, ONE lookup), the same way every other permission-or-agnostic
// continual-lookup fact does. This file is the logic over that data, never a
// second holder of it.
//
// GENERIC BY DESIGN — NOT PROJECT-SPECIFIC
//   A file tag says "this pool's content traces back to this file on disk."
//   A project source file is ONE kind of file; the pipeline's rules content,
//   once loaded into a pool, is another; more arrive as the system grows.
//   One mechanism, used by whichever domain has a file-backed pool, owned by
//   none of them. There is no "project" column and never will be — the tag is
//   a file, full stop.
//
// WHAT THIS ENABLES
//   When a file on disk changes, every pool carrying its tag can be found and
//   destroyed before the new version is minted — so a stale version is never
//   resident. Finding is this controller's job; destroying is the pool file's,
//   called by whoever owns the reload (the caller). This controller never
//   creates or destroys a pool.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace prime {

class LiveRegistry;

class FileController {
public:
    explicit FileController(LiveRegistry& registry) : registry_(registry) {}

    FileController(const FileController&)            = delete;
    FileController& operator=(const FileController&) = delete;

    // Create the tag: this pool's content traces back to this file. One tag
    // per pool — a pool's content comes from one file. Tagging an already-
    // tagged pool replaces the tag (the pool's content IS the new file's now).
    void tag(const std::string& pool_name, const std::string& file);

    // Read the tag. Empty optional means the pool carries no file tag — a
    // fact, not an error: most pools trace back to no file.
    std::optional<std::string> file_of(const std::string& pool_name) const;

    // Every pool currently tagged with this file — the reload question: "what
    // must go before the new version is minted."
    std::vector<std::string> pools_of(const std::string& file) const;

    // Drop one pool's tag. Called when the pool is destroyed, so a dead
    // pool's tag never lingers in the registry.
    void untag(const std::string& pool_name);

    // Drop every tag for a file. The file itself is gone from the system.
    void untag_file(const std::string& file);

private:
    LiveRegistry& registry_;
};

} // namespace prime
