#include "app/shell_script.hpp"

namespace sc::app {

std::string_view shell_factory_script() {
    return R"js(
(() => {
  const canvas = document.getElementById("factory-canvas");
  if (!canvas || !canvas.getContext) return;
  const ctx = canvas.getContext("2d");
  const stage = canvas.closest(".factory-stage");
  const status = document.getElementById("factory-status");
  const params = new URLSearchParams(window.location.search);
  const defaultView = {yaw:-0.76, pitch:0.53, distance:66, tx:0, ty:2, tz:0};
  const view = {...defaultView};
  const materials = {
    1:"#2f6f9f", 2:"#d7a92b", 3:"#748596", 4:"#c94b4b",
    5:"#2f9f70", 6:"#d7dde4", 7:"#343a43", 8:"#a8784a", 9:"#151b26"
  };
  const light = {x:-0.38, y:0.82, z:0.46};
  let scene = null;
  let width = 1;
  let height = 1;
  let dpr = 1;
  let dragging = null;
  let savedSettings = {};
  try { savedSettings = JSON.parse(localStorage.getItem("sc-shell-settings") || "{}"); } catch (_) {}
  const showGrid = savedSettings.grid !== false;
  const animate = savedSettings.motion !== false &&
    !window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

  function resize() {
    const rect = canvas.getBoundingClientRect();
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    width = Math.max(1, Math.floor(rect.width));
    height = Math.max(1, Math.floor(rect.height));
    canvas.width = Math.floor(width * dpr);
    canvas.height = Math.floor(height * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.lineJoin = "round";
  }

  function project(point) {
    const dx = point.x - view.tx;
    const dz = point.z - view.tz;
    const cy = Math.cos(view.yaw);
    const sy = Math.sin(view.yaw);
    const x1 = cy * dx - sy * dz;
    const z1 = sy * dx + cy * dz;
    const cp = Math.cos(view.pitch);
    const sp = Math.sin(view.pitch);
    const y1 = cp * (point.y - view.ty) - sp * z1;
    const z2 = sp * (point.y - view.ty) + cp * z1 + view.distance;
    if (z2 < 1) return null;
    const focal = Math.min(width, height) * 1.25;
    return {x: width / 2 + focal * x1 / z2, y: height / 2 - focal * y1 / z2, depth: z2};
  }

  function shade(hex, factor) {
    const value = parseInt(hex.slice(1), 16);
    const r = clamp(Math.round(((value >> 16) & 255) * factor), 0, 255);
    const g = clamp(Math.round(((value >> 8) & 255) * factor), 0, 255);
    const b = clamp(Math.round((value & 255) * factor), 0, 255);
    return "rgb(" + r + "," + g + "," + b + ")";
  }

  function addBox(faces, piece, override) {
    const x = piece.x;
    const y = piece.y;
    const z = piece.z;
    const x2 = x + piece.w;
    const y2 = y + piece.h;
    const z2 = z + piece.d;
    const color = override || materials[piece.m] || materials[3];
    const quads = [
      {n:{x:0,y:1,z:0}, v:[[x,y2,z],[x2,y2,z],[x2,y2,z2],[x,y2,z2]]},
      {n:{x:0,y:0,z:1}, v:[[x,y,z2],[x2,y,z2],[x2,y2,z2],[x,y2,z2]]},
      {n:{x:1,y:0,z:0}, v:[[x2,y,z],[x2,y,z2],[x2,y2,z2],[x2,y2,z]]},
      {n:{x:-1,y:0,z:0}, v:[[x,y,z],[x,y,z2],[x,y2,z2],[x,y2,z]]},
      {n:{x:0,y:0,z:-1}, v:[[x,y,z],[x2,y,z],[x2,y2,z],[x,y2,z]]},
      {n:{x:0,y:-1,z:0}, v:[[x,y,z],[x2,y,z],[x2,y,z2],[x,y,z2]]}
    ];
    for (const quad of quads) {
      const points = quad.v.map((point) => project({x:point[0], y:point[1], z:point[2]}));
      if (points.some((point) => point === null)) continue;
      const depth = points.reduce((sum, point) => sum + point.depth, 0) / points.length;
      const lambert = Math.max(0, quad.n.x * light.x + quad.n.y * light.y + quad.n.z * light.z);
      faces.push({
        points,
        depth,
        fill: shade(color, 0.62 + 0.48 * lambert),
        stroke: "rgba(3,8,14,.52)"
      });
    }
  }

  function boundsOf(kit) {
    const bounds = {
      minX: Infinity, maxX: -Infinity, minY: Infinity,
      maxY: -Infinity, minZ: Infinity, maxZ: -Infinity
    };
    for (const piece of kit.pieces) {
      bounds.minX = Math.min(bounds.minX, piece.x);
      bounds.maxX = Math.max(bounds.maxX, piece.x + piece.w);
      bounds.minY = Math.min(bounds.minY, piece.y);
      bounds.maxY = Math.max(bounds.maxY, piece.y + piece.h);
      bounds.minZ = Math.min(bounds.minZ, piece.z);
      bounds.maxZ = Math.max(bounds.maxZ, piece.z + piece.d);
    }
    return bounds;
  }

  function drawGrid() {
    if (!showGrid) return;
    const corners = [
      {x:-42,y:0,z:-42}, {x:42,y:0,z:-42},
      {x:42,y:0,z:42}, {x:-42,y:0,z:42}
    ].map(project);
    if (corners.every(Boolean)) {
      ctx.beginPath();
      ctx.moveTo(corners[0].x, corners[0].y);
      for (let i = 1; i < corners.length; ++i) ctx.lineTo(corners[i].x, corners[i].y);
      ctx.closePath();
      ctx.fillStyle = "rgba(8,17,29,.72)";
      ctx.fill();
      ctx.strokeStyle = "rgba(64,100,132,.5)";
      ctx.lineWidth = 1;
      ctx.stroke();
    }
    ctx.beginPath();
    for (let i = -40; i <= 40; i += 2) {
      const a = project({x:i, y:0, z:-42});
      const b = project({x:i, y:0, z:42});
      if (a && b) { ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); }
      const c = project({x:-42, y:0, z:i});
      const d = project({x:42, y:0, z:i});
      if (c && d) { ctx.moveTo(c.x, c.y); ctx.lineTo(d.x, d.y); }
    }
    ctx.strokeStyle = "rgba(74,120,150,.16)";
    ctx.lineWidth = 1;
    ctx.stroke();
  }

  function drawLabels() {
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.font = "700 10px Inter, system-ui, sans-serif";
    const occupied = [];
    for (const kit of scene.kits) {
      const bounds = boundsOf(kit);
      const point = project({
        x:(bounds.minX + bounds.maxX) / 2,
        y:bounds.maxY + 1.2,
        z:(bounds.minZ + bounds.maxZ) / 2
      });
      if (!point || point.depth > 150) continue;
      const label = kit.label.replace(/_/g, " ").toUpperCase();
      const textWidth = ctx.measureText(label).width;
      const left = point.x - textWidth / 2 - 5;
      const right = point.x + textWidth / 2 + 5;
      const top = point.y - 8;
      const bottom = point.y + 8;
      if (left < 0 || right > width || top < 0 || bottom > height) continue;
      const collides = occupied.some((box) =>
        left < box.right && right > box.left && top < box.bottom && bottom > box.top);
      if (collides) continue;
      occupied.push({left, right, top, bottom});
      ctx.fillStyle = "rgba(7,13,22,.76)";
      ctx.fillRect(left, top, right - left, bottom - top);
      ctx.fillStyle = "rgba(173,198,220,.9)";
      ctx.fillText(label, point.x, point.y);
    }
  }

  function drawScene(time) {
    ctx.clearRect(0, 0, width, height);
    drawGrid();
    if (!scene) return;
    const faces = [];
    for (const kit of scene.kits) {
      for (const piece of kit.pieces) addBox(faces, piece);
    }

    if (animate) {
      const conveyor = scene.kits.find((kit) => kit.id.indexOf("conveyor") >= 0);
      if (conveyor) {
        const bounds = boundsOf(conveyor);
        const lane = (bounds.minZ + bounds.maxZ) / 2;
        const span = Math.max(1, bounds.maxX - bounds.minX);
        for (let i = 0; i < 3; ++i) {
          const t = ((time * 0.075) + i / 3) % 1;
          addBox(faces, {
            x:bounds.minX + t * span - 0.7,
            y:bounds.maxY + 0.3,
            z:lane - 0.7,
            w:1.5, h:1.1, d:1.5, m:8
          }, "#b7864e");
        }
      }
    }

    faces.sort((a, b) => b.depth - a.depth);
    for (const face of faces) {
      ctx.beginPath();
      ctx.moveTo(face.points[0].x, face.points[0].y);
      for (let i = 1; i < face.points.length; ++i) {
        ctx.lineTo(face.points[i].x, face.points[i].y);
      }
      ctx.closePath();
      ctx.fillStyle = face.fill;
      ctx.fill();
      ctx.strokeStyle = face.stroke;
      ctx.lineWidth = 0.65;
      ctx.stroke();
    }
    drawLabels();
  }

  function updateStatus() {
    if (!status) return;
    if (!scene) {
      status.textContent = "Loading scene…";
      return;
    }
    const pieces = scene.kits.reduce((sum, kit) => sum + kit.pieces.length, 0);
    status.textContent = scene.kits.length + " kits · " + pieces + " pieces";
    status.classList.remove("is-error");
  }

  function focusKit(focus) {
    if (!scene || !focus) return;
    const needle = focus.toLowerCase();
    const kit = scene.kits.find((item) =>
      item.id.toLowerCase().indexOf(needle) >= 0 || item.name.toLowerCase().indexOf(needle) >= 0);
    if (!kit) return;
    const bounds = boundsOf(kit);
    view.tx = (bounds.minX + bounds.maxX) / 2;
    view.tz = (bounds.minZ + bounds.maxZ) / 2;
    view.ty = 2;
    view.distance = clamp((bounds.maxX - bounds.minX + bounds.maxZ - bounds.minZ) * 1.2, 30, 72);
    if (status) status.textContent = "Focused on " + kit.label.replace(/_/g, " ");
  }

  function resetView() {
    Object.assign(view, defaultView);
    const focus = params.get("focus");
    if (focus) focusKit(focus);
  }

  function startDrag(event) {
    if (event.button > 2) return;
    event.preventDefault();
    dragging = {
      x:event.clientX, y:event.clientY,
      yaw:view.yaw, pitch:view.pitch,
      tx:view.tx, tz:view.tz,
      mode:event.button === 0 ? "orbit" : "pan"
    };
    if (canvas.setPointerCapture) canvas.setPointerCapture(event.pointerId);
    stage.classList.add("is-dragging");
  }

  function moveDrag(event) {
    if (!dragging) return;
    const dx = event.clientX - dragging.x;
    const dy = event.clientY - dragging.y;
    if (dragging.mode === "orbit") {
      view.yaw = dragging.yaw - dx * 0.006;
      view.pitch = clamp(dragging.pitch + dy * 0.005, 0.12, 1.36);
    } else {
      const scale = view.distance / Math.max(260, Math.min(width, height)) * 1.9;
      const cy = Math.cos(view.yaw);
      const sy = Math.sin(view.yaw);
      view.tx = dragging.tx - (dx * cy + dy * sy) * scale;
      view.tz = dragging.tz - (-dx * sy + dy * cy) * scale;
    }
  }

  function endDrag() {
    dragging = null;
    stage.classList.remove("is-dragging");
  }

  canvas.addEventListener("pointerdown", startDrag);
  canvas.addEventListener("pointermove", moveDrag);
  canvas.addEventListener("pointerup", endDrag);
  canvas.addEventListener("pointercancel", endDrag);
  canvas.addEventListener("pointerleave", (event) => {
    if (event.buttons === 0) endDrag();
  });
  canvas.addEventListener("contextmenu", (event) => event.preventDefault());
  canvas.addEventListener("wheel", (event) => {
    event.preventDefault();
    view.distance = clamp(view.distance * Math.exp(event.deltaY * 0.0012), 22, 230);
  }, {passive:false});

  for (const button of document.querySelectorAll("[data-factory-view]")) {
    button.addEventListener("click", () => {
      const action = button.getAttribute("data-factory-view");
      if (action === "top") {
        Object.assign(view, {yaw:0, pitch:1.32, distance:78, tx:0, ty:0, tz:0});
      } else if (action === "cell") {
        resetView();
        focusKit("press");
      } else {
        resetView();
      }
    });
  }

  window.addEventListener("keydown", (event) => {
    if (event.target && /input|select|textarea/i.test(event.target.tagName)) return;
    if (event.key.toLowerCase() === "r") resetView();
  });
  window.addEventListener("resize", resize);

  resize();
  resetView();
  const frame = (now) => {
    drawScene(now / 1000);
    window.requestAnimationFrame(frame);
  };
  window.requestAnimationFrame(frame);

  fetch("/api/factory")
    .then((response) => {
      if (!response.ok) throw new Error("factory request failed");
      return response.json();
    })
    .then((data) => {
      scene = data;
      updateStatus();
      const focus = params.get("focus");
      if (focus) focusKit(focus);
    })
    .catch(() => {
      if (status) {
        status.textContent = "Scene could not load";
        status.classList.add("is-error");
      }
    });
})();
)js";
}

}  // namespace sc::app
