// cylon_bar.h — The liveness bar.
//
// Sits under an indicator's text. Always there; scans left-right while this
// pipeline's agents are working, still when they aren't. That's how you tell
// it hasn't hung.

#pragma once

namespace prime {

void draw_cylon_bar(bool active, float height = 3.0f);

}
