// diagnostics_panel_render.h — The diagnostics panel.
//
// This is what says what's going on internally. Two tabs, because there are two
// logs:
//   Feed    — running lines, errors flagged.
//   Journey — each agent step: what it got, what it thought, what it produced.
//
// Both logs are written to directly by whatever is running. This reads and
// draws them.

#pragma once

#include "diagnostics_state.h"

namespace prime {

class DiagnosticsPanelRender {
public:
    DiagnosticsPanelRender(prime::frontend::DiagnosticsLog& diagnostics,
                           prime::frontend::JourneyLog&     journey);

    void draw();

private:
    void draw_feed();
    void draw_journey();

    prime::frontend::DiagnosticsLog& diagnostics_;
    prime::frontend::JourneyLog&     journey_;

    bool errors_only_ = false;
};

} // namespace prime
