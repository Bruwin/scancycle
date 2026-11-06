#include "app/editor_page.hpp"

#include <string>

namespace sc::app {
namespace {

// Kept as one stylesheet so every editor page has the same density, controls
// and focus treatment. No external font or script: the pages must work on a
// loopback classroom machine with no network.
constexpr std::string_view kEditorCss = R"css(
:root{
  color-scheme:light;
  --bg:#f2f5f8;--surface:#fff;--surface-2:#f8fafc;--ink:#14202b;
  --muted:#62707d;--line:#d9e1e8;--line-strong:#c5d0da;
  --accent:#0a62a8;--accent-ink:#fff;--accent-soft:#eaf3fb;
  --ok:#16794c;--ok-soft:#e8f5ee;--bad:#b42318;--bad-soft:#fdf0ef;
  --warn:#8a5a00;--warn-soft:#fff7e6;--radius:12px;
  --shadow:0 1px 2px rgba(16,24,40,.05),0 10px 30px rgba(16,24,40,.04);
}
*{box-sizing:border-box}
html{background:var(--bg)}
body{
  margin:0;color:var(--ink);
  font:15px/1.5 Inter,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
  background:radial-gradient(circle at 15% 0%,#eaf2f9 0,transparent 35rem),var(--bg);
}
a{color:var(--accent);text-decoration:none}
a:hover{text-decoration:underline}
code,pre{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
.topbar{
  max-width:1160px;margin:0 auto;padding:24px 24px 0;
  display:flex;align-items:center;gap:20px;flex-wrap:wrap;
}
.brand{display:flex;align-items:center;gap:11px;margin-right:auto}
.brand-mark{
  display:grid;place-items:center;width:36px;height:36px;border-radius:10px;
  background:#102a43;color:#fff;font-size:12px;font-weight:800;letter-spacing:.08em;
}
.brand-copy{display:flex;flex-direction:column;line-height:1.15}
.brand-copy strong{font-size:15px;letter-spacing:-.01em}
.brand-copy small{font-size:10px;color:var(--muted);letter-spacing:.12em;text-transform:uppercase}
.tabs{
  display:flex;gap:2px;padding:3px;border:1px solid var(--line);
  border-radius:9px;background:#e9eef3;
}
.tab{padding:.42rem .9rem;border-radius:7px;color:#4d5d6b;font-size:13px;font-weight:650}
.tab:hover{text-decoration:none;background:#f5f8fb}
.tab.active{background:#fff;color:#102a43;box-shadow:0 1px 2px rgba(16,24,40,.08)}
.local{display:flex;align-items:center;gap:7px;color:var(--muted);font-size:12px}
.local::before{
  content:"";width:7px;height:7px;border-radius:50%;
  background:#1f9d63;box-shadow:0 0 0 3px #dff4e8;
}
.shell{max-width:1160px;margin:0 auto;padding:28px 24px 56px}
.page-head{
  display:flex;align-items:flex-start;justify-content:space-between;
  gap:24px;margin:8px 0 24px;
}
.eyebrow{
  margin:0 0 4px;color:var(--accent);font-size:11px;font-weight:750;
  letter-spacing:.12em;text-transform:uppercase;
}
h1,h2,h3,p{margin-top:0}
h1{font-size:clamp(1.8rem,3vw,2.45rem);line-height:1.12;letter-spacing:-.035em;margin-bottom:10px}
h2{font-size:1.15rem;line-height:1.3;letter-spacing:-.015em;margin-bottom:4px}
h3{font-size:1rem;line-height:1.35;margin-bottom:4px}
.lede{max-width:58ch;margin:0;color:var(--muted)}
.session{
  display:flex;align-items:center;gap:10px;flex-wrap:wrap;
  justify-content:flex-end;padding-top:6px;
}
.badge{
  display:inline-flex;align-items:center;gap:6px;border-radius:999px;
  padding:.28rem .65rem;font-size:12px;font-weight:700;white-space:nowrap;
  border:1px solid transparent;
}
.badge::before{content:"";width:6px;height:6px;border-radius:50%;background:currentColor}
.badge.ok{color:var(--ok);background:var(--ok-soft);border-color:#cce8d8}
.badge.bad{color:var(--bad);background:var(--bad-soft);border-color:#f2d3d0}
.badge.warn{color:var(--warn);background:var(--warn-soft);border-color:#f0dfb5}
.badge.neutral{color:var(--muted);background:#eef2f5;border-color:var(--line)}
.scan{font-size:12px;color:var(--muted)}
.scan strong{color:var(--ink);font-variant-numeric:tabular-nums}
.panel{
  background:var(--surface);border:1px solid var(--line);border-radius:var(--radius);
  box-shadow:var(--shadow);margin:0 0 18px;overflow:hidden;
}
.panel-head{
  display:flex;align-items:flex-start;justify-content:space-between;
  gap:16px;padding:18px 20px;border-bottom:1px solid var(--line);
}
.panel-head p{margin:2px 0 0;color:var(--muted);font-size:13px}
.panel-body{padding:20px}
.chip{
  display:inline-flex;align-items:center;gap:5px;padding:.22rem .55rem;
  border-radius:999px;background:var(--surface-2);border:1px solid var(--line);
  color:var(--muted);font-size:11px;font-weight:650;white-space:nowrap;
}
.rung-list{display:grid;gap:14px}
.rung-card{border:1px solid var(--line);border-radius:10px;background:#fff;overflow:hidden}
.rung-head{
  display:flex;align-items:flex-start;gap:10px;padding:12px 16px;
  background:var(--surface-2);border-bottom:1px solid var(--line);
}
.rung-index{
  display:grid;place-items:center;min-width:30px;height:26px;border-radius:6px;
  background:#e8eef4;color:#405363;font:600 11px ui-monospace,monospace;
}
.rung-head h3{margin:0}
.rung-head p{margin:2px 0 0;color:var(--muted);font-size:12px}
.diagram{
  padding:18px;overflow-x:auto;
  background:
    linear-gradient(#fbfcfd,#fbfcfd) padding-box,
    repeating-linear-gradient(0deg,#f0f3f6 0 1px,transparent 1px 24px);
}
.diagram svg{display:block;margin:0 auto;max-width:none}
svg.ladder,svg.fbd,svg.sfc{
  background:#fff;border:1px solid var(--line);border-radius:8px;
  box-shadow:0 1px 2px rgba(16,24,40,.04);
}
svg.ladder,svg.fbd{width:100%;max-width:640px;height:auto}
svg.sfc{max-width:100%;height:auto}
svg.ladder .wire,svg.fbd .wire,svg.sfc .wire{stroke:#334155;stroke-width:2;fill:none}
svg.ladder .bar,svg.fbd .bar{stroke:#111827;stroke-width:3}
svg.ladder .slash,svg.fbd .slash{stroke:#b42318;stroke-width:2}
svg.ladder .coil,svg.fbd .coil{fill:none;stroke:#111827;stroke-width:2}
svg.ladder .block,svg.fbd .block{fill:#f8fafc;stroke:#334155;stroke-width:2}
svg.ladder text,svg.fbd text,svg.sfc text{
  font:11px ui-monospace,SFMono-Regular,Menlo,monospace;fill:#111827;
}
svg.sfc .step{fill:#f8fafc;stroke:#334155;stroke-width:2}
svg.sfc .step.active{fill:#e8f3ff;stroke:#0a62a8}
svg.sfc .arrow{fill:#334155}
svg.fbd .neg{fill:#fff;stroke:#b42318;stroke-width:2}
.toolbar{
  display:flex;align-items:flex-start;gap:8px;flex-wrap:wrap;
  padding:12px 16px;border-top:1px solid var(--line);background:var(--surface-2);
}
.btn{
  display:inline-flex;align-items:center;justify-content:center;gap:6px;
  border:1px solid var(--line-strong);border-radius:7px;background:#fff;
  color:#263746;padding:.42rem .7rem;font-family:inherit;font-size:12px;
  font-weight:600;line-height:1.2;cursor:pointer;
}
.btn:hover{text-decoration:none;background:#f7fafc;border-color:#aebdca}
.btn.primary{background:var(--accent);border-color:var(--accent);color:var(--accent-ink)}
.btn.primary:hover{background:#08558f}
.btn.danger{color:var(--bad);border-color:#edcbc8}
.btn.danger:hover{background:var(--bad-soft)}
.btn.subtle{background:transparent}
.btn:disabled{opacity:.55;cursor:not-allowed}
.editor-tools{margin-left:auto}
.editor-tools summary{
  cursor:pointer;color:var(--accent);padding:.42rem .2rem;
  font-family:inherit;font-size:12px;font-weight:600;line-height:1.2;
}
.editor-tools summary:hover{text-decoration:underline}
.edit-grid{
  display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));
  gap:10px;margin-top:12px;padding-top:12px;
  border-top:1px dashed var(--line-strong);width:100%;
}
.tool-form{
  display:grid;gap:8px;padding:12px;border:1px solid var(--line);
  border-radius:8px;background:#fff;
}
.tool-form h4{margin:0;font-size:12px;color:#405363;letter-spacing:.06em;text-transform:uppercase}
.tool-form .row{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
label{font-size:12px;color:var(--muted)}
input,select,textarea{
  font:inherit;color:var(--ink);border:1px solid var(--line-strong);
  border-radius:6px;background:#fff;padding:.38rem .5rem;min-width:0;
}
input:focus,select:focus,textarea:focus{
  outline:2px solid #9ec9ea;outline-offset:1px;border-color:var(--accent);
}
input[type=checkbox]{accent-color:var(--accent);width:auto}
.tool-form input[name=path]{width:5.5rem}
.tool-form input[name=tag],.tool-form input[name=coil]{width:8.5rem}
.tool-form input[name=index]{width:4rem}
.add-rung{
  display:flex;align-items:center;justify-content:space-between;gap:16px;
  flex-wrap:wrap;margin-top:18px;padding-top:18px;border-top:1px solid var(--line);
}
.add-rung p{margin:2px 0 0;color:var(--muted);font-size:12px}
.add-rung form{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.table-wrap{overflow-x:auto}
table{width:100%;border-collapse:collapse}
th,td{padding:.55rem .65rem;text-align:left;vertical-align:top;border-bottom:1px solid var(--line)}
th{
  font-size:11px;letter-spacing:.04em;text-transform:uppercase;
  color:var(--muted);background:var(--surface-2);
}
tbody tr:hover{background:#fbfcfd}
td code,th code{background:#eef2f5;padding:.08rem .3rem;border-radius:4px}
.value{font-weight:650;font-variant-numeric:tabular-nums}
.value.true{color:var(--ok)}
.value.false{color:var(--muted)}
.alert{
  margin:0 0 18px;padding:14px 16px;border:1px solid #f0c9c6;
  border-radius:10px;background:var(--bad-soft);color:#7a1a12;
}
.alert h2{font-size:1rem;margin-bottom:6px}
.alert p{margin:0}
.notice{
  padding:14px 16px;border:1px solid var(--line);border-radius:10px;
  background:var(--surface-2);color:var(--muted);
}
.notice + .diagram{margin-top:14px}
.split{display:grid;grid-template-columns:1fr 1fr;gap:18px;align-items:start}
.disclosure{
  border:1px solid var(--line);border-radius:var(--radius);background:var(--surface);
  box-shadow:var(--shadow);margin-bottom:18px;overflow:hidden;
}
.disclosure summary{
  cursor:pointer;padding:14px 18px;font-weight:650;color:#263746;
  background:var(--surface-2);border-bottom:1px solid transparent;
}
.disclosure[open] summary{border-bottom-color:var(--line)}
.disclosure .panel-body{padding:18px}
pre{
  margin:0;padding:14px;border-radius:8px;background:#0f1720;
  color:#e6edf3;overflow:auto;font-size:12px;line-height:1.5;
}
textarea{
  width:100%;resize:vertical;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;
  font-size:13px;line-height:1.5;
}
fieldset{border:1px solid var(--line);border-radius:8px;padding:10px;margin:0}
legend{font-size:12px;color:var(--muted)}
.grid-2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.meta{display:flex;align-items:center;gap:8px;flex-wrap:wrap;color:var(--muted);font-size:12px}
.dot{width:7px;height:7px;border-radius:50%;background:#94a3b8;display:inline-block}
.dot.ok{background:#1f9d63}.dot.bad{background:#b42318}.dot.warn{background:#b7791f}
@media(max-width:760px){
  .topbar{padding:18px 16px 0;gap:12px}
  .brand-copy small{display:none}
  .local{display:none}
  .shell{padding:22px 16px 40px}
  .page-head{flex-direction:column;gap:12px}
  .session{justify-content:flex-start;padding-top:0}
  .panel-head{padding:15px 16px}
  .panel-body{padding:16px}
  .diagram{padding:12px}
  .split{grid-template-columns:1fr}
  .edit-grid{grid-template-columns:1fr}
  .toolbar{padding:10px 12px}
  .editor-tools{margin-left:0}
}
@media print{
  .topbar,.session,.toolbar,.editor-tools,.add-rung,form,button{display:none!important}
  body{background:#fff}
  .shell{max-width:none;padding:0}
  .panel,.rung-card,.disclosure{box-shadow:none;border:0}
}
)css";

}  // namespace

std::string editor_page_head(std::string_view title, std::string_view active) {
    std::string page;
    page.reserve(kEditorCss.size() + 1024);
    page += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
    page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    page += "<title>";
    page += std::string(title);
    page += "</title><style>";
    page += kEditorCss;
    page += "</style></head><body><header class=\"topbar\">";
    page += "<div class=\"brand\"><span class=\"brand-mark\">SC</span>";
    page += "<div class=\"brand-copy\"><strong>Scan Cycle</strong>";
    page += "<small>PLC Workbench</small></div></div>";
    page += "<nav class=\"tabs\" aria-label=\"Editor views\">";

    if (active == "sfc") {
        page += "<span class=\"tab active\" aria-current=\"page\">SFC</span>";
    } else {
        const bool fbd = active == "fbd";
        page += "<a class=\"tab";
        if (!fbd) {
            page += " active\" aria-current=\"page";
        }
        page += "\" href=\"/\">Ladder</a>";
        page += "<a class=\"tab";
        if (fbd) {
            page += " active\" aria-current=\"page";
        }
        page += "\" href=\"/view?mode=fbd\">FBD</a>";
    }

    page += "</nav><span class=\"local\">Local session</span></header>";
    page += "<main class=\"shell\">";
    return page;
}

std::string editor_page_foot() { return "</main></body></html>"; }

}  // namespace sc::app
