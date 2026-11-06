#include "ui/svg_text.hpp"

#include <cstddef>
#include <string>

namespace sc::ui {

std::string svg_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
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

std::string svg_path_text(const plc::NetPath& path) {
    std::string text;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            text.push_back('.');
        }
        text += std::to_string(path[i]);
    }
    return text;
}

std::string duration_label(i64 milliseconds) {
    if (milliseconds > 0 && milliseconds % 60'000 == 0) {
        return std::to_string(milliseconds / 60'000) + "m";
    }
    if (milliseconds > 0 && milliseconds % 1'000 == 0) {
        return std::to_string(milliseconds / 1'000) + "s";
    }
    return std::to_string(milliseconds) + "ms";
}

}  // namespace sc::ui
