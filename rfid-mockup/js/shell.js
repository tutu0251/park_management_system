/**
 * Shared chrome: navigation visibility (RBAC), user chip, profile menu, sign out.
 */

import { getSession, logout, updatePasswordForUser } from "./auth-session.js";
import { can, navLinkPermission } from "./rbac.js";
import { showToast } from "./common.js";

function initials(name) {
  const parts = String(name || "")
    .trim()
    .split(/\s+/);
  if (parts.length >= 2) return (parts[0][0] + parts[1][0]).toUpperCase();
  const s = parts[0] || "?";
  return s.slice(0, 2).toUpperCase();
}

function basename(href) {
  try {
    const u = new URL(href, location.href);
    const segs = u.pathname.split("/").filter(Boolean);
    return segs[segs.length - 1] || href;
  } catch {
    return href.split("/").pop() || href;
  }
}

/**
 * @param {string} activeFile e.g. "index.html"
 */
export function initShell(activeFile) {
  const session = getSession();
  if (!session) return;

  document.querySelectorAll(".sidebar-nav a").forEach((a) => {
    const file = basename(a.getAttribute("href") || "");
    const perm = navLinkPermission(file);
    if (perm && !can(session, perm)) {
      a.style.display = "none";
    } else {
      a.style.display = "";
    }
    if (file === activeFile) a.classList.add("active");
    else a.classList.remove("active");
  });

  const av = document.getElementById("user-avatar-initials");
  const nm = document.getElementById("user-chip-name");
  const rl = document.getElementById("user-chip-role");
  if (av) av.textContent = initials(session.fullName);
  if (nm) nm.textContent = session.fullName;
  if (rl) rl.textContent = session.role;
  const em = document.getElementById("user-menu-email");
  if (em) em.textContent = session.email;

  const btn = document.getElementById("user-chip-btn");
  const menu = document.getElementById("user-menu");
  if (btn && menu) {
    const close = () => {
      menu.hidden = true;
      btn.setAttribute("aria-expanded", "false");
    };
    btn.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      const open = menu.hidden;
      if (open) {
        menu.hidden = false;
        btn.setAttribute("aria-expanded", "true");
      } else close();
    });
    document.addEventListener("click", close);
    menu.addEventListener("click", (e) => e.stopPropagation());

    menu.querySelector("#menu-change-password")?.addEventListener("click", () => {
      close();
      openChangePasswordModal(session);
    });
    menu.querySelector("#menu-sign-out")?.addEventListener("click", () => {
      logout();
      location.href = "login.html";
    });
  }

  ensurePasswordModal();
}

/**
 * @param {{ userId: string }} session
 */
function openChangePasswordModal(session) {
  const backdrop = document.getElementById("shell-password-modal");
  if (!backdrop) return;
  const err = backdrop.querySelector("#pw-modal-error");
  const p1 = /** @type {HTMLInputElement} */ (backdrop.querySelector("#pw-new-1"));
  const p2 = /** @type {HTMLInputElement} */ (backdrop.querySelector("#pw-new-2"));
  if (err) err.textContent = "";
  if (p1) p1.value = "";
  if (p2) p2.value = "";
  backdrop.classList.add("open");
  backdrop.setAttribute("aria-hidden", "false");

  const saveBtn = /** @type {HTMLButtonElement | null} */ (backdrop.querySelector("#pw-save-btn"));

  const close = () => {
    backdrop.classList.remove("open");
    backdrop.setAttribute("aria-hidden", "true");
    backdrop.onclick = null;
    if (saveBtn) saveBtn.onclick = null;
  };

  backdrop.querySelectorAll("[data-close-pw]").forEach((b) => {
    b.onclick = () => close();
  });
  backdrop.onclick = (e) => {
    if (e.target === backdrop) close();
  };

  if (saveBtn) {
    saveBtn.onclick = () => {
      const a = /** @type {HTMLInputElement} */ (backdrop.querySelector("#pw-new-1"))?.value || "";
      const b = /** @type {HTMLInputElement} */ (backdrop.querySelector("#pw-new-2"))?.value || "";
      const erEl = backdrop.querySelector("#pw-modal-error");
      if (a.length < 4) {
        if (erEl) erEl.textContent = "Password must be at least 4 characters (mock).";
        return;
      }
      if (a !== b) {
        if (erEl) erEl.textContent = "Passwords do not match.";
        return;
      }
      updatePasswordForUser(session.userId, a);
      showToast("Password updated (mock registry).");
      close();
    };
  }
}

function ensurePasswordModal() {
  if (document.getElementById("shell-password-modal")) return;
  const el = document.createElement("div");
  el.className = "modal-backdrop";
  el.id = "shell-password-modal";
  el.setAttribute("aria-hidden", "true");
  el.innerHTML = `
    <div class="modal" role="dialog" aria-labelledby="pw-modal-title">
      <div class="modal-header">
        <h2 id="pw-modal-title">Change password</h2>
        <button type="button" class="icon-btn" data-close-pw aria-label="Close">×</button>
      </div>
      <div class="modal-body">
        <p class="muted" style="margin:0 0 0.75rem;font-size:0.8125rem;">Mock only — stored in your browser with the demo user list.</p>
        <p id="pw-modal-error" class="form-error" role="alert"></p>
        <div class="form-row">
          <label for="pw-new-1">New password</label>
          <input type="password" id="pw-new-1" autocomplete="new-password" />
        </div>
        <div class="form-row">
          <label for="pw-new-2">Confirm password</label>
          <input type="password" id="pw-new-2" autocomplete="new-password" />
        </div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-secondary" data-close-pw>Cancel</button>
        <button type="button" class="btn btn-primary" id="pw-save-btn">Save</button>
      </div>
    </div>`;
  document.body.appendChild(el);
}
