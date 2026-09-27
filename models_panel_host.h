// models_panel_host.h — Prime Architect Models panel, host layer
//
// FIRST-PASS SCAFFOLD. Owns the panel's LIFECYCLE, distinct from its drawing.
// Opening the panel (reached via settings-menu navigation) is a different event
// from drawing it each frame: the open transition is what triggers a fresh disk
// rescan and loads current config into the authoring layers; drawing just paints
// whatever state exists. That open-vs-draw seam is the reason host and render
// are separate files, and it is the convention this first panel sets.
//
// SINGLE RESPONSIBILITY
//   Own is-open state. On open: rescan discovery, load config into the authoring
//   layers, reset the render view. Each frame while open: draw the render layer
//   inside a window/popup. On the operator's save action: serialize to disk and
//   surface failure. It does not itself author, draw panes, or walk the disk —
//   it orchestrates the pieces that do.
//
// SAVE POLICY (first-pass: explicit)
//   A dirty flag is raised by any authoring change (via the render layer's
//   DirtyFn) and cleared on a successful write. First pass uses an explicit
//   "Save" action; on-change or on-close autosave can replace this later without
//   touching render or the authoring layers. A failed write is surfaced, never
//   swallowed.
//
// RESCAN ON EVERY OPEN
//   Discovery re-walks the disk on each open, uncached — the walk is the
//   mechanism by which on-disk model bloat becomes visible for pruning.

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "agent_config.h"
#include "team_config.h"
#include "model_discovery.h"
#include "models_panel_render.h"

namespace prime {

// Loads existing config.json into the authoring layers on panel open. Supplied
// by the host's owner (whoever holds the config path and a reader). Returns the
// parsed agents and teams; the host installs them into AgentConfig/TeamConfig.
struct LoadedConfig {
    std::vector<AgentEntry> agents;
    std::vector<TeamEntry>  teams;
};
using ConfigLoadFn = std::function<LoadedConfig()>;

// Surfaces a save failure to the operator (first-pass hook; wire to whatever the
// app uses for user-visible errors).
using ErrorReportFn = std::function<void(const std::string& message)>;

class ModelsPanelHost {
public:
    ModelsPanelHost(std::string config_path,
                    std::string models_root,
                    ConfigLoadFn load_config,
                    TempReadFn temp_read,
                    NameToDepartmentFn name_to_dept,
                    ErrorReportFn report_error);

    // Called from the settings menu. Triggers rescan + config load + view reset,
    // then marks the panel open.
    void open();

    // Marks closed. Does not force a save (save policy is explicit this pass);
    // an owner wanting save-on-close can call save() before close().
    void close();

    bool is_open() const { return open_; }

    // Draw one frame. No-op when closed. Hosts the render layer inside a window
    // titled for the settings context; drives the explicit Save action.
    void draw();

private:
    void do_open_load();          // rescan + config load + view reset
    bool save();                  // serialize to disk; false on failure
    void mark_dirty() { dirty_ = true; }

    std::string     config_path_;
    ConfigLoadFn    load_config_;
    ErrorReportFn   report_error_;

    ModelDiscovery  discovery_;
    AgentConfig     agents_;
    TeamConfig      teams_;
    ModelsPanelRender render_;

    bool open_  = false;
    bool dirty_ = false;
};

} // namespace prime
