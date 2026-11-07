#include "app/shell_script.hpp"

namespace sc::app {

std::string_view shell_settings_script() {
    return R"js(
(() => {
  const form = document.getElementById("settings-form");
  if (!form) return;
  const grid = document.getElementById("setting-grid");
  const motion = document.getElementById("setting-motion");
  const status = document.getElementById("settings-status");
  const key = "sc-shell-settings";
  try {
    const saved = JSON.parse(localStorage.getItem(key) || "{}");
    grid.checked = saved.grid !== false;
    motion.checked = saved.motion !== false;
  } catch (_) {}
  form.addEventListener("submit", (event) => {
    event.preventDefault();
    try {
      localStorage.setItem(key, JSON.stringify({grid:grid.checked, motion:motion.checked}));
      status.textContent = "Saved for this browser.";
    } catch (_) {
      status.textContent = "Could not save settings in this browser.";
    }
  });
})();
)js";
}

}  // namespace sc::app
