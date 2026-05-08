/** Shared UI helpers — clock, mobile nav, toast */

export function initClock(elementId = "live-clock") {
  const el = document.getElementById(elementId);
  if (!el) return;
  function tick() {
    const now = new Date();
    el.textContent = now.toLocaleString(undefined, {
      weekday: "short",
      year: "numeric",
      month: "short",
      day: "numeric",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
  }
  tick();
  setInterval(tick, 1000);
}

export function initMobileNav() {
  const toggle = document.querySelector(".menu-toggle");
  const sidebar = document.querySelector(".sidebar");
  const overlay = document.querySelector(".sidebar-overlay");
  if (!toggle || !sidebar) return;
  function close() {
    sidebar.classList.remove("open");
    overlay?.classList.remove("show");
  }
  toggle.addEventListener("click", () => {
    sidebar.classList.toggle("open");
    overlay?.classList.toggle("show");
  });
  overlay?.addEventListener("click", close);
  sidebar.querySelectorAll("a").forEach((a) => a.addEventListener("click", close));
}

export function showToast(message, ms = 2400) {
  let t = document.getElementById("app-toast");
  if (!t) {
    t = document.createElement("div");
    t.id = "app-toast";
    t.className = "toast";
    document.body.appendChild(t);
  }
  t.textContent = message;
  t.classList.add("show");
  clearTimeout(showToast._tid);
  showToast._tid = setTimeout(() => t.classList.remove("show"), ms);
}

export function formatMoney(n) {
  return new Intl.NumberFormat(undefined, { style: "currency", currency: "USD" }).format(n);
}

export function formatDateTime(iso) {
  if (!iso) return "—";
  return new Date(iso).toLocaleString();
}

export function eventBadgeClass(type) {
  const map = {
    Success: "badge-success",
    Check: "badge-info",
    Fail: "badge-warning",
    Error: "badge-danger",
  };
  return map[type] || "badge-neutral";
}

export function machineStatusBadgeClass(status) {
  const map = {
    Online: "badge-success",
    Offline: "badge-neutral",
    Error: "badge-danger",
    Maintenance: "badge-warning",
    Removed: "badge-neutral",
  };
  return map[status] || "badge-neutral";
}

export function cardStatusBadgeClass(status) {
  const map = {
    Active: "badge-success",
    CheckedOut: "badge-info",
    Inactive: "badge-neutral",
    /** Legacy mock labels — normalized on load in cards.js */
    Disabled: "badge-neutral",
    Lost: "badge-neutral",
  };
  return map[status] || "badge-neutral";
}
