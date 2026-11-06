#include "app/sfc_page.hpp"

#include "app/editor_page.hpp"
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

std::string status_badge(const SfcSession& session, bool faulted, bool started) {
    if (!session.parsed()) {
        return "<span class=\"badge bad\">Parse error</span>";
    }
    if (!session.compiled()) {
        return "<span class=\"badge warn\">Not compiled</span>";
    }
    if (faulted) {
        return "<span class=\"badge bad\">Faulted</span>";
    }
    if (!started) {
        return "<span class=\"badge warn\">Not started</span>";
    }
    return "<span class=\"badge ok\">Running</span>";
}

std::string count_label(std::size_t count, std::string_view one, std::string_view many) {
    return std::to_string(count) + " " + std::string(count == 1 ? one : many);
}

}  // namespace

net::Response sfc_page(const SfcSession& session) {
    const Application::WatchSnapshot snapshot = session.snapshot();
    const std::vector<bool> active = session.active_steps();
    const bool parsed = session.parsed();
    const bool started = snapshot.scan > 0;

    std::string page = editor_page_head("Scan Cycle SFC", "sfc");
    page += "<div class=\"page-head\"><div><p class=\"eyebrow\">Sequential control</p>"
            "<h1>Sequential function chart</h1>"
            "<p class=\"lede\">Read the process as states and transitions. The active "
            "step is highlighted after each scan; the chart text remains the source of "
            "truth.</p></div><div class=\"session\">";
    page += status_badge(session, snapshot.faulted, started);
    page += "<span class=\"scan\">Scan <strong>" + std::to_string(snapshot.scan) +
            "</strong></span></div></div>";

    if (!session.error().empty()) {
        page += "<div class=\"alert\"><h2>Error</h2><p>";
        page += net::html_escape(session.error());
        page += "</p></div>";
    }

    if (!session.diagnostics().empty()) {
        page += "<section class=\"panel\"><div class=\"panel-head\"><div>"
                "<p class=\"eyebrow\">Compiler</p><h2>Diagnostics</h2>"
                "<p>Fix these before the chart can run.</p></div></div>"
                "<div class=\"panel-body table-wrap\"><table><thead><tr><th>Line</th>"
                "<th>Column</th><th>Message</th></tr></thead><tbody>";
        for (const plc::Diagnostic& diagnostic : session.diagnostics()) {
            page += "<tr><td>" + std::to_string(diagnostic.line) + "</td><td>" +
                    std::to_string(diagnostic.column) + "</td><td>" +
                    net::html_escape(diagnostic.message) + "</td></tr>";
        }
        page += "</tbody></table></div></section>";
    }

    if (parsed) {
        const std::size_t steps = session.chart().steps.size();
        const std::size_t transitions = session.chart().transitions.size();
        page += "<section class=\"panel\"><div class=\"panel-head\"><div>"
                "<p class=\"eyebrow\">Chart</p><h2>Process flow</h2>"
                "<p>Steps are drawn in file order; every step is a state and every "
                "arrow is a guarded transition.</p></div><span class=\"chip\">";
        page += count_label(steps, "step", "steps");
        page += " · ";
        page += count_label(transitions, "transition", "transitions");
        page += "</span></div><div class=\"panel-body\">";
        if (session.compiled() && !snapshot.faulted && !started) {
            page += "<p class=\"notice\">Not started. The initial step activates on the "
                    "first scan.</p>";
        }
        page += "<div class=\"diagram\">";
        page += ui::sfc_chart_svg(session.chart(), {active});
        page += "</div></div></section>";

        page += "<div class=\"split\"><section class=\"panel\"><div class=\"panel-head\">"
                "<div><p class=\"eyebrow\">States</p><h2>Steps</h2>"
                "<p>Actions run while the step is active.</p></div></div>"
                "<div class=\"panel-body table-wrap\"><table><thead><tr><th>Step</th>"
                "<th>State</th><th>Actions</th></tr></thead><tbody>";
        for (std::size_t i = 0; i < steps; ++i) {
            const plc::SfcStep& step = session.chart().steps[i];
            const bool running = i < active.size() && active[i];
            page += "<tr><td><code>" + net::html_escape(step.name) + "</code>";
            if (i == session.chart().initial) {
                page += " <span class=\"chip\">initial</span>";
            }
            page += "</td><td>";
            page += running ? "<span class=\"badge ok\">Active</span>"
                            : "<span class=\"value false\">-</span>";
            page += "</td><td>";
            for (std::size_t a = 0; a < step.actions.size(); ++a) {
                if (a != 0) {
                    page += ", ";
                }
                page += "<code>" + net::html_escape(step.actions[a].tag) + "</code>";
            }
            page += "</td></tr>";
        }
        page += "</tbody></table></div></section>";

        page += "<section class=\"panel\"><div class=\"panel-head\"><div>"
                "<p class=\"eyebrow\">Rules</p><h2>Transitions</h2>"
                "<p>An arrow is taken when its guard is true.</p></div></div>"
                "<div class=\"panel-body table-wrap\"><table><thead><tr><th>From</th>"
                "<th>To</th><th>Guard</th></tr></thead><tbody>";
        for (const plc::SfcTransition& transition : session.chart().transitions) {
            page += "<tr><td><code>" + net::html_escape(transition.from) +
                    "</code></td><td><code>" + net::html_escape(transition.to) +
                    "</code></td><td><code>";
            page += transition.guard
                        ? net::html_escape(plc::format_net_expression(*transition.guard))
                        : std::string("always");
            page += "</code></td></tr>";
        }
        page += "</tbody></table></div></section></div>";
    } else {
        page += "<div class=\"notice\"><strong>No drawing yet.</strong> The chart does not "
                "parse, so there is nothing to draw. Fix it below, or edit the file and "
                "reload.</div>";
    }

    page += "<section class=\"panel\"><div class=\"panel-head\"><div>"
            "<p class=\"eyebrow\">Execution</p><h2>Monitor</h2>"
            "<p>Values are read after the last scan; a step executes exactly one scan.</p>"
            "</div><form method=\"post\" action=\"/step\"><button class=\"btn primary\" "
            "type=\"submit\"";
    if (!session.compiled() || snapshot.faulted) {
        page += " disabled";
    }
    page += ">Step one scan</button></form></div><div class=\"panel-body\">";

    if (!session.compiled()) {
        page += "<p class=\"notice\">Compile a chart to see its tags.</p>";
    } else if (snapshot.tags.empty()) {
        page += "<p class=\"notice\">This chart drives no tags.</p>";
    } else {
        page += "<div class=\"table-wrap\"><table><thead><tr><th>Tag</th><th>Value</th>"
                "</tr></thead><tbody>";
        for (const Application::Watch& tag : snapshot.tags) {
            page += "<tr><td><code>" + net::html_escape(tag.name) + "</code></td><td>";
            if (tag.present) {
                const std::string value = tag.value.to_string();
                const bool on = value == "TRUE";
                page += "<span class=\"value " + std::string(on ? "true" : "false") +
                        "\">" + net::html_escape(value) + "</span>";
            } else {
                page += "<span class=\"value false\">-</span>";
            }
            page += "</td></tr>";
        }
        page += "</tbody></table></div>";
    }
    page += "</div></section>";

    page += "<details class=\"disclosure\"><summary>Chart text</summary>"
            "<div class=\"panel-body\"><pre>";
    page += net::html_escape(session.text());
    page += "</pre></div></details>";
    page += "<details class=\"disclosure\"><summary>Replace the chart</summary>"
            "<div class=\"panel-body\"><form method=\"post\" action=\"/load\">"
            "<textarea name=\"source\" rows=\"16\" spellcheck=\"false\" "
            "aria-label=\"SFC source\">";
    page += net::html_escape(session.text());
    page += "</textarea><p><button class=\"btn primary\" type=\"submit\">Load</button>"
            "</p></form></div></details>";

    page += editor_page_foot();

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
