#include "app/shell_page.hpp"

#include "app/shell_scene.hpp"
#include "app/shell_script.hpp"
#include "app/shell_style.hpp"
#include "net/portal_server.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace sc::app {
namespace {

using net::HttpMethod;
using net::PortalRouter;
using net::Request;
using net::Response;

std::string nav_link(std::string_view href, std::string_view label, bool active) {
    std::string link = "<a class=\"nav-link";
    if (active) {
        link += " active";
    }
    link += "\" href=\"";
    link += net::html_escape(href);
    link += "\"";
    if (active) {
        link += " aria-current=\"page\"";
    }
    link += ">";
    link += label;
    link += "</a>";
    return link;
}

std::string shell_topbar(const ShellOptions& options, std::string_view active) {
    std::string bar = "<header class=\"topbar\">";
    bar += R"(<a class="brand" href="/" aria-label="Scan Cycle home">
      <span class="mark mark-small" aria-hidden="true">SC</span>
      <span class="brand-copy"><strong>Scan Cycle</strong><small>1.0 · mechatronics lab</small></span>
    </a>)";
    bar += "<nav class=\"nav\" aria-label=\"Main navigation\">";
    bar += nav_link("/factory", "Factory", active == "factory");
    bar += nav_link(options.ide_url, "PLC IDE", false);
    bar += nav_link(options.free_play_url, "Free Play", false);
    if (options.teacher_mode) {
        bar += nav_link(options.portal_url, "Teacher portal", false);
    }
    bar += "</nav>";
    bar += "<span class=\"local\">";
    bar += options.teacher_mode ? "Teacher mode" : "Local session";
    bar += "</span></header>";
    return bar;
}

std::string shell_head(const ShellOptions& options, std::string_view title,
                       std::string_view active) {
    std::string page;
    page.reserve(12000);
    page += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
    page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">";
    page += "<link rel=\"icon\" href=\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='14' fill='%23f3b642'/%3E%3Ctext x='32' y='42' font-size='24' font-family='sans-serif' font-weight='900' text-anchor='middle' fill='%231a1000'%3ESC%3C/text%3E%3C/svg%3E\">";
    page += "<title>";
    page += net::html_escape(title);
    page += "</title><style>";
    page += shell_style();
    page += "</style></head><body><div class=\"app\">";
    page += shell_topbar(options, active);
    return page;
}

std::string shell_close() {
    return "</div></body></html>";
}

std::string with_script(std::string page, std::string_view script) {
    page += "<script>";
    page += script;
    page += "</script></div></body></html>";
    return page;
}

Response html_response(std::string body, int status = 200) {
    Response response;
    response.status = status;
    response.content_type = "text/html; charset=utf-8";
    response.body = std::move(body);
    return response;
}

std::string intro_page(const ShellOptions& options) {
    std::string page = shell_head(options, "Scan Cycle 1.0", "");
    page += R"(<main class="main intro">
  <section class="hero-copy">
    <div class="logo-lockup">
      <div class="logo-glow"><span class="mark mark-large" aria-hidden="true">SC</span></div>
    </div>
    <p class="eyebrow">Mechatronics lab · 1.0</p>
    <h1>Scan Cycle <span class="version">1.0</span></h1>
    <p class="lede">Program the PLC. Watch the cell. Learn the scan.</p>
    <div class="hero-note">
      <span class="chip">Local simulation</span>
      <span>No cloud account. No setup.</span>
    </div>
  </section>
  <nav class="menu" aria-label="Start menu">
    <a class="menu-card" href="/factory?new=1" autofocus>
      <span class="menu-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></svg></span>
      <span class="menu-text"><strong>New session</strong><small>Open the press-cell factory</small></span>
      <span class="menu-arrow" aria-hidden="true">→</span>
    </a>
    <a class="menu-card" href="/sessions">
      <span class="menu-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M5 5h14v14H5z"/><path d="M9 9h6M9 13h6M9 17h3"/></svg></span>
      <span class="menu-text"><strong>Load session</strong><small>Choose a sample cell scenario</small></span>
      <span class="menu-arrow" aria-hidden="true">→</span>
    </a>)";
    page += "<a class=\"menu-card\" href=\"";
    page += net::html_escape(options.free_play_url);
    page += R"(">
      <span class="menu-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg></span>
      <span class="menu-text"><strong>Free Play</strong><small>Experiment without grades</small></span>
      <span class="menu-arrow" aria-hidden="true">→</span>
    </a>)";
    page += R"(    <a class="menu-card" href="/settings">
      <span class="menu-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z"/><path d="m4.9 4.9 2.2 2.2M16.9 16.9l2.2 2.2M4.9 19.1l2.2-2.2M16.9 7.1l2.2-2.2M3 12h3M18 12h3M12 3v3M12 18v3"/></svg></span>
      <span class="menu-text"><strong>Settings</strong><small>Grid, motion and this browser</small></span>
      <span class="menu-arrow" aria-hidden="true">→</span>
    </a>)";
    if (options.teacher_mode) {
        page += "<a class=\"menu-card teacher-card\" href=\"";
        page += net::html_escape(options.portal_url);
        page += R"(">
      <span class="menu-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M4 6h16v12H4z"/><path d="M8 10h8M8 14h5"/></svg></span>
      <span class="menu-text"><strong>Teacher portal</strong><small>Open the class log and grading desk</small></span>
      <span class="menu-arrow" aria-hidden="true">→</span>
    </a>)";
    }
    page += R"(  </nav>
</main>
<footer class="foot">
  <span>Scan Cycle 1.0 · local build</span>
  <span>Student mode · teacher surfaces hidden</span>
</footer>)";
    page += shell_close();
    return page;
}

std::string factory_page(const ShellOptions& options) {
    std::string page = shell_head(options, "Factory · Scan Cycle", "factory");
    page += R"(<main class="main factory-main">
  <div class="page-head factory-head">
    <div>
      <p class="eyebrow">Factory / procedural cell</p>
      <h1>Press cell</h1>
      <p class="lede">The same kit layout the native viewer builds, projected here for inspection.</p>
    </div>
    <a class="btn ghost" href="/">Back to menu</a>
  </div>
  <section class="factory-stage" data-factory-stage aria-label="Interactive 3D factory preview">
    <canvas id="factory-canvas" data-factory-canvas aria-label="Drag to orbit, right-drag to pan, scroll to zoom"></canvas>
    <div class="factory-overlay">
      <strong>FACTORY PREVIEW</strong>
      <p>Press cell · 10 industrial kit classes. Drag to orbit. Right-drag to pan. Scroll to zoom.</p>
    </div>
    <div class="factory-legend" aria-hidden="true">
      <span><i class="swatch" style="background:#f3b642"></i> safety</span>
      <span><i class="swatch" style="background:#2f6f9f"></i> machine</span>
      <span><i class="swatch" style="background:#2f9f70"></i> conveyor</span>
      <span><i class="swatch" style="background:#748596"></i> steel</span>
    </div>
    <div class="factory-controls" data-factory-controls>
      <button class="btn small" type="button" data-factory-view="reset">Reset view</button>
      <button class="btn small" type="button" data-factory-view="top">Top</button>
      <button class="btn small" type="button" data-factory-view="cell">Cell</button>
      <span class="factory-hint">Drag to orbit · Right-drag to pan · Scroll to zoom</span>
    </div>
    <div class="factory-state" id="factory-status" aria-live="polite">Loading scene…</div>
  </section>
  <div class="factory-footer">
    <span>Preview scene generated from <code>kFactoryKitLayout</code>.</span>
    <a href=")";
    page += net::html_escape(options.ide_url);
    page += R"(">Open PLC IDE →</a>
  </div>
</main>)";
    return with_script(std::move(page), shell_factory_script());
}

std::string sessions_page(const ShellOptions& options) {
    std::string page = shell_head(options, "Load session · Scan Cycle", "");
    page += R"(<main class="main">
  <div class="page-head">
    <div>
      <p class="eyebrow">Load / sample scenarios</p>
      <h1>Choose a session</h1>
      <p class="lede">Sample cells for a quick look. Open the PLC IDE for the editable program.</p>
    </div>
    <div class="page-actions">
      <a class="btn ghost" href="/">Back to menu</a>
      <a class="btn primary" href="/factory?new=1">New session</a>
    </div>
  </div>
  <div class="session-grid">
    <a class="session-card" href="/factory?focus=press">
      <span class="chip gold">Press cell</span>
      <h2>Press batch</h2>
      <p>Photo-eye, press and outfeed. The press cell used by the first lesson.</p>
      <span class="open">Open preview →</span>
    </a>
    <a class="session-card" href="/factory?focus=conveyor">
      <span class="chip">Conveyor</span>
      <h2>Conveyor interlock</h2>
      <p>Start, stop and safety interlock around the transfer conveyor.</p>
      <span class="open">Open preview →</span>
    </a>
    <a class="session-card" href="/factory?focus=pallet">
      <span class="chip">Pallet</span>
      <h2>Batch count</h2>
      <p>Count parts into a pallet load and release when the batch is complete.</p>
      <span class="open">Open preview →</span>
    </a>
  </div>
  <div class="session-actions">
    <a class="btn primary" href="/factory?new=1">Start a new session</a>
    <a class="btn" href=")";
    page += net::html_escape(options.ide_url);
    page += R"(">Open PLC IDE</a>
    <span class="settings-note">The shell previews the cell; the IDE owns the program.</span>
  </div>
</main>)";
    page += shell_close();
    return page;
}

std::string settings_page(const ShellOptions& options) {
    std::string page = shell_head(options, "Settings · Scan Cycle", "");
    page += R"(<main class="main">
  <div class="page-head">
    <div>
      <p class="eyebrow">Settings / local browser</p>
      <h1>Settings</h1>
      <p class="lede">Display preferences are stored in this browser only.</p>
    </div>
    <div class="page-actions"><a class="btn ghost" href="/">Back to menu</a></div>
  </div>
  <section class="settings-card">
    <form id="settings-form">
      <div class="setting-row">
        <input type="checkbox" id="setting-grid" autofocus>
        <label for="setting-grid"><strong>Floor grid</strong><small>Show the 2-unit factory grid under the kits.</small></label>
      </div>
      <div class="setting-row">
        <input type="checkbox" id="setting-motion">
        <label for="setting-motion"><strong>Conveyor motion</strong><small>Animate sample packages along the conveyor. Reduced-motion preferences win.</small></label>
      </div>
      <div class="settings-actions">
        <button class="btn primary" type="submit">Save settings</button>
        <span id="settings-status" aria-live="polite"></span>
      </div>
      <p class="settings-note">Factory view reads these settings when it loads.</p>
    </form>
  </section>
</main>)";
    return with_script(std::move(page), shell_settings_script());
}

std::string not_found_page(const ShellOptions& options, std::string_view title,
                           std::string_view message) {
    std::string page = shell_head(options, title, "");
    page += "<main class=\"main\"><div class=\"page-head\"><div><p class=\"eyebrow\">404</p><h1>";
    page += net::html_escape(title);
    page += "</h1><p class=\"lede\">";
    page += net::html_escape(message);
    page += "</p></div></div><div class=\"empty\"><p>That surface is not available in this mode.</p><a class=\"btn\" href=\"/\">Back to menu</a></div></main>";
    page += shell_close();
    return page;
}

}  // namespace

net::PortalRouter make_shell_router(const ShellOptions& options) {
    PortalRouter router;
    router.route(HttpMethod::Get, "/",
                 [options](const Request&) { return html_response(intro_page(options)); });
    router.route(HttpMethod::Get, "/factory",
                 [options](const Request&) { return html_response(factory_page(options)); });
    router.route(HttpMethod::Get, "/sessions",
                 [options](const Request&) { return html_response(sessions_page(options)); });
    router.route(HttpMethod::Get, "/settings",
                 [options](const Request&) { return html_response(settings_page(options)); });

    const std::string scene = factory_scene_json();
    router.route(HttpMethod::Get, "/api/factory", [scene](const Request&) {
        Response response;
        response.content_type = "application/json";
        response.body = scene;
        return response;
    });
    router.route(HttpMethod::Get, "/api/health", [](const Request&) {
        Response response;
        response.content_type = "application/json";
        response.body = "{\"status\":\"ok\",\"surface\":\"shell\"}";
        return response;
    });
    router.route(HttpMethod::Get, "/teacher", [options](const Request&) {
        if (!options.teacher_mode) {
            return html_response(not_found_page(options, "Teacher access",
                                                "The teacher portal entry is hidden in student mode."),
                                 404);
        }
        Response response;
        response.status = 303;
        response.content_type = "text/plain; charset=utf-8";
        response.location = options.portal_url;
        response.body = "Teacher portal\n";
        return response;
    });

    return router;
}

}  // namespace sc::app
