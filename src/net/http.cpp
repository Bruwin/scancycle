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
