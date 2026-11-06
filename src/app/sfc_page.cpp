#include "app/sfc_page.hpp"

#include "net/http.hpp"
#include "plc/ladder_text.hpp"
#include "ui/sfc_svg.hpp"

#include <cstddef>
#include <string>
#include <string_view>
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

net::Response sfc_page(const SfcSession& session) {
    const Application::WatchSnapshot snapshot = session.snapshot();
    const std::vector<bool> active = session.active_steps();

    std::string page = net::page_head("Scan Cycle SFC");
    page += "<style>"
            "svg.sfc{background:#fff;border:1px solid #ccc;max-width:100%;height:auto;"
            "margin:.4rem 0}"
            "svg.sfc .step{fill:#f7f7f7;stroke:#333;stroke-width:2}"
            "svg.sfc .step.active{fill:#e6f2ff;stroke:#06c}"
            "svg.sfc .wire{stroke:#333;stroke-width:2;fill:none}"
            "svg.sfc .arrow{fill:#333}"
            "svg.sfc text{font:11px monospace;fill:#111}"
            "table{border-collapse:collapse}"
            "td,th{border:1px solid #ddd;padding:.2rem .5rem;text-align:left}"
            "</style>";
    page += "<h1>Scan Cycle SFC</h1>";
    page += "<p class=\"no-print\">Steps are drawn in file order and the active step "
            "is highlighted. Replace the text below to change the chart. The page is "
            "local to this machine.</p>";

    page += "<p>Scan <strong>" + std::to_string(snapshot.scan) + "</strong> · ";
    if (!session.parsed()) {
        page += "<span class=\"fail\">parse error</span>";
    } else if (!session.compiled()) {
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

    if (session.parsed()) {
        page += ui::sfc_chart_svg(session.chart(), {active});

        page += "<h2>Steps</h2><table><thead><tr><th>Step</th><th>State</th>"
                "<th>Actions</th></tr></thead><tbody>";
        for (std::size_t i = 0; i < session.chart().steps.size(); ++i) {
            const plc::SfcStep& step = session.chart().steps[i];
            const bool running = i < active.size() && active[i];
            page += "<tr><td><code>" + net::html_escape(step.name) + "</code>";
            if (i == session.chart().initial) {
                page += " <em>initial</em>";
            }
            page += "</td><td>" + std::string(running ? "active" : "-") + "</td><td>";
            for (std::size_t a = 0; a < step.actions.size(); ++a) {
                if (a != 0) {
                    page += ", ";
                }
                page += "<code>" + net::html_escape(step.actions[a].tag) + "</code>";
            }
            page += "</td></tr>";
        }
        page += "</tbody></table>";

        page += "<h2>Transitions</h2><table><thead><tr><th>From</th><th>To</th>"
                "<th>Guard</th></tr></thead><tbody>";
        for (const plc::SfcTransition& transition : session.chart().transitions) {
            page += "<tr><td><code>" + net::html_escape(transition.from) +
                    "</code></td><td><code>" + net::html_escape(transition.to) +
                    "</code></td><td><code>";
            page += transition.guard
                        ? net::html_escape(plc::format_net_expression(*transition.guard))
                        : std::string("always");
            page += "</code></td></tr>";
        }
        page += "</tbody></table>";
    } else {
        page += "<p class=\"fail\">The chart does not parse, so there is nothing to "
                "draw. Fix it below, or edit the file and reload.</p>";
    }

    page += "<h2>Monitor</h2>";
    page += "<form method=\"post\" action=\"/step\" class=\"no-print\">"
            "<button type=\"submit\"";
    if (!session.compiled() || snapshot.faulted) {
        page += " disabled";
    }
    page += ">Step one scan</button></form>";

    if (!session.compiled()) {
        page += "<p>Compile a chart to see its tags.</p>";
    } else if (snapshot.tags.empty()) {
        page += "<p>This chart drives no tags.</p>";
    } else {
        page += "<table><thead><tr><th>Tag</th><th>Value</th></tr></thead><tbody>";
        for (const Application::Watch& tag : snapshot.tags) {
            page += "<tr><td><code>" + net::html_escape(tag.name) + "</code></td><td>";
            if (tag.present) {
                page += net::html_escape(tag.value.to_string());
            } else {
                page += "-";
            }
            page += "</td></tr>";
        }
        page += "</tbody></table>";
    }

    page += "<details><summary>Chart text</summary><pre>";
    page += net::html_escape(session.text());
    page += "</pre></details>";
    page += "<details><summary>Replace the chart</summary>"
            "<form method=\"post\" action=\"/load\">"
            "<textarea name=\"source\" rows=\"16\" cols=\"80\" spellcheck=\"false\">";
    page += net::html_escape(session.text());
    page += "</textarea><p><button type=\"submit\">Load</button></p></form></details>";
    page += "</main></body></html>";

    net::Response response;
    response.content_type = "text/html; charset=utf-8";
    response.body = std::move(page);
    return response;
}

net::PortalRouter make_sfc_router(SfcSession& session) {
    net::PortalRouter router;

    router.route(net::HttpMethod::Get, "/", [&session](const net::Request&) {
        return sfc_page(session);
    });

    router.route(net::HttpMethod::Post, "/step", [&session](const net::Request&) {
        session.step();
        return see_other("/");
    });

    router.route(net::HttpMethod::Post, "/load", [&session](const net::Request& request) {
        session.load(net::form_field(request.body, "source"));
        return see_other("/");
    });

    return router;
}

}  // namespace sc::app
