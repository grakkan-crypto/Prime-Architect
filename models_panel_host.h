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

struct LoadedConfig {
    std::vector<AgentEntry> agents;
    std::vector<TeamEntry>  teams;
};
using ConfigLoadFn = std::function<LoadedConfig()>;

using ErrorReportFn = std::function<void(const std::string& message)>;

class ModelsPanelHost {
public:
    ModelsPanelHost(std::string config_path,
                    std::string models_root,
                    ConfigLoadFn load_config,
                    TempReadFn temp_read,
                    NameToDepartmentFn name_to_dept,
                    ErrorReportFn report_error);

    void open();

    void close();

    bool is_open() const { return open_; }

    void draw();

private:
    void do_open_load();
    bool save();
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

}
