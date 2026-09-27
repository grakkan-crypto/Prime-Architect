// models_panel_host.cpp — Prime Architect Models panel host layer
//
// FIRST-PASS SCAFFOLD (see header). Owns lifecycle and orchestration.

#include "models_panel_host.h"
#include "config_writer.h"

#include "imgui.h"

namespace prime {

ModelsPanelHost::ModelsPanelHost(std::string config_path,
                                 std::string models_root,
                                 ConfigLoadFn load_config,
                                 TempReadFn temp_read,
                                 NameToDepartmentFn name_to_dept,
                                 ErrorReportFn report_error)
    : config_path_(std::move(config_path)),
      load_config_(std::move(load_config)),
      report_error_(std::move(report_error)),
      discovery_(std::move(models_root)),
      render_(agents_, teams_, discovery_,
              std::move(temp_read),
              std::move(name_to_dept),
              [this] { mark_dirty(); }) {}

void ModelsPanelHost::open() {
    do_open_load();
    open_  = true;
    dirty_ = false;
}

void ModelsPanelHost::close() {
    open_ = false;
}

void ModelsPanelHost::do_open_load() {
    // Rescan the disk fresh every open — the walk is how bloat becomes visible.
    discovery_.scan();

    // Load current config into the authoring layers, if a loader is provided.
    // Absent loader means start empty (e.g. no config yet); that is a legitimate
    // clean state, not an error to invent an entry over.
    if (load_config_) {
        LoadedConfig loaded = load_config_();
        agents_.set_entries(std::move(loaded.agents));
        teams_.set_teams(std::move(loaded.teams));
    }

    render_.reset_view();
}

bool ModelsPanelHost::save() {
    const bool ok = ConfigWriter::write(config_path_,
                                        agents_.entries(),
                                        teams_.teams());
    if (!ok) {
        if (report_error_)
            report_error_("Failed to write config to " + config_path_);
        return false;   // surfaced, not swallowed
    }
    dirty_ = false;
    return true;
}

void ModelsPanelHost::draw() {
    if (!open_) return;

    // First-pass: a plain window standing in for the settings-reached popup.
    if (ImGui::Begin("Models", &open_)) {
        // Save bar.
        if (dirty_) {
            if (ImGui::Button("Save"))
                save();
            ImGui::SameLine();
            ImGui::TextDisabled("unsaved changes");
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("Save");
            ImGui::EndDisabled();
        }
        ImGui::Separator();

        render_.draw();
    }
    ImGui::End();

    // The window's close box flipped open_ via the &open_ binding; nothing else
    // to reconcile — save policy is explicit, so closing does not auto-persist.
}

} // namespace prime
