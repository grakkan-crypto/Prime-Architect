// cylon_bar.cpp

#include "cylon_bar.h"

#include "imgui.h"

#include <cmath>

namespace prime {

void draw_cylon_bar(bool active, float height) {
    ImDrawList*  draw   = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float  width  = ImGui::GetContentRegionAvail().x;

    if (width <= 0.0f) return;

    const float rounding = height * 0.5f;

    // Track — always drawn, so the bar stays put and just stops moving.
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                        IM_COL32(17, 17, 17, 255), rounding);

    if (active) {
        const float scanner = width * 0.30f;
        const float travel  = width - scanner;

        // 1.2s each way.
        const float phase = static_cast<float>(std::fmod(ImGui::GetTime(), 2.4));
        const float ramp  = (phase < 1.2f) ? (phase / 1.2f) : ((2.4f - phase) / 1.2f);
        const float eased = ramp * ramp * (3.0f - 2.0f * ramp);

        const float x = origin.x + travel * eased;
        draw->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + scanner, origin.y + height),
                            IM_COL32(56, 139, 253, 255), rounding);
    }

    ImGui::Dummy(ImVec2(width, height));
}

} // namespace prime
