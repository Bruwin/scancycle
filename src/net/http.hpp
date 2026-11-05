#pragma once
// HTTP/1.1 message handling for the teacher portal. Text in, text out: the
// socket layer owns every descriptor, so this layer is testable without opening
// a port. Only what the portal actually serves is implemented - a request line,
// a body after the blank line, and a response with a Content-Length.
//
// Query strings are accepted and dropped: the portal routes by path, and a
// browser that appends a cache-buster should still get the catalog.
#include "net/portal_server.hpp"

#include <string>
#include <string_view>

namespace sc::net {

struct ParsedRequest {
    bool ok = false;
    Request request;
};

// Reads one request. A missing request line, an unknown method, a version that
// is not HTTP, or a target that does not start with '/' is malformed, which the
// transport answers with 400 rather than a misleading 404.
ParsedRequest parse_http_request(std::string_view raw);

// Serialises a response with `Connection: close`, because the transport answers
// one request per connection and a hanging client is worse than a reconnect.
std::string format_http_response(const Response& response);

}  // namespace sc::net
