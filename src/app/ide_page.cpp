#include "app/ide_page.hpp"

#include "net/http.hpp"
#include "plc/value.hpp"

#include <string>
#include <utility>
#include <vector>

namespace sc::app {
namespace {

net::Response see_other(std::string location) {
    net::Response response;
    response.status = 303;
    response.content_type = "text/plain; charset=utf-8";
    response.body = "See " + location + "\n";
    response.location = std::move(location);
    return response;
}

}  // namespace

net::Response ide_page(const IdeSession& session) {
    const Application::WatchSnapshot snapshot = session.snapshot();
    const std::vector<std::string> unbound = session.unbound_devices();

    std::string page = net::page_head("Scan Cycle IDE");
    page += "<h1>Scan Cycle IDE</h1>";
    page += "<p class=\"no-print\">Edit Structured Text, compile it, then step the scan "
            "cycle one scan at a time. The page is local to this machine.</p>";

    page += "<p>Scan <strong>" + std::to_string(snapshot.scan) + "</strong> · clock <strong>" +
            std::to_string(snapshot.now_ms) + " ms</strong> · ";
    if (!session.compiled()) {
        page += "<span class=\"fail\">not compiled</span>";
    } else if (snapshot.faulted) {
        page += "<span class=\"fail\">faulted</span>";
    } else {
        page += "<span class=\"pass\">ready</span>";
    }
    page += "</p>";

    if (!session.error().empty()) {
        page += "<p class=\"fail\"><strong>Error:</strong> " +
                net::html_escape(session.error()) + "</p>";
    }

    if (!session.diagnostics().empty()) {
        page += "<h2>Diagnostics</h2><table><thead><tr><th>Line</th><th>Column</th>"
                "<th>Message</th></tr></thead><tbody>";
        for (const plc::Diagnostic& diagnostic : session.diagnostics()) {
            page += "<tr><td>" + std::to_string(diagnostic.line) + "</td><td>" +
                    std::to_string(diagnostic.column) + "</td><td>" +
                    net::html_escape(diagnostic.message) + "</td></tr>";
        }
        page += "</tbody></table>";
    }

    page += "<h2>Program</h2>";
    page += "<form method=\"post\" action=\"/apply\">"
            "<textarea name=\"source\" rows=\"24\" wrap=\"off\" spellcheck=\"false\">";
    page += net::html_escape(session.source());
    page += "</textarea><p><button type=\"submit\">Compile &amp; load</button> "
            "<span class=\"no-print\">Replaces the running program and resets the scan "
            "count.</span></p></form>";

    page += "<h2>Monitor</h2>";
    page += "<form method=\"post\" action=\"/step\" class=\"no-print\">"
            "<button type=\"submit\"";
    if (!session.compiled() || snapshot.faulted) {
        page += " disabled";
    }
    page += ">Step one scan</button></form>";

    if (!session.compiled()) {
        page += "<p>Compile a program to see its variables.</p>";
    } else if (snapshot.tags.empty()) {
        page += "<p>This program declares no watchable variables.</p>";
    } else {
        page += "<table><thead><tr><th>Tag</th><th>Type</th><th>Value</th><th>Forced</th>"
                "<th class=\"no-print\">Actions</th></tr></thead><tbody>";
        for (const Application::Watch& tag : snapshot.tags) {
            page += "<tr><td><code>" + net::html_escape(tag.name) + "</code></td><td>";
            if (tag.present) {
                page += net::html_escape(plc::value_type_name(tag.value.type));
            } else {
                page += "-";
            }
            page += "</td><td>";
            if (tag.present) {
                page += net::html_escape(tag.value.to_string());
            } else {
                page += "-";
            }
            page += "</td><td>";
            if (tag.forced) {
                page += "yes";
            }
            page += "</td><td class=\"no-print\">";
            page += "<form method=\"post\" action=\"/force\" "
                    "style=\"display:flex;gap:.3rem;margin:0\">";
            page += "<input type=\"hidden\" name=\"name\" value=\"" +
                    net::html_escape(tag.name) + "\">";
            page += "<button name=\"action\" value=\"true\">TRUE</button>";
            page += "<button name=\"action\" value=\"false\">FALSE</button>";
            page += "<button name=\"action\" value=\"clear\">Clear</button>";
            page += "</form></td></tr>";
        }
        page += "</tbody></table>";
    }

    page += "<p class=\"no-print\">Forcing an input changes what the program sees. "
            "Forcing an output changes what the field device gets. The watch value stays "
            "the program's own, so the logic and the force can disagree on purpose.</p>";

    if (!unbound.empty()) {
        page += "<h2>Unbound devices</h2><p>These plant devices are not named by the "
                "program:</p><ul>";
        for (const std::string& name : unbound) {
            page += "<li><code>" + net::html_escape(name) + "</code></li>";
        }
        page += "</ul>";
    }

    page += "</main></body></html>";

    net::Response response;
    response.content_type = "text/html; charset=utf-8";
    response.body = std::move(page);
    return response;
}

net::PortalRouter make_ide_router(IdeSession& session) {
    net::PortalRouter router;

    router.route(net::HttpMethod::Get, "/", [&session](const net::Request&) {
        return ide_page(session);
    });

    router.route(net::HttpMethod::Get, "/api/health", [](const net::Request&) {
        net::Response response;
        response.body = R"({"status":"ok","service":"scancycle-ide"})";
        return response;
    });

    router.route(net::HttpMethod::Post, "/apply", [&session](const net::Request& request) {
        session.compile(net::form_field(request.body, "source"));
        return see_other("/");
    });

    router.route(net::HttpMethod::Post, "/step", [&session](const net::Request&) {
        session.step();
        return see_other("/");
    });

    router.route(net::HttpMethod::Post, "/force", [&session](const net::Request& request) {
        const std::string name = net::form_field(request.body, "name");
        const std::string action = net::form_field(request.body, "action");
        if (action == "true") {
            session.set_force(name, true);
        } else if (action == "false") {
            session.set_force(name, false);
        } else if (action == "clear") {
            session.clear_force(name);
        }
        return see_other("/");
    });

    return router;
}

}  // namespace sc::app
