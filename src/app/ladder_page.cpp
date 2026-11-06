#include "app/ladder_page.hpp"

#include "app/editor_page.hpp"
#include "net/http.hpp"
#include "ui/fbd_svg.hpp"
#include "ui/ladder_svg.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace sc::app {
namespace {

constexpr std::size_t kMissing = static_cast<std::size_t>(-1);

net::Response see_other(std::string location) {
    net::Response response;
    response.status = 303;
    response.content_type = "text/plain; charset=utf-8";
    response.body = "See " + location + "\n";
    response.location = std::move(location);
    return response;
}

net::Response bad_request(std::string message) {
    net::Response response;
    response.status = 400;
    response.content_type = "text/plain; charset=utf-8";
    response.body = std::move(message) + "\n";
    return response;
}

std::size_t parse_index(std::string_view text, std::size_t fallback) {
    if (text.empty() || text.size() > 9) {
        return fallback;
    }
    std::size_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return fallback;
        }
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    return value;
}

bool parse_path(std::string_view text, plc::NetPath& path) {
    path.clear();
    if (text.empty()) {
        return true;
    }
    std::size_t begin = 0;
    while (true) {
        const std::size_t dot = text.find('.', begin);
        const std::size_t end = dot == std::string_view::npos ? text.size() : dot;
        const std::string_view piece = text.substr(begin, end - begin);
        if (piece.empty() || piece.size() > 9) {
            return false;
        }
        std::size_t index = 0;
        for (const char c : piece) {
            if (c < '0' || c > '9') {
                return false;
            }
            index = index * 10 + static_cast<std::size_t>(c - '0');
        }
        path.push_back(index);
        if (dot == std::string_view::npos) {
            break;
        }
        begin = dot + 1;
    }
    return true;
}

std::string hidden(const char* name, std::size_t value) {
    return "<input type=\"hidden\" name=\"" + std::string(name) + "\" value=\"" +
           std::to_string(value) + "\">";
}

std::string two_digits(std::size_t value) {
    return (value < 9 ? "0" : "") + std::to_string(value + 1);
}

std::string status_badge(const LadderSession& session, bool faulted) {
    if (!session.parsed()) {
        return "<span class=\"badge bad\">Parse error</span>";
    }
    if (!session.compiled()) {
        return "<span class=\"badge warn\">Not compiled</span>";
    }
    if (faulted) {
        return "<span class=\"badge bad\">Faulted</span>";
    }
    return "<span class=\"badge ok\">Ready</span>";
}

std::string rung_editor(std::size_t index) {
    const std::string rung = std::to_string(index);
    std::string tools = "<div class=\"toolbar\">";
    tools += "<a class=\"btn subtle\" href=\"/rail?rung=" + rung +
             "&value=1\" title=\"Replace this rung's condition with a true rail\">Set true</a>";
    tools += "<a class=\"btn subtle\" href=\"/rail?rung=" + rung +
             "&value=0\" title=\"Replace this rung's condition with a false rail\">Set false</a>";
    tools += "<a class=\"btn danger\" href=\"/remove-rung?rung=" + rung +
             "\">Remove rung</a>";
    tools += "<details class=\"editor-tools\"><summary>Edit rung</summary>";
    tools += "<div class=\"edit-grid\">";

    tools += "<form class=\"tool-form\" method=\"post\" action=\"/add\">";
    tools += "<h4>Add branch</h4>";
    tools += hidden("rung", index);
    tools += "<div class=\"row\"><label>Path <input name=\"path\" size=\"4\" "
             "placeholder=\"0.1\" title=\"branch path, e.g. 0.1\"></label>";
    tools += "<label>Tag <input name=\"tag\" size=\"8\" placeholder=\"tag\"></label></div>";
    tools += "<div class=\"row\"><label>Kind <select name=\"kind\">"
             "<option value=\"series\">series</option>"
             "<option value=\"parallel\">parallel</option></select></label>";
    tools += "<label><input type=\"checkbox\" name=\"negated\" value=\"1\"> NC</label></div>";
    tools += "<button class=\"btn primary\" type=\"submit\">Add branch</button></form>";

    tools += "<form class=\"tool-form\" method=\"get\" action=\"/remove\">";
    tools += "<h4>Remove branch</h4>";
    tools += hidden("rung", index);
    tools += "<div class=\"row\"><label>Path <input name=\"path\" size=\"4\" "
             "placeholder=\"0.1\"></label></div>";
    tools += "<button class=\"btn\" type=\"submit\">Remove branch</button></form>";

    tools += "<form class=\"tool-form\" method=\"post\" action=\"/coil\">";
    tools += "<h4>Set coil</h4>";
    tools += hidden("rung", index);
    tools += "<div class=\"row\"><label>Index <input type=\"number\" name=\"index\" "
             "value=\"0\" min=\"0\"></label>";
    tools += "<label>Coil <input name=\"tag\" size=\"8\" placeholder=\"coil\"></label>";
    tools += "<label><input type=\"checkbox\" name=\"negated\" value=\"1\"> NC</label></div>";
    tools += "<button class=\"btn\" type=\"submit\">Set coil</button></form>";

    tools += "</div></details></div>";
    return tools;
}

}  // namespace

net::Response ladder_page(const LadderSession& session) {
    const Application::WatchSnapshot snapshot = session.snapshot();
    const bool fbd = session.view() == ChartView::Fbd;
    const char* title = fbd ? "Scan Cycle FBD" : "Scan Cycle ladder";
    const char* eyebrow = "Graphical logic";
    const char* heading = fbd ? "Function block diagram" : "Ladder editor";
    const char* lede =
        fbd ? "The same control network, drawn as function blocks. Click a junction to "
              "swap AND for OR. Keyboard: Tab to a block or junction, then press Enter "
              "to toggle it."
            : "Inspect and edit the control logic one rung at a time. Click a contact to "
              "invert it, or a junction to swap AND for OR. Keyboard: Tab to a contact "
              "or junction, then press Enter to toggle it.";

    std::string page = editor_page_head(title, fbd ? "fbd" : "ladder");
    page += "<div class=\"page-head\"><div><p class=\"eyebrow\">";
    page += eyebrow;
    page += "</p><h1>";
    page += heading;
    page += "</h1><p class=\"lede\">";
    page += lede;
    page += "</p></div><div class=\"session\">";
    page += status_badge(session, snapshot.faulted);
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

    if (session.parsed()) {
        const std::size_t count = session.source().rungs.size();
        page += "<section class=\"panel\"><div class=\"panel-head\"><div>"
                "<p class=\"eyebrow\">Source</p><h2>";
        page += fbd ? "Networks" : "Rungs";
        page += "</h2><p>";
        page += fbd ? "Each network is one rung lowered to function blocks."
                    : "Each card is one line of the ladder source.";
        page += "</p></div><span class=\"chip\">" + std::to_string(count) + " ";
        page += fbd ? (count == 1 ? "network" : "networks")
                    : (count == 1 ? "rung" : "rungs");
        page += "</span></div><div class=\"panel-body\"><div class=\"rung-list\">";

        for (std::size_t i = 0; i < count; ++i) {
            const std::string rung = std::to_string(i);
            page += "<article class=\"rung-card\"><div class=\"rung-head\">";
            page += "<span class=\"rung-index\">" + two_digits(i) + "</span><div><h3>";
            page += fbd ? "Network " : "Rung ";
            page += std::to_string(i + 1);
            page += "</h3><p>";
            page += fbd ? "Click a block to invert a signal or change the junction."
                        : "Click a contact to invert it; click a junction to change it.";
            page += "</p></div></div><div class=\"diagram\">";
            if (fbd) {
                page += ui::fbd_rung_svg(session.source().rungs[i], {i, true});
            } else {
                page += ui::ladder_rung_svg(session.source().rungs[i], {i, true});
            }
            page += "</div>";
            page += rung_editor(i);
            page += "</article>";
        }

        page += "</div><div class=\"add-rung\"><div><h3>Add a rung</h3>"
                "<p>Give the new coil a tag. The rung starts with a true rail.</p></div>";
        page += "<form method=\"post\" action=\"/rung\"><input name=\"coil\" "
                "placeholder=\"coil tag\" aria-label=\"New coil tag\">"
                "<button class=\"btn primary\" type=\"submit\">Add rung</button></form>";
        page += "</div></div></section>";
    } else {
        page += "<div class=\"notice\"><strong>No drawing yet.</strong> The file does not "
                "parse, so there is nothing to edit. Fix it below, or edit the file and "
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
        page += "<p class=\"notice\">Compile a chart to see its variables.</p>";
    } else if (snapshot.tags.empty()) {
        page += "<p class=\"notice\">This chart declares no watchable variables.</p>";
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

    page += "<details class=\"disclosure\"><summary>Generated ladder text</summary>"
            "<div class=\"panel-body\"><pre>";
    page += net::html_escape(session.text());
    page += "</pre></div></details>";
    page += "<details class=\"disclosure\"><summary>Replace the file</summary>"
            "<div class=\"panel-body\"><form method=\"post\" action=\"/load\">"
            "<textarea name=\"source\" rows=\"14\" spellcheck=\"false\" "
            "aria-label=\"Ladder source\">";
    page += net::html_escape(session.text());
    page += "</textarea><p><button class=\"btn primary\" type=\"submit\">Load</button>"
            "</p></form></div></details>";

    page += editor_page_foot();

    net::Response response;
    response.content_type = "text/html; charset=utf-8";
    response.body = std::move(page);
    return response;
}

net::PortalRouter make_ladder_router(LadderSession& session) {
    net::PortalRouter router;

    router.route(net::HttpMethod::Get, "/", [&session](const net::Request&) {
        return ladder_page(session);
    });

    router.route(net::HttpMethod::Post, "/step", [&session](const net::Request&) {
        session.step();
        return see_other("/");
    });

    router.route(net::HttpMethod::Post, "/load", [&session](const net::Request& request) {
        session.load(net::form_field(request.body, "source"));
        return see_other("/");
    });

    router.route(net::HttpMethod::Get, "/toggle", [&session](const net::Request& request) {
        const std::size_t rung =
            parse_index(net::form_field(request.query, "rung"), kMissing);
        plc::NetPath path;
        if (rung == kMissing ||
            !parse_path(net::form_field(request.query, "path"), path)) {
            return bad_request("bad rung or path");
        }
        if (!session.edit(rung, [&](plc::Rung& one) {
                return plc::toggle_negated(one, path);
            })) {
            return bad_request(session.error());
        }
        return see_other("/");
    });

    router.route(net::HttpMethod::Get, "/view", [&session](const net::Request& request) {
        const std::string mode = net::form_field(request.query, "mode");
        if (mode == "fbd") {
            session.set_view(ChartView::Fbd);
        } else if (mode == "ladder") {
            session.set_view(ChartView::Ladder);
        } else {
            return bad_request("mode must be ladder or fbd");
        }
        return see_other("/");
    });

    router.route(net::HttpMethod::Get, "/swap", [&session](const net::Request& request) {
        const std::size_t rung =
            parse_index(net::form_field(request.query, "rung"), kMissing);
        plc::NetPath path;
        if (rung == kMissing ||
            !parse_path(net::form_field(request.query, "path"), path)) {
            return bad_request("bad rung or path");
        }
        if (!session.edit(rung, [&](plc::Rung& one) {
                return plc::toggle_junction(one, path);
            })) {
            return bad_request(session.error());
        }
        return see_other("/");
    });

    router.route(net::HttpMethod::Get, "/rail", [&session](const net::Request& request) {
        const std::size_t rung =
            parse_index(net::form_field(request.query, "rung"), kMissing);
        if (rung == kMissing) {
            return bad_request("bad rung");
        }
        const std::string value = net::form_field(request.query, "value");
        const bool rail = value == "1" || value == "true";
        if (!session.edit(rung, [&](plc::Rung& one) {
                return plc::set_rail(one, rail);
            })) {
            return bad_request(session.error());
        }
        return see_other("/");
    });

    router.route(net::HttpMethod::Get, "/remove", [&session](const net::Request& request) {
        const std::size_t rung =
            parse_index(net::form_field(request.query, "rung"), kMissing);
        plc::NetPath path;
        if (rung == kMissing ||
            !parse_path(net::form_field(request.query, "path"), path)) {
            return bad_request("bad rung or path");
        }
        if (!session.edit(rung, [&](plc::Rung& one) {
                return plc::remove_node(one, path);
            })) {
            return bad_request(session.error());
        }
        return see_other("/");
    });

    router.route(net::HttpMethod::Get, "/remove-rung",
                 [&session](const net::Request& request) {
                     const std::size_t rung =
                         parse_index(net::form_field(request.query, "rung"), kMissing);
                     if (rung == kMissing) {
                         return bad_request("bad rung");
                     }
                     if (!session.remove_rung(rung)) {
                         return bad_request(session.error());
                     }
                     return see_other("/");
                 });

    router.route(net::HttpMethod::Post, "/add", [&session](const net::Request& request) {
        const std::size_t rung =
            parse_index(net::form_field(request.body, "rung"), kMissing);
        plc::NetPath path;
        if (rung == kMissing ||
            !parse_path(net::form_field(request.body, "path"), path)) {
            return bad_request("bad rung or path");
        }
        const std::string kind = net::form_field(request.body, "kind");
        if (kind != "series" && kind != "parallel") {
            return bad_request("kind must be series or parallel");
        }
        const std::string tag = net::form_field(request.body, "tag");
        const bool negated = !net::form_field(request.body, "negated").empty();
        const bool ok = session.edit(rung, [&](plc::Rung& one) {
            if (kind == "series") {
                return plc::add_series(one, path, tag, negated);
            }
            return plc::add_parallel(one, path, tag, negated);
        });
        if (!ok) {
            return bad_request(session.error());
        }
        return see_other("/");
    });

    router.route(net::HttpMethod::Post, "/coil", [&session](const net::Request& request) {
        const std::size_t rung =
            parse_index(net::form_field(request.body, "rung"), kMissing);
        const std::size_t index =
            parse_index(net::form_field(request.body, "index"), 0);
        if (rung == kMissing) {
            return bad_request("bad rung");
        }
        const std::string tag = net::form_field(request.body, "tag");
        const bool negated = !net::form_field(request.body, "negated").empty();
        if (!session.edit(rung, [&](plc::Rung& one) {
                return plc::set_coil(one, index, tag, negated);
            })) {
            return bad_request(session.error());
        }
        return see_other("/");
    });

    router.route(net::HttpMethod::Post, "/rung", [&session](const net::Request& request) {
        if (!session.add_rung(net::form_field(request.body, "coil"))) {
            return bad_request(session.error());
        }
        return see_other("/");
    });

    return router;
}

}  // namespace sc::app
