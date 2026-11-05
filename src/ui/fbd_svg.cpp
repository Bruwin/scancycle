#include "ui/fbd_svg.hpp"

#include "plc/netlist_edit.hpp"
#include "ui/svg_text.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sc::ui {
namespace {

constexpr int kBoxWidth = 88;
constexpr int kBoxHeight = 34;
constexpr int kStub = 12;       // the wire a box draws past its own edge
constexpr int kBlockWidth = 44;
constexpr int kBlockGap = 18;   // between a junction's inputs and its block
constexpr int kStackGap = 10;   // between stacked inputs
constexpr int kBus = 6;         // where the input bus runs
constexpr int kFlowGap = 26;    // between the network and its sink

// A laid-out sub-network. `out_y` is where its value leaves the right edge and
// `height` is what the parent stacks, so a junction never needs to know what its
// children drew.
struct Box {
    int width = 0;
    int height = 0;
    int out_y = 0;
    std::string body;
};

std::string line(int x1, int y1, int x2, int y2) {
    return "<line class=\"wire\" x1=\"" + std::to_string(x1) + "\" y1=\"" +
           std::to_string(y1) + "\" x2=\"" + std::to_string(x2) + "\" y2=\"" +
           std::to_string(y2) + "\"/>";
}

std::string box_rect(int x, int y, int width, int height) {
    return "<rect class=\"block\" x=\"" + std::to_string(x) + "\" y=\"" +
           std::to_string(y) + "\" width=\"" + std::to_string(width) + "\" height=\"" +
           std::to_string(height) + "\" rx=\"3\"/>";
}

std::string label(int x, int y, std::string_view text) {
    return "<text x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) +
           "\" text-anchor=\"middle\">" + svg_escape(text) + "</text>";
}

std::string translate(int x, int y, const std::string& body) {
    return "<g transform=\"translate(" + std::to_string(x) + "," + std::to_string(y) +
           ")\">" + body + "</g>";
}

std::string bubble(int cx, int cy) {
    return "<circle class=\"neg\" cx=\"" + std::to_string(cx) + "\" cy=\"" +
           std::to_string(cy) + "\" r=\"5\"/>";
}

std::string link(const std::string& href, const std::string& body) {
    return "<a href=\"" + href + "\">" + body + "</a>";
}

// The query separator has to be escaped, or the '&' would end the attribute.
std::string target(const char* action, std::size_t rung, const plc::NetPath& path) {
    return std::string(action) + "?rung=" + std::to_string(rung) + "&amp;path=" +
           svg_path_text(path);
}

// A variable or a rail literal: a box with its value on the wire.
Box value_box(std::string_view text, bool negated) {
    std::string body = box_rect(0, 0, kBoxWidth, kBoxHeight);
    body += label(kBoxWidth / 2, kBoxHeight / 2 + 4, text);
    body += line(kBoxWidth, kBoxHeight / 2, kBoxWidth + kStub, kBoxHeight / 2);
    if (negated) {
        body += bubble(kBoxWidth + kStub / 2, kBoxHeight / 2);
    }
    return {kBoxWidth + kStub, kBoxHeight, kBoxHeight / 2, body};
}

Box layout(const plc::NetNode& node, const plc::NetPath& path, std::size_t rung,
           bool clickable);

Box contact_box(const plc::NetNode& node, const plc::NetPath& path, std::size_t rung,
                bool clickable) {
    Box box = value_box(node.var, node.negated);
    if (clickable) {
        box.body = link(target("/toggle", rung, path), box.body);
    }
    return box;
}

Box junction_box(const plc::NetNode& node, const plc::NetPath& path, std::size_t rung,
                 bool clickable) {
    std::vector<Box> children;
    std::vector<int> tops;
    std::vector<int> centres;
    int height = 0;
    int widest = 0;
    for (std::size_t i = 0; i < node.operands.size(); ++i) {
        plc::NetPath child = path;
        child.push_back(i);
        children.push_back(layout(*node.operands[i], child, rung, clickable));
        tops.push_back(height);
        centres.push_back(height + children.back().out_y);
        widest = std::max(widest, children.back().width);
        height += children.back().height + (i == 0 ? 0 : kStackGap);
    }

    const int pins = static_cast<int>(children.size());
    // Room for one pin per input plus a margin, so a three-input AND still reads
    // as a block rather than a stack of touching wires.
    const int block_height = std::max(46, (pins - 1) * 12 + 24);
    const int block_top = (height - block_height) / 2;
    const int block_x = widest + kBlockGap;
    const int bus = widest + kBus;

    std::string body;
    for (int i = 0; i < pins; ++i) {
        const int pin_y = block_top + (block_height * (i + 1)) / (pins + 1);
        body += translate(0, tops[static_cast<std::size_t>(i)],
                          children[static_cast<std::size_t>(i)].body);
        body += line(children[static_cast<std::size_t>(i)].width,
                     centres[static_cast<std::size_t>(i)], bus,
                     centres[static_cast<std::size_t>(i)]);
        body += line(bus, centres[static_cast<std::size_t>(i)], bus, pin_y);
        body += line(bus, pin_y, block_x, pin_y);
    }

    std::string junction = box_rect(block_x, block_top, kBlockWidth, block_height);
    junction += label(block_x + kBlockWidth / 2, block_top + block_height / 2 + 4,
                      node.kind == plc::NetKind::Series ? "&" : ">=1");
    if (clickable) {
        junction = link(target("/swap", rung, path), junction);
    }
    body += junction;

    const int out_y = block_top + block_height / 2;
    body += line(block_x + kBlockWidth, out_y, block_x + kBlockWidth + kStub, out_y);
    return {block_x + kBlockWidth + kStub, height, out_y, body};
}

Box layout(const plc::NetNode& node, const plc::NetPath& path, std::size_t rung,
           bool clickable) {
    switch (node.kind) {
        case plc::NetKind::Contact:
            return contact_box(node, path, rung, clickable);
        case plc::NetKind::Always:
            return value_box("TRUE", false);
        case plc::NetKind::Series:
        case plc::NetKind::Parallel:
            break;
    }

    if (node.operands.empty()) {
        return value_box(node.kind == plc::NetKind::Series ? "TRUE" : "FALSE", false);
    }
    if (node.operands.size() == 1) {
        return layout(*node.operands.front(), path, rung, clickable);
    }
    return junction_box(node, path, rung, clickable);
}

// A coil is an output assignment, so it is drawn as a box that reads from the
// left. A negated coil puts its bubble on the input wire, where the inversion
// happens.
Box coil_box(const plc::Coil& coil) {
    std::string body = line(0, kBoxHeight / 2, kStub, kBoxHeight / 2);
    body += box_rect(kStub, 0, kBoxWidth, kBoxHeight);
    body += label(kStub + kBoxWidth / 2, kBoxHeight / 2 + 4, coil.var);
    if (coil.negated) {
        body += bubble(kStub / 2, kBoxHeight / 2);
    }
    return {kStub + kBoxWidth, kBoxHeight, kBoxHeight / 2, body};
}

Box coils_box(const std::vector<plc::Coil>& coils) {
    if (coils.empty()) {
        return value_box("?", false);
    }
    if (coils.size() == 1) {
        return coil_box(coils.front());
    }

    std::vector<Box> children;
    std::vector<int> tops;
    std::vector<int> centres;
    int height = 0;
    for (std::size_t i = 0; i < coils.size(); ++i) {
        children.push_back(coil_box(coils[i]));
        tops.push_back(height);
        centres.push_back(height + kBoxHeight / 2);
        height += kBoxHeight + (i == 0 ? 0 : kStackGap);
    }

    Box box{coil_box(coils.front()).width, height, height / 2, {}};
    for (std::size_t i = 0; i < children.size(); ++i) {
        box.body += translate(0, tops[i], children[i].body);
    }
    box.body += line(kBus, centres.front(), kBus, centres.back());
    return box;
}

Box timer_box(const plc::TimerSink& timer) {
    const int width = 132;
    std::string body = box_rect(0, 0, width, 40);
    body += label(width / 2, 25, std::string(timer.off_delay ? "TOF " : "TON ") +
                                   duration_label(timer.preset_ms));
    body += label(width / 2, 56, timer.var);
    return {width, 60, 20, body};
}

Box counter_box(const plc::CounterSink& counter) {
    const int width = 140;
    std::string body = box_rect(0, 0, width, 40);
    body += label(width / 2, 25, std::string(counter.down ? "CTD " : "CTU ") +
                                   std::to_string(counter.preset));
    body += label(width / 2, 56, counter.var);
    if (counter.reset) {
        body += label(width / 2, 72, "reset wired");
    }
    return {width, counter.reset ? 80 : 60, 20, body};
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

std::string fbd_rung_svg(const plc::Rung& rung, const FbdSvgOptions& options) {
    const Box condition = rung.condition
                              ? layout(*rung.condition, {}, options.rung, options.clickable)
                              : value_box("TRUE", false);
    const Box sink = sink_box(rung);

    const int height = std::max(condition.height, sink.height);
    const int width = condition.width + kFlowGap + sink.width;
    const int condition_top = (height - condition.height) / 2;
    const int sink_top = (height - sink.height) / 2;
    const int wire_y = condition_top + condition.out_y;

    std::string body;
    body += translate(0, condition_top, condition.body);
    body += translate(condition.width + kFlowGap, sink_top, sink.body);
    body += line(condition.width, wire_y, condition.width + kFlowGap, wire_y);

    return "<svg class=\"fbd\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " +
           std::to_string(width) + " " + std::to_string(height) + "\" width=\"" +
           std::to_string(width) + "\" height=\"" + std::to_string(height) + "\">" +
           body + "</svg>";
}

}  // namespace sc::ui
