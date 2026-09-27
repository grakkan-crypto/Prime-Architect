// layout_tabs.cpp — The layout tab bar implementation

#include "layout_tabs.h"

namespace prime {

size_t LayoutTabs::open(const std::string& pipeline,
                        const std::string& indicator) {
    LayoutTab t;
    t.pipeline  = pipeline;
    t.indicator = indicator;
    tabs_.push_back(std::move(t));

    active_ = tabs_.size() - 1;
    return active_;
}

bool LayoutTabs::close(size_t index) {
    if (index >= tabs_.size()) return false;

    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));

    if (tabs_.empty()) {
        active_ = 0;
        return true;
    }

    if (active_ >= tabs_.size()) active_ = tabs_.size() - 1;
    else if (index < active_)    --active_;

    return true;
}

void LayoutTabs::select(size_t index) {
    if (index < tabs_.size()) active_ = index;
}

const LayoutTab* LayoutTabs::active() const {
    if (tabs_.empty() || active_ >= tabs_.size()) return nullptr;
    return &tabs_[active_];
}

LayoutTab* LayoutTabs::active() {
    if (tabs_.empty() || active_ >= tabs_.size()) return nullptr;
    return &tabs_[active_];
}

std::string LayoutTabs::active_pipeline() const {
    const LayoutTab* t = active();
    return t ? t->pipeline : std::string();
}

std::string LayoutTabs::active_indicator() const {
    const LayoutTab* t = active();
    return t ? t->indicator : std::string();
}

void LayoutTabs::set_active_indicator(const std::string& text) {
    if (LayoutTab* t = active()) t->indicator = text;
}

} // namespace prime
