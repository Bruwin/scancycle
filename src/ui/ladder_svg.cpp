#include "ui/ladder_svg.hpp"

#include "plc/netlist_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sc::ui {
namespace {

constexpr int kRowHeight = 48;
constexpr int kGap = 12;
constexpr int kBus = 4;

struct Box {
    int width = 0;
    int height = 0;
    std::string body;
};

std::string escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out.push_back(c);
        }
    }
    return out;
}

std::string line(const char* css, int x1, int y1, int x2, int y2) {
    return "<line class=\"" + std::string(css) + "\" x1=\"" + std::to_string(x1) +
           "\" y1=\"" + std::to_string(y1) + "\" x2=\"" + std::to_string(x2) +
           "\" y2=\"" + std::to_string(y2) + "\"/>";
}

std::string translate(int x, int y, const std::string& body) {
    return "<g transform=\"translate(" + std::to_string(x) + "," + std::to_string(y) +
           ")\">" + body + "</g>";
}

std::string path_text(const plc::NetPath& path) {
    std::string text;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            text.push_back('.');
        }
        text += std::to_string(path[i]);
    }
    return text;
}

std::string duration_text(i64 milliseconds) {
    if (milliseconds > 0 && milliseconds % 60'000 == 0) {
        return std::to_string(milliseconds / 60'000) + "m";
    }
    if (milliseconds > 0 && milliseconds % 1'000 == 0) {
        return std::to_string(milliseconds / 1'000) + "s";
    }
    return std::to_string(milliseconds) + "ms";
}

Box rail_box(const char* label) {
    Box box{52, kRowHeight, {}};
    box.body += line("wire", 0, kRowHeight / 2, 52, kRowHeight / 2);
    box.body += "<text x=\"26\" y=\"11\" text-anchor=\"middle\">" + std::string(label) +
                "</text>";
    return box;
}

Box contact_box(const plc::NetNode& node, const plc::NetPath& path, std::size_t rung,
                bool clickable) {
    std::string body;
    body += line("wire", 0, 24, 18, 24);
    body += line("wire", 54, 24, 72, 24);
    body += line("bar", 18, 14, 18, 34);
    body += line("bar", 54, 14, 54, 34);
    if (node.negated) {
        body += line("slash", 18, 34, 54, 14);
    }
    body += "<text x=\"36\" y=\"10\" text-anchor=\"middle\">" + escape(node.var) + "</text>";
    if (clickable) {
        body = "<a href=\"/toggle?rung=" + std::to_string(rung) + "&amp;path=" +
               path_text(path) + "\">" + body + "</a>";
    }
    return {72, kRowHeight, body};
}

Box layout(const plc::NetNode& node, const plc::NetPath& path, std::size_t rung,
           bool clickable);

Box series_box(const plc::NetNode& node, const plc::NetPath& path, std::size_t rung,
               bool clickable) {
    if (node.operands.empty()) {
        return rail_box("TRUE");
    }
    std::vector<Box> children;
    int width = 0;
    int height = 0;
    for (std::size_t i = 0; i < node.operands.size(); ++i) {
        plc::NetPath child_path = path;
        child_path.push_back(i);
        children.push_back(layout(*node.operands[i], child_path, rung, clickable));
        width += children.back().width + (i == 0 ? 0 : kGap);
        height = std::max(height, children.back().height);
    }

    Box box{width, height, {}};
    int x = 0;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const int y = (height - children[i].height) / 2;
        box.body += translate(x, y, children[i].body);
        if (i + 1 < children.size()) {
            box.body += line("wire", x + children[i].width, height / 2,
                             x + children[i].width + kGap, height / 2);
        }
        x += children[i].width + kGap;
    }
    return box;
}

Box parallel_box(const plc::NetNode& node, const plc::NetPath& path, std::size_t rung,
                 bool clickable) {
    if (node.operands.empty()) {
        return rail_box("FALSE");
    }
    if (node.operands.size() == 1) {
        return layout(*node.operands.front(), path, rung, clickable);
    }

    std::vector<Box> children;
    std::vector<int> mids;
    int max_width = 0;
    int height = 0;
    for (std::size_t i = 0; i < node.operands.size(); ++i) {
        plc::NetPath child_path = path;
        child_path.push_back(i);
        children.push_back(layout(*node.operands[i], child_path, rung, clickable));
        max_width = std::max(max_width, children.back().width);
        height += (i == 0 ? 0 : kGap) + children.back().height;
    }

    const int width = max_width + 2 * kGap;
    Box box{width, height, {}};
    int y = 0;
    for (std::size_t i = 0; i < children.size(); ++i) {
        box.body += translate(kGap, y, children[i].body);
        mids.push_back(y + children[i].height / 2);
        y += children[i].height + kGap;
    }

    const int middle = height / 2;
    const int left_bus = kBus;
    const int right_bus = width - kBus;
    box.body += line("wire", 0, middle, left_bus, middle);
    box.body += line("wire", left_bus, mids.front(), left_bus, mids.back());
    box.body += line("wire", right_bus, mids.front(), right_bus, mids.back());
    box.body += line("wire", right_bus, middle, width, middle);
    for (std::size_t i = 0; i < children.size(); ++i) {
        box.body += line("wire", left_bus, mids[i], kGap, mids[i]);
        box.body += line("wire", kGap + children[i].width, mids[i], right_bus, mids[i]);
    }
    return box;
}

Box layout(const plc::NetNode& node, const plc::NetPath& path, std::size_t rung,
           bool clickable) {
    switch (node.kind) {
        case plc::NetKind::Contact:
            return contact_box(node, path, rung, clickable);
        case plc::NetKind::Series:
            return series_box(node, path, rung, clickable);
        case plc::NetKind::Parallel:
            return parallel_box(node, path, rung, clickable);
        case plc::NetKind::Always:
            return rail_box("TRUE");
    }
    return rail_box("FALSE");
}

Box coil_box(const plc::Coil& coil) {
    std::string body;
    body += line("wire", 0, 24, 20, 24);
    body += "<path class=\"coil\" d=\"M20,14 Q8,24 20,34\"/>";
    body += "<path class=\"coil\" d=\"M44,14 Q56,24 44,34\"/>";
    body += line("wire", 44, 24, 72, 24);
    if (coil.negated) {
        body += line("slash", 20, 34, 44, 14);
    }
    body += "<text x=\"32\" y=\"10\" text-anchor=\"middle\">" + escape(coil.var) + "</text>";
    return {72, kRowHeight, body};
}

Box coils_box(const std::vector<plc::Coil>& coils) {
    if (coils.empty()) {
        return rail_box("?");
    }
    if (coils.size() == 1) {
        return coil_box(coils.front());
    }

    std::vector<Box> children;
    int height = 0;
    for (std::size_t i = 0; i < coils.size(); ++i) {
        children.push_back(coil_box(coils[i]));
        height += (i == 0 ? 0 : kGap) + kRowHeight;
    }

    Box box{78, height, {}};
    std::vector<int> mids;
    int y = 0;
    for (std::size_t i = 0; i < children.size(); ++i) {
        box.body += translate(6, y, children[i].body);
        mids.push_back(y + kRowHeight / 2);
        y += kRowHeight + kGap;
    }
    const int middle = height / 2;
    box.body += line("wire", 0, middle, 6, middle);
    box.body += line("wire", 6, mids.front(), 6, mids.back());
    return box;
}

Box timer_box(const plc::TimerSink& timer) {
    std::string body;
    body += line("wire", 0, 30, 10, 30);
    body += "<rect class=\"block\" x=\"10\" y=\"12\" width=\"100\" height=\"36\" rx=\"4\"/>";
    body += "<text x=\"60\" y=\"34\" text-anchor=\"middle\">" +
            std::string(timer.off_delay ? "TOF " : "TON ") +
            duration_text(timer.preset_ms) + "</text>";
    body += line("wire", 110, 30, 150, 30);
    body += "<text x=\"60\" y=\"58\" text-anchor=\"middle\">" + escape(timer.var) + "</text>";
    return {150, 64, body};
}

Box counter_box(const plc::CounterSink& counter) {
    std::string body;
    body += line("wire", 0, 30, 10, 30);
    body += "<rect class=\"block\" x=\"10\" y=\"12\" width=\"130\" height=\"36\" rx=\"4\"/>";
    body += "<text x=\"75\" y=\"34\" text-anchor=\"middle\">" +
            std::string(counter.down ? "CTD " : "CTU ") +
            std::to_string(counter.preset) + "</text>";
    body += line("wire", 140, 30, 180, 30);
    body += "<text x=\"75\" y=\"58\" text-anchor=\"middle\">" + escape(counter.var) + "</text>";
    if (counter.reset) {
        body += "<text x=\"160\" y=\"58\" text-anchor=\"middle\">reset</text>";
    }
    return {180, 64, body};
}

Box sink_box(const plc::Rung& rung) {
    if (rung.timer) {
        return timer_box(*rung.timer);
    }
    if (rung.counter) {
        return counter_box(*rung.counter);
    }
    return coils_box(rung.coils);
}

}  // namespace

std::string ladder_rung_svg(const plc::Rung& rung, const LadderSvgOptions& options) {
    const Box condition = rung.condition
                              ? layout(*rung.condition, {}, options.rung, options.clickable)
                              : rail_box("TRUE");
    const Box sink = sink_box(rung);

    const int gap = 26;
    const int height = std::max(condition.height, sink.height);
    const int width = condition.width + gap + sink.width;

    std::string body;
    body += translate(0, (height - condition.height) / 2, condition.body);
    body += translate(condition.width + gap, (height - sink.height) / 2, sink.body);
    body += line("wire", condition.width, height / 2, condition.width + gap, height / 2);

    return "<svg class=\"ladder\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " +
           std::to_string(width) + " " + std::to_string(height) + "\" width=\"" +
           std::to_string(width) + "\" height=\"" + std::to_string(height) + "\">" +
           body + "</svg>";
}

}  // namespace sc::ui
