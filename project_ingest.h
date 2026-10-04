// project_ingest.h — the Project configuration of the file loader
//
// ONE JOB: say what "loading a project" means, and fire the loader to do it.
// A project is a directory walked file by file. Each code file is one entry
// — one path, one function — and the chunker inside the function decides how
// many pools that one file becomes: FUNCTIONAL BLOCKS, not files. Every
// project pool carries the same mask facts: lever-operated, never fixed,
// never cascade-exempt — who sees a block is granted live by masking,
// universally, straight from what LiveRegistry declares. Nothing per-block
// is authored here, and nothing here is named: a pool's identity is the
// Pool ID it mints for itself, and the registry's files list cross-
// references each Pool ID against the file it came from.
//
// PROJECT POOLS MINT OUTSIDE THE TURN SEQUENCE
//   No Turn ID, no Prompt ID — those stamps land at mint or never, and
//   these mints have no turn to stamp. Established, and relied on: standing
//   project pools never gather stamp noise.
//
// RELOAD IS FILE-GRAIN
//   When edited pools are identified (their Pool IDs are known to whatever
//   confirmed the write), the files list says which file(s) those IDs
//   belong to; every pool tied to those files is destroyed and the files
//   reload from disk whole. reload_file is that motion for one file. The
//   TRIGGER — the confirmed write path — is deferred; this file assumes it
//   works and builds around it.
//
// WHAT STAYED
//   The directory walk — a directory being the unit of a project load is
//   project meaning. The readiness gate — false from the first instant to
//   the last pool, because answering against a half-resident project is
//   worse than failing loudly; a partial load reported by the loader is
//   torn down whole here.

#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace prime {

class FileLoader;

struct IngestReport {
    bool        ok = false;
    std::string failure;
    size_t      files_ingested = 0;
    size_t      blocks_minted  = 0;
    size_t      files_skipped  = 0;
};

class ProjectIngest {
public:
    explicit ProjectIngest(FileLoader& loader) : loader_(loader) {}

    ProjectIngest(const ProjectIngest&)            = delete;
    ProjectIngest& operator=(const ProjectIngest&) = delete;

    IngestReport ingest(const std::string& root_path);

    IngestReport reload_file(const std::string& path);

    bool ready() const;

    void clear();

    const std::string& root() const { return root_; }

private:
    FileLoader& loader_;

    mutable std::mutex       mutex_;
    std::string              root_;
    std::vector<std::string> sources_;
    bool                     ready_ = false;
};

}
