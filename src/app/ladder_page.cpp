#include "app/ladder_page.hpp"

#include "net/http.hpp"
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

}  // namespace

net::Response ladder_page(const LadderSession& session) {
    const Application::WatchSnapshot snapshot = session.snapshot();

    std::string page = net::page_head("Scan Cycle ladder");
    page += "<style>"
            "svg.ladder{background:#fff;border:1px solid #ccc;max-width:100%;height:auto;"
            "margin:.4rem 0}"
            "svg.ladder .wire{stroke:#333;stroke-width:2;fill:none}"
            "svg.ladder .bar{stroke:#111;stroke-width:3}"
            "svg.ladder .slash{stroke:#b00;stroke-width:2}"
            "svg.ladder .coil{fill:none;stroke:#111;stroke-width:2}"
            "svg.ladder .block{fill:#f5f5f5;stroke:#333;stroke-width:2}"
            "svg.ladder text{font:11px monospace;fill:#111}"
            "svg.ladder a:hover .bar{stroke:#06c}"
            ".rung{border-top:1px solid #ddd;padding:.4rem 0}"
            ".tools form{display:inline-flex;gap:.25rem;margin:.2rem .6rem .2rem 0}"
            "</style>";
    page += "<h1>Scan Cycle ladder</h1>";
    page += "<p class=\"no-print\">Click a contact to invert it. Use a form to add a "
            "branch, change the coil, or set a rail. The page is local to this machine."
            "</p>";

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
        page += "<h2>Rungs</h2>";
        for (std::size_t i = 0; i < session.source().rungs.size(); ++i) {
            const std::string rung = std::to_string(i);
            page += "<section class=\"rung\"><h3>Rung " + std::to_string(i + 1) + "</h3>";
            page += ui::ladder_rung_svg(session.source().rungs[i], {i, true});
            page += "<div class=\"tools no-print\">";
            page += "<a href=\"/rail?rung=" + rung + "&value=1\">always true</a> ";
            page += "<a href=\"/rail?rung=" + rung + "&value=0\">always false</a> ";
            page += "<a href=\"/remove-rung?rung=" + rung + "\">remove rung</a> ";

            page += "<form method=\"post\" action=\"/add\">";
            page += hidden("rung", i);
            page += "<input name=\"path\" size=\"4\" placeholder=\"path\" "
                    "title=\"branch path, e.g. 0.1\">";
            page += "<input name=\"tag\" size=\"8\" placeholder=\"tag\">";
            page += "<select name=\"kind\"><option value=\"series\">series</option>"
                    "<option value=\"parallel\">parallel</option></select>";
            page += "<label><input type=\"checkbox\" name=\"negated\" value=\"1\">NC</label>";
            page += "<button>Add</button></form>";

            page += "<form method=\"get\" action=\"/remove\">";
            page += hidden("rung", i);
            page += "<input name=\"path\" size=\"4\" placeholder=\"path\">";
            page += "<button>Remove branch</button></form>";

            page += "<form method=\"post\" action=\"/coil\">";
            page += hidden("rung", i);
            page += "<input type=\"number\" name=\"index\" value=\"0\" min=\"0\" "
                    "style=\"width:4rem\">";
            page += "<input name=\"tag\" size=\"8\" placeholder=\"coil\">";
            page += "<label><input type=\"checkbox\" name=\"negated\" value=\"1\">NC</label>";
            page += "<button>Set coil</button></form>";
            page += "</div></section>";
        }

        page += "<h2>Add a rung</h2><form method=\"post\" action=\"/rung\">"
                "<input name=\"coil\" placeholder=\"coil tag\"> "
                "<button>Add rung</button></form>";
    } else {
        page += "<p class=\"fail\">The file does not parse, so there is no drawing to "
                "edit. Fix it below, or edit the file and reload.</p>";
    }

    page += "<h2>Monitor</h2>";
    page += "<form method=\"post\" action=\"/step\" class=\"no-print\">"
            "<button type=\"submit\"";
    if (!session.compiled() || snapshot.faulted) {
        page += " disabled";
    }
    page += ">Step one scan</button></form>";

    if (!session.compiled()) {
        page += "<p>Compile a chart to see its variables.</p>";
    } else if (snapshot.tags.empty()) {
        page += "<p>This chart declares no watchable variables.</p>";
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

    page += "<details><summary>Generated ladder text</summary><pre>";
    page += net::html_escape(session.text());
    page += "</pre></details>";
    page += "<details><summary>Replace the file</summary>"
            "<form method=\"post\" action=\"/load\">"
            "<textarea name=\"source\" rows=\"12\" cols=\"80\" spellcheck=\"false\">";
    page += net::html_escape(session.text());
    page += "</textarea><p><button type=\"submit\">Load</button></p></form></details>";
    page += "</main></body></html>";

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
