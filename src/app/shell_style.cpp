#include "app/shell_style.hpp"

namespace sc::app {

std::string_view shell_style() {
    return R"css(
:root{
  color-scheme:dark;
  --bg:#070c14;--panel:#0d1624;--panel-2:#111d2e;--line:#223249;
  --ink:#e8eff8;--muted:#8ca1b8;--gold:#f3b642;--gold-2:#df8c24;
  --teal:#35d0ba;--bad:#f06a6a;--ok:#55d68b;
  --radius:16px;--shadow:0 22px 70px rgba(0,0,0,.42);
}
*{box-sizing:border-box}
html{min-height:100%;background:var(--bg)}
body{
  margin:0;min-height:100vh;min-height:100dvh;color:var(--ink);
  font:15px/1.55 Inter,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
  background:
    radial-gradient(900px 520px at 86% -12%,rgba(243,182,66,.14),transparent 66%),
    radial-gradient(820px 620px at -6% 108%,rgba(53,208,186,.09),transparent 60%),
    var(--bg);
}
body::before{
  content:"";position:fixed;inset:0;pointer-events:none;opacity:.22;
  background-image:
    linear-gradient(rgba(126,165,196,.17) 1px,transparent 1px),
    linear-gradient(90deg,rgba(126,165,196,.17) 1px,transparent 1px);
  background-size:48px 48px;
  mask-image:radial-gradient(ellipse at center,#000 18%,transparent 78%);
}
a{color:inherit;text-decoration:none}
a:hover{color:var(--gold)}
:focus-visible{outline:2px solid var(--gold);outline-offset:3px}
.menu-card:focus-visible,.session-card:focus-visible,.btn:focus-visible,.nav-link:focus-visible{
  outline:2px solid var(--gold);outline-offset:3px;
}
.menu-card:focus-visible{
  transform:translateY(-2px);border-color:rgba(243,182,66,.45);
  background:linear-gradient(120deg,rgba(24,39,61,.98),rgba(15,25,41,.98));
}
.menu-card:focus-visible .menu-arrow{transform:translateX(3px);color:var(--gold)}
.btn.primary:focus-visible{outline-color:#fff}
button,input,select,textarea{font:inherit}
button{color:inherit}
.app{position:relative;display:flex;flex-direction:column;min-height:100vh;min-height:100dvh;
  padding-left:env(safe-area-inset-left);padding-right:env(safe-area-inset-right)}
.topbar{
  position:relative;z-index:2;width:min(1320px,calc(100% - 48px));
  margin:0 auto;padding:calc(20px + env(safe-area-inset-top)) 0 20px;
  display:flex;align-items:center;gap:22px;
}
.brand{display:flex;align-items:center;gap:11px;margin-right:auto;min-width:0}
.mark{
  display:grid;place-items:center;flex:0 0 auto;
  clip-path:polygon(50% 0,94% 25%,94% 75%,50% 100%,6% 75%,6% 25%);
  background:linear-gradient(145deg,var(--gold),var(--gold-2));
  color:#1a1000;font-weight:900;letter-spacing:-.08em;
  text-shadow:0 1px 0 rgba(255,255,255,.28);
}
.mark-small{width:36px;height:36px;font-size:13px}
.mark-large{width:118px;height:118px;font-size:42px}
.brand-copy{display:flex;flex-direction:column;line-height:1.12;min-width:0}
.brand-copy strong{font-size:15px;letter-spacing:-.015em;white-space:nowrap}
.brand-copy small{color:var(--muted);font-size:10px;letter-spacing:.16em;text-transform:uppercase}
.nav{display:flex;align-items:center;gap:4px;flex-wrap:wrap}
.nav-link{
  padding:.45rem .72rem;border-radius:8px;color:var(--muted);
  font-size:12px;font-weight:700;letter-spacing:.02em;
}
.nav-link:hover{background:var(--panel-2);color:var(--ink);text-decoration:none}
.nav-link.active{background:var(--panel-2);color:var(--gold)}
.local{
  display:flex;align-items:center;gap:7px;color:var(--muted);
  font-size:11px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;
  white-space:nowrap;
}
.local::before{content:"";width:7px;height:7px;border-radius:50%;background:var(--teal);
  box-shadow:0 0 0 4px rgba(53,208,186,.12)}
.main{position:relative;z-index:1;width:min(1240px,calc(100% - 48px));margin:0 auto;flex:1}
.intro{
  display:grid;grid-template-columns:minmax(0,1fr) minmax(350px,430px);
  gap:clamp(38px,7vw,100px);align-items:center;
  min-height:calc(100vh - 150px);padding:34px 0 58px;
}
.hero-copy{max-width:620px}
.eyebrow{
  margin:0 0 9px;color:var(--gold);font-size:10px;font-weight:800;
  letter-spacing:.19em;text-transform:uppercase;
}
h1,h2,h3,p{margin-top:0;overflow-wrap:anywhere}
h1{font-size:clamp(2.7rem,6vw,5rem);line-height:.94;letter-spacing:-.065em;margin-bottom:18px}
h2{font-size:1.25rem;line-height:1.22;letter-spacing:-.025em;margin-bottom:6px}
h3{font-size:1rem;line-height:1.25;margin-bottom:4px}
.version{
  display:inline-block;margin-left:8px;padding:.22rem .45rem;
  border:1px solid rgba(243,182,66,.38);border-radius:999px;
  color:var(--gold);font-size:12px;letter-spacing:.04em;vertical-align:middle;
}
.lede{max-width:48ch;margin-bottom:20px;color:var(--muted);font-size:clamp(1rem,1.6vw,1.16rem)}
.logo-lockup{display:flex;align-items:center;gap:20px;margin-bottom:28px}
.logo-glow{
  position:relative;flex:0 0 auto;
  filter:drop-shadow(0 0 34px rgba(243,182,66,.2));
}
.logo-glow::after{
  content:"";position:absolute;inset:-22px;border:1px solid rgba(243,182,66,.18);
  border-radius:50%;transform:rotate(-18deg);pointer-events:none;
}
.hero-note{
  display:flex;align-items:center;gap:10px;flex-wrap:wrap;
  margin-top:26px;color:var(--muted);font-size:12px;
}
.chip{
  display:inline-flex;align-items:center;gap:6px;padding:.26rem .58rem;
  border:1px solid var(--line);border-radius:999px;background:rgba(17,29,46,.72);
  color:var(--muted);font-size:10px;font-weight:800;letter-spacing:.08em;
  text-transform:uppercase;white-space:nowrap;
}
.chip::before{content:"";width:5px;height:5px;border-radius:50%;background:var(--teal)}
.chip.gold{color:var(--gold);border-color:rgba(243,182,66,.32)}
.chip.gold::before{background:var(--gold)}
.menu{display:grid;gap:10px}
.menu-card{
  display:flex;align-items:center;gap:14px;min-height:76px;padding:14px 15px;
  border:1px solid var(--line);border-radius:13px;background:
    linear-gradient(120deg,rgba(17,29,46,.95),rgba(13,22,36,.92));
  box-shadow:0 10px 30px rgba(0,0,0,.12);
  transition:transform .16s ease,border-color .16s ease,background .16s ease;
}
.menu-card:hover{
  transform:translateY(-2px);border-color:rgba(243,182,66,.45);
  background:linear-gradient(120deg,rgba(24,39,61,.98),rgba(15,25,41,.98));
  text-decoration:none;
}
.menu-icon{
  display:grid;place-items:center;width:42px;height:42px;flex:0 0 auto;
  border:1px solid var(--line);border-radius:10px;background:#0a1220;
  color:var(--gold);
}
.menu-icon svg{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.7;
  stroke-linecap:round;stroke-linejoin:round}
.menu-text{display:flex;flex-direction:column;gap:2px;min-width:0}
.menu-text strong{font-size:14px;letter-spacing:-.01em}
.menu-text small{color:var(--muted);font-size:11px}
.menu-arrow{margin-left:auto;color:var(--muted);font-size:17px;transition:transform .16s ease}
.menu-card:hover .menu-arrow{transform:translateX(3px);color:var(--gold)}
.teacher-card{border-color:rgba(243,182,66,.24)}
.teacher-card .menu-icon{color:var(--teal);border-color:rgba(53,208,186,.24)}
.foot{
  display:flex;align-items:center;justify-content:space-between;gap:14px;flex-wrap:wrap;
  padding:18px 0 max(24px,env(safe-area-inset-bottom));
  border-top:1px solid rgba(34,50,73,.7);
  color:var(--muted);font-size:11px;
}
.foot a{color:var(--muted)}
.foot a:hover{color:var(--gold)}
.page-head{display:flex;align-items:flex-end;justify-content:space-between;gap:24px;
  flex-wrap:wrap;padding:34px 0 24px}
.page-actions{display:flex;align-items:center;justify-content:flex-end;gap:10px;flex-wrap:wrap}
.page-head h1{font-size:clamp(2rem,4vw,3rem);margin-bottom:8px}
.page-head .lede{margin-bottom:0;font-size:14px}
.btn{
  display:inline-flex;align-items:center;justify-content:center;gap:7px;
  min-height:44px;padding:.5rem .78rem;border:1px solid var(--line);
  border-radius:8px;background:var(--panel-2);color:var(--ink);
  font-size:12px;font-weight:750;cursor:pointer;white-space:nowrap;
}
.btn:hover{text-decoration:none;border-color:rgba(243,182,66,.45);color:var(--gold)}
.btn.primary{background:var(--gold);border-color:var(--gold);color:#1a1000}
.btn.primary:hover{background:#ffd16a;color:#1a1000}
.btn.ghost{background:transparent}
.btn.small{min-height:36px;padding:.3rem .55rem;font-size:11px}
.btn.small.active{color:var(--gold);border-color:rgba(243,182,66,.45);background:rgba(243,182,66,.08)}
.factory-main{padding:8px 0 38px}
.factory-head{padding:18px 0 16px}
.factory-head h1{font-size:clamp(1.8rem,3.5vw,2.6rem);margin-bottom:4px}
.factory-head .eyebrow{margin-bottom:4px}
.factory-stage{
  position:relative;min-height:520px;height:min(68vh,720px);
  border:1px solid var(--line);border-radius:var(--radius);overflow:hidden;
  background:
    radial-gradient(ellipse at 50% 25%,rgba(52,92,122,.2),transparent 58%),
    linear-gradient(180deg,#0a1524,#060b12);
  box-shadow:var(--shadow);
}
#factory-canvas{display:block;width:100%;height:100%;touch-action:none;cursor:grab}
.factory-stage.is-dragging #factory-canvas{cursor:grabbing}
.factory-overlay{
  position:absolute;left:18px;top:18px;max-width:270px;padding:12px 14px;
  border:1px solid rgba(56,82,110,.7);border-radius:10px;
  background:rgba(7,13,22,.78);backdrop-filter:blur(8px);
}
.factory-overlay p{margin:3px 0 0;color:var(--muted);font-size:11px}
.factory-overlay strong{font-size:12px;letter-spacing:.03em}
.factory-controls{
  position:absolute;left:18px;bottom:18px;display:flex;align-items:center;gap:6px;
  flex-wrap:wrap;max-width:calc(100% - 36px);
}
.factory-hint{color:var(--muted);font-size:11px;padding-left:4px}
.factory-legend{
  position:absolute;right:18px;top:18px;display:grid;gap:5px;
  padding:11px 13px;border:1px solid rgba(56,82,110,.7);border-radius:10px;
  background:rgba(7,13,22,.78);font-size:10px;color:var(--muted);
  backdrop-filter:blur(8px);
}
.factory-legend span{display:flex;align-items:center;gap:7px}
.swatch{width:8px;height:8px;border-radius:2px;display:inline-block}
.factory-state{
  position:absolute;right:18px;bottom:18px;color:var(--muted);font-size:11px;
  padding:5px 8px;border-radius:6px;background:rgba(7,13,22,.7);
}
.factory-state.is-error{color:var(--bad)}
.factory-footer{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;
  padding:12px 2px 0;color:var(--muted);font-size:11px}
.factory-footer a{color:var(--gold)}
.session-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin:0 0 18px}
.session-card{
  display:flex;flex-direction:column;min-height:170px;padding:18px;
  border:1px solid var(--line);border-radius:13px;background:linear-gradient(140deg,var(--panel-2),var(--panel));
  transition:transform .16s ease,border-color .16s ease;
}
.session-card:hover{transform:translateY(-2px);border-color:rgba(243,182,66,.45);text-decoration:none}
.session-card h2{margin:10px 0 5px}
.session-card p{color:var(--muted);font-size:12px;margin-bottom:16px}
.session-card .open{margin-top:auto;color:var(--gold);font-size:11px;font-weight:750}
.session-actions{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-top:20px}
.settings-card{max-width:680px;padding:22px;border:1px solid var(--line);border-radius:14px;
  background:linear-gradient(140deg,var(--panel-2),var(--panel));box-shadow:var(--shadow)}
.settings-card form{display:grid;gap:16px}
.setting-row{display:flex;align-items:flex-start;gap:12px;padding-bottom:14px;
  border-bottom:1px solid rgba(34,50,73,.7)}
.setting-row:last-of-type{border-bottom:0;padding-bottom:0}
.setting-row input[type=checkbox]{accent-color:var(--gold);width:20px;height:20px;flex:0 0 auto;margin-top:1px}
.setting-row strong{display:block;font-size:13px}
.setting-row small{display:block;color:var(--muted);font-size:11px;margin-top:2px}
.settings-note{color:var(--muted);font-size:11px;margin:0}
#settings-status{color:var(--ok);font-size:12px;min-height:1.4em}
.empty{padding:40px 24px;border:1px dashed var(--line);border-radius:12px;text-align:center;color:var(--muted)}
@media(max-width:900px){
  .intro{grid-template-columns:1fr;min-height:auto;padding-top:20px;gap:34px}
  .hero-copy{max-width:none}
  .logo-lockup{margin-bottom:20px}
  .session-grid{grid-template-columns:1fr 1fr}
}
@media(max-width:620px){
  .topbar{width:calc(100% - 32px);padding:calc(14px + env(safe-area-inset-top)) 0 14px;
    gap:10px;flex-wrap:wrap}
  .local{display:none}
  .nav{order:3;width:100%;overflow:auto;padding-top:2px}
  .main{width:calc(100% - 32px)}
  .intro{padding:18px 0 36px}
  .mark-large{width:86px;height:86px;font-size:30px}
  h1{font-size:clamp(2.5rem,14vw,4rem)}
  .session-grid{grid-template-columns:1fr}
  .factory-stage{height:58vh;min-height:420px}
  .factory-overlay{max-width:210px;padding:9px 11px}
  .factory-legend{display:none}
  .factory-hint{width:100%;padding-left:0}
  .factory-controls{bottom:12px;left:12px}
  .factory-state{right:12px;bottom:12px;max-width:150px;text-align:right}
  .page-head{padding-top:20px}
}
@media(max-width:420px){
  .menu-card{padding:12px;gap:10px}
  .menu-icon{width:38px;height:38px}
  .menu-text small{font-size:10px}
  .nav-link{padding:.5rem .55rem}
  .page-actions{justify-content:flex-start}
}
@media(prefers-reduced-motion:reduce){
  *{scroll-behavior:auto!important}
  .menu-card,.session-card,.menu-arrow,.btn{transition:none}
}
)css";
}

}  // namespace sc::app
