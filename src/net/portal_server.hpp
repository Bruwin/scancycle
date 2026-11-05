#pragma once
// Teacher portal request routing. Transport (sockets, TLS, auth) is a separate
// concern and lands in M5; this layer is pure request-to-response mapping so the
// classroom surface is testable without opening a port.
#include "core/types.hpp"

#include <functional>
#include <string>
#include <vector>

namespace sc::lesson {
struct Lesson;
}

namespace sc::net {

enum class HttpMethod : u8 { Get = 0, Post };

struct Request {
    HttpMethod method = HttpMethod::Get;
    std::string path;
    std::string body;
    // Everything after the first '?', still percent-encoded. Routing ignores it;
    // a page that knows its field names decodes what it needs.
    std::string query;
};

struct Response {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
};

class PortalRouter {
public:
    using Handler = std::function<Response(const Request&)>;

    void route(HttpMethod method, std::string path, Handler handler);

    // A family of paths under one prefix, for something like `/grades/<n>` where
    // the valid suffixes depend on data the router does not hold. Exact routes
    // win; among prefixes the longest match wins.
    void route_prefix(HttpMethod method, std::string prefix, Handler handler);

    Response dispatch(const Request& request) const;
    std::size_t route_count() const { return routes_.size() + prefix_routes_.size(); }

private:
    struct Route {
        HttpMethod method = HttpMethod::Get;
        std::string path;
        Handler handler;
    };
    std::vector<Route> routes_;
    std::vector<Route> prefix_routes_;
};

std::string json_escape(std::string_view text);

// Page escaping, next to the JSON one so the catalog and the grade page
// cannot disagree about what is safe to interpolate.
std::string html_escape(std::string_view text);

// The shell every page shares: title, viewport and the one stylesheet, so the
// catalog, a lesson and a grade look like one product.
std::string page_head(std::string_view title);

// The one lesson form. The catalog renders it empty; the grade page renders it
// with the submission, so neither a program nor the name on it is lost between
// attempts. The student field is optional: an unnamed attempt is still graded.
// `query` is an already-encoded filter placed on the action, so grading again
// from a filtered log returns to that view instead of the whole one.
std::string lesson_form(const std::vector<lesson::Lesson>& lessons,
                        std::string_view selected, std::string_view program,
                        std::string_view student, std::string_view query = {});

PortalRouter make_default_router(const std::vector<lesson::Lesson>& lessons);

}  // namespace sc::net
