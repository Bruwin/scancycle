#include "net/http.hpp"

#include <string>

namespace sc::net {
namespace {

const char* status_text(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 500:
            return "Internal Server Error";
        default:
            return "Unknown";
    }
}

int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

std::string url_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char character = text[i];
        if (character == '+') {
            out += ' ';
        } else if (character == '%' && i + 2 < text.size() && hex_value(text[i + 1]) >= 0 &&
                   hex_value(text[i + 2]) >= 0) {
            out += static_cast<char>(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2]));
            i += 2;
        } else {
            out += character;
        }
    }
    return out;
}

}  // namespace

ParsedRequest parse_http_request(std::string_view raw) {
    ParsedRequest out;

    const std::size_t line_end = raw.find("\r\n");
    if (line_end == std::string_view::npos) {
        return out;
    }
    const std::string_view line = raw.substr(0, line_end);

    const std::size_t first_space = line.find(' ');
    if (first_space == std::string_view::npos) {
        return out;
    }
    const std::size_t second_space = line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos) {
        return out;
    }

    const std::string_view method = line.substr(0, first_space);
    std::string_view target = line.substr(first_space + 1, second_space - first_space - 1);
    const std::string_view version = line.substr(second_space + 1);

    if (version.rfind("HTTP/", 0) != 0) {
        return out;
    }
    if (method == "GET") {
        out.request.method = HttpMethod::Get;
    } else if (method == "POST") {
        out.request.method = HttpMethod::Post;
    } else {
        return out;
    }
    if (target.empty() || target.front() != '/') {
        return out;
    }

    const std::size_t query = target.find('?');
    if (query != std::string_view::npos) {
        target = target.substr(0, query);
    }
    out.request.path = std::string(target);

    const std::size_t body = raw.find("\r\n\r\n");
    if (body != std::string_view::npos) {
        out.request.body = std::string(raw.substr(body + 4));
    }

    out.ok = true;
    return out;
}

std::string format_http_response(const Response& response) {
    std::string text = "HTTP/1.1 ";
    text += std::to_string(response.status);
    text += ' ';
    text += status_text(response.status);
    text += "\r\nContent-Type: ";
    text += response.content_type;
    text += "\r\nContent-Length: ";
    text += std::to_string(response.body.size());
    text += "\r\nConnection: close\r\n\r\n";
    text += response.body;
    return text;
}

}  // namespace sc::net

namespace sc::net {

std::string form_field(std::string_view body, std::string_view name) {
    std::size_t pos = 0;
    while (pos <= body.size()) {
        const std::size_t ampersand = body.find('&', pos);
        const std::string_view pair = body.substr(
            pos, ampersand == std::string_view::npos ? std::string_view::npos : ampersand - pos);
        const std::size_t equals = pair.find('=');
        if (equals != std::string_view::npos && pair.substr(0, equals) == name) {
            return url_decode(pair.substr(equals + 1));
        }
        if (ampersand == std::string_view::npos) {
            break;
        }
        pos = ampersand + 1;
    }
    return std::string();
}

}  // namespace sc::net
