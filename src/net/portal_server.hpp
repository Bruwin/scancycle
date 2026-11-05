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
    Response dispatch(const Request& request) const;
    std::size_t route_count() const { return routes_.size(); }

private:
    struct Route {
        HttpMethod method = HttpMethod::Get;
        std::string path;
        Handler handler;
    };
    std::vector<Route> routes_;
};

std::string json_escape(std::string_view text);

PortalRouter make_default_router(const std::vector<lesson::Lesson>& lessons);

}  // namespace sc::net
