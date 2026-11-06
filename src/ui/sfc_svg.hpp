#pragma once
// Draws an SFC chart: steps as boxes in file order, transitions as arrows routed
// down the right-hand side, and the active step marked. A chart is a sequence,
// not a network, so there is no left-to-right layout to share with the ladder
// drawing; what it does share is the model the session compiled, so the drawing
// and the running chart cannot disagree about a step name.
#include "plc/sfc.hpp"

#include <string>
#include <vector>

namespace sc::ui {

struct SfcSvgOptions {
    // One flag per step, in chart order. Empty or short means "not known yet",
    // which is what an uncompiled chart looks like.
    std::vector<bool> active;
};

std::string sfc_chart_svg(const plc::SfcChart& chart, const SfcSvgOptions& options);

}  // namespace sc::ui
