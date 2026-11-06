#include "ui/sfc_svg.hpp"

#include "plc/ladder_text.hpp"
#include "ui/svg_text.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sc::ui {
namespace {

constexpr int kStepWidth = 220;
constexpr int kStepGap = 52;   // room between boxes for a transition to turn in
constexpr int kStepBase = 28;
constexpr int kActionHeight = 16;
constexpr int kRouteGap = 16;
constexpr int kTextRoom = 160;  // the guard label beside the rightmost route

int step_height(const plc::SfcStep& step) {
    return kStepBase + static_cast<int>(step.actions.size()) * kActionHeight;
}

std::string rect(const char* css, int x, int y, int width, int height) {
    return "<rect class=\"" + std::string(css) + "\" x=\"" + std::to_string(x) +
           "\" y=\"" + std::to_string(y) + "\" width=\"" + std::to_string(width) +
           "\" height=\"" + std::to_string(height) + "\" rx=\"4\"/>";
}

std::string text(int x, int y, std::string_view label, const char* anchor = "start") {
    return "<text x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) +
           "\" text-anchor=\"" + std::string(anchor) + "\">" + svg_escape(label) +
           "</text>";
}

std::string line(int x1, int y1, int x2, int y2) {
    return "<line class=\"wire\" x1=\"" + std::to_string(x1) + "\" y1=\"" +
           std::to_string(y1) + "\" x2=\"" + std::to_string(x2) + "\" y2=\"" +
           std::to_string(y2) + "\"/>";
}

// Points back into the step it enters, because transitions read downward.
std::string arrow(int x, int y) {
    return "<polygon class=\"arrow\" points=\"" + std::to_string(x) + "," +
           std::to_string(y) + " " + std::to_string(x + 8) + "," + std::to_string(y - 5) +
           " " + std::to_string(x + 8) + "," + std::to_string(y + 5) + "\"/>";
}

std::size_t step_index(const plc::SfcChart& chart, std::string_view name) {
    for (std::size_t i = 0; i < chart.steps.size(); ++i) {
        if (chart.steps[i].name == name) {
            return i;
        }
    }
    return chart.steps.size();
}

}  // namespace

std::string sfc_chart_svg(const plc::SfcChart& chart, const SfcSvgOptions& options) {
    std::vector<int> tops;
    std::vector<int> heights;
    int y = 0;
    for (const plc::SfcStep& step : chart.steps) {
        tops.push_back(y);
        heights.push_back(step_height(step));
        y += heights.back() + kStepGap;
    }

    const int height = chart.steps.empty() ? 0 : y - kStepGap;
    const int width = kStepWidth + 40 +
                      static_cast<int>(chart.transitions.size()) * kRouteGap + kTextRoom;

    std::string body;
    for (std::size_t i = 0; i < chart.steps.size(); ++i) {
        const plc::SfcStep& step = chart.steps[i];
        const bool active = i < options.active.size() && options.active[i];
        body += rect(active ? "step active" : "step", 0, tops[i], kStepWidth, heights[i]);
        body += text(12, tops[i] + 19, step.name);
        if (i == chart.initial) {
            body += text(kStepWidth - 12, tops[i] + 19, "initial", "end");
        }
        for (std::size_t a = 0; a < step.actions.size(); ++a) {
            body += text(12, tops[i] + 36 + static_cast<int>(a) * kActionHeight,
                         "+ " + step.actions[a].tag);
        }
    }

    for (std::size_t k = 0; k < chart.transitions.size(); ++k) {
        const plc::SfcTransition& transition = chart.transitions[k];
        const std::size_t from = step_index(chart, transition.from);
        const std::size_t to = step_index(chart, transition.to);
        if (from >= chart.steps.size() || to >= chart.steps.size()) {
            continue;
        }

        const int from_y = tops[from] + heights[from] / 2;
        const int to_y = tops[to] + heights[to] / 2;
        const int route = kStepWidth + 30 + static_cast<int>(k) * kRouteGap;
        body += line(kStepWidth, from_y, route, from_y);
        body += line(route, from_y, route, to_y);
        body += line(route, to_y, kStepWidth + 6, to_y);
        body += arrow(kStepWidth + 2, to_y);
        body += text(route + 6, (from_y + to_y) / 2,
                     transition.guard ? plc::format_net_expression(*transition.guard)
                                      : "always");
    }

    return "<svg class=\"sfc\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " +
           std::to_string(width) + " " + std::to_string(std::max(height, 1)) +
           "\" width=\"" + std::to_string(width) + "\" height=\"" +
           std::to_string(std::max(height, 1)) + "\">" + body + "</svg>";
}

}  // namespace sc::ui
