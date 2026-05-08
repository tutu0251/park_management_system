import { requireAuth, getSession, loadUsers, saveUsers } from "./auth-session.js";
import { canAccessPage } from "./rbac.js";
import { initShell } from "./shell.js";
import { initClock, initMobileNav, showToast, formatDateTime } from "./common.js";

requireAuth();
const session = getSession();
initShell("users.html");
initClock();
initMobileNav();

function escapeHtml(s) {
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

function newUserId() {
  return `usr-${crypto.randomUUID().replace(/-/g, "").slice(0, 12)}`;
}

const backdrop = document.getElementById("modal-users");
const modalTitle = document.getElementById("modal-users-title");
const modalBody = document.getElementById("modal-users-body");
const modalFooter = document.getElementById("modal-users-footer");

function openModal() {
  backdrop?.classList.add("open");
  backdrop?.setAttribute("aria-hidden", "false");
}

function closeModal() {
  backdrop?.classList.remove("open");
  backdrop?.setAttribute("aria-hidden", "true");
}

/** @type {{ user_id: string, role: string } | null} */
let pendingRoleChange = null;

function renderAccessDenied() {
  const main = document.querySelector("main.content");
  if (!main) return;
  main.innerHTML = `
    <h1 class="page-title">Access restricted</h1>
    <p class="page-desc">User management is limited to administrators in this mockup.</p>
    <div class="card" style="max-width:32rem">
      <div class="card-body">
        <p class="muted" style="margin:0">Your role does not include <strong>User management</strong>. Return to the dashboard or contact a park administrator if you need access.</p>
        <p style="margin:1rem 0 0"><a class="btn btn-primary" href="index.html">Back to dashboard</a></p>
      </div>
    </div>`;
}

if (!session || !canAccessPage(session, "users.html")) {
  renderAccessDenied();
} else {
  function readUsers() {
    return loadUsers();
  }

  function writeUsers(list) {
    saveUsers(list);
    renderTable();
  }

  function getSearch() {
    return (document.getElementById("user-search")?.value || "").trim().toLowerCase();
  }

  function filteredUsers() {
    const q = getSearch();
    const statusF = document.getElementById("user-filter-status")?.value || "all";
    const roleF = document.getElementById("user-filter-role")?.value || "all";
    return readUsers().filter((u) => {
      if (statusF !== "all" && u.status !== statusF) return false;
      if (roleF !== "all" && u.role !== roleF) return false;
      if (!q) return true;
      const hay = `${u.full_name} ${u.email} ${u.role} ${u.status} ${u.user_id}`.toLowerCase();
      return hay.includes(q);
    });
  }

  function renderTable() {
    const tbody = document.querySelector("#users-table tbody");
    if (!tbody) return;
    const rows = filteredUsers();
    tbody.innerHTML = rows
      .map((u) => {
        const isSelf = u.user_id === session.userId;
        const adminCount = readUsers().filter((x) => x.role === "Admin").length;
        const soleAdmin = adminCount === 1 && u.role === "Admin";
        return `
      <tr data-user-id="${escapeHtml(u.user_id)}">
        <td>${escapeHtml(u.full_name)}</td>
        <td class="mono-cell">${escapeHtml(u.email)}</td>
        <td><span class="badge badge-role-${String(u.role).toLowerCase()}">${escapeHtml(u.role)}</span></td>
        <td><span class="badge ${u.status === "Active" ? "badge-success" : "badge-neutral"}">${escapeHtml(u.status)}</span></td>
        <td>${u.last_login_at ? formatDateTime(u.last_login_at) : "—"}</td>
        <td>${formatDateTime(u.registered_at)}</td>
        <td class="action-cell">
          <button type="button" class="btn btn-secondary btn-sm js-edit">Edit</button>
          <button type="button" class="btn btn-secondary btn-sm js-toggle">${u.status === "Active" ? "Deactivate" : "Activate"}</button>
          <button type="button" class="btn btn-danger btn-sm js-delete" ${isSelf || soleAdmin ? `disabled title="${soleAdmin ? "Cannot delete the only administrator" : "You cannot delete your own account in the mock"}"` : ""}>Delete</button>
        </td>
      </tr>`;
      })
      .join("");

    tbody.querySelectorAll(".js-edit").forEach((btn) => {
      btn.addEventListener("click", () => {
        const id = btn.closest("tr")?.dataset.userId;
        const u = readUsers().find((x) => x.user_id === id);
        if (u) openEditModal(u);
      });
    });
    tbody.querySelectorAll(".js-toggle").forEach((btn) => {
      btn.addEventListener("click", () => {
        const id = btn.closest("tr")?.dataset.userId;
        const u = readUsers().find((x) => x.user_id === id);
        if (!u) return;
        const next = u.status === "Active" ? "Inactive" : "Active";
        if (u.user_id === session.userId && next === "Inactive") {
          showToast("You cannot deactivate your own account in the mock.");
          return;
        }
        u.status = next;
        writeUsers(readUsers());
        showToast(`User ${next === "Active" ? "activated" : "deactivated"}.`);
      });
    });
    tbody.querySelectorAll(".js-delete").forEach((btn) => {
      btn.addEventListener("click", () => {
        if (btn.disabled) return;
        const id = btn.closest("tr")?.dataset.userId;
        const u = readUsers().find((x) => x.user_id === id);
        if (u) openDeleteModal(u);
      });
    });
  }

  function openAddModal() {
    modalTitle.textContent = "Register user";
    modalBody.innerHTML = `
      <p class="muted" style="margin:0 0 1rem;font-size:0.8125rem;">New accounts are created as <strong>Inactive</strong> until an administrator activates them.</p>
      <div class="form-row"><label for="add-full">Full name</label><input id="add-full" required autocomplete="name" /></div>
      <div class="form-row"><label for="add-email">Email</label><input id="add-email" type="email" required autocomplete="off" /></div>
      <div class="form-row"><label for="add-pass">Password</label><input id="add-pass" type="password" required autocomplete="new-password" /></div>
      <div class="form-row"><label for="add-role">Role</label>
        <select id="add-role">
          <option>Admin</option>
          <option>Operator</option>
          <option>Staff</option>
        </select>
      </div>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-secondary" data-close>Cancel</button><button type="button" class="btn btn-primary" id="modal-confirm">Create user</button>`;
    modalFooter.querySelector("[data-close]")?.addEventListener("click", closeModal);
    modalFooter.querySelector("#modal-confirm")?.addEventListener("click", () => {
      const full_name = document.getElementById("add-full")?.value.trim() || "";
      const email = document.getElementById("add-email")?.value.trim() || "";
      const password = document.getElementById("add-pass")?.value || "";
      const role = document.getElementById("add-role")?.value || "Staff";
      if (!full_name || !email || password.length < 4) {
        showToast("Enter name, email, and password (min 4 chars).");
        return;
      }
      const list = readUsers();
      if (list.some((x) => x.email.toLowerCase() === email.toLowerCase())) {
        showToast("That email is already registered.");
        return;
      }
      list.push({
        user_id: newUserId(),
        full_name,
        email,
        password,
        role,
        status: "Inactive",
        last_login_at: null,
        registered_at: new Date().toISOString(),
      });
      saveUsers(list);
      showToast("User created (inactive). Activate when ready.");
      closeModal();
      renderTable();
    });
    openModal();
  }

  function openEditModal(u) {
    modalTitle.textContent = "Edit user";
    modalBody.innerHTML = `
      <input type="hidden" id="edit-id" value="${escapeHtml(u.user_id)}" />
      <input type="hidden" id="edit-role-orig" value="${escapeHtml(u.role)}" />
      <div class="form-row"><label for="edit-full">Full name</label><input id="edit-full" value="${escapeHtml(u.full_name)}" /></div>
      <div class="form-row"><label for="edit-email">Email</label><input id="edit-email" type="email" value="${escapeHtml(u.email)}" /></div>
      <div class="form-row"><label for="edit-role">Role</label>
        <select id="edit-role">
          <option ${u.role === "Admin" ? "selected" : ""}>Admin</option>
          <option ${u.role === "Operator" ? "selected" : ""}>Operator</option>
          <option ${u.role === "Staff" ? "selected" : ""}>Staff</option>
        </select>
      </div>
      <div class="form-row"><label for="edit-status">Status</label>
        <select id="edit-status">
          <option value="Active" ${u.status === "Active" ? "selected" : ""}>Active</option>
          <option value="Inactive" ${u.status === "Inactive" ? "selected" : ""}>Inactive</option>
        </select>
      </div>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-secondary" data-close>Cancel</button><button type="button" class="btn btn-primary" id="modal-confirm">Save changes</button>`;
    modalFooter.querySelector("[data-close]")?.addEventListener("click", closeModal);
    modalFooter.querySelector("#modal-confirm")?.addEventListener("click", () => {
      const id = document.getElementById("edit-id")?.value;
      const full_name = document.getElementById("edit-full")?.value.trim() || "";
      const email = document.getElementById("edit-email")?.value.trim() || "";
      const role = document.getElementById("edit-role")?.value || "Staff";
      const status = document.getElementById("edit-status")?.value || "Inactive";
      const origRole = document.getElementById("edit-role-orig")?.value;
      if (!full_name || !email) {
        showToast("Name and email are required.");
        return;
      }
      if (role !== origRole) {
        pendingRoleChange = { user_id: id, full_name, email, role, status };
        openRoleConfirmModal();
        return;
      }
      applyUserEdit(id, full_name, email, role, status);
    });
    openModal();
  }

  function openRoleConfirmModal() {
    if (!pendingRoleChange) return;
    modalTitle.textContent = "Confirm role change";
    modalBody.innerHTML = `<p style="margin:0;line-height:1.5;color:var(--text-secondary);">Changing a user’s <strong>role</strong> updates their permissions across the console and API. Proceed?</p>
      <p class="muted" style="margin:0.75rem 0 0;font-size:0.8125rem;">User: <strong>${escapeHtml(pendingRoleChange.user_id)}</strong> → role <strong>${escapeHtml(pendingRoleChange.role)}</strong></p>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-secondary" data-close>Cancel</button><button type="button" class="btn btn-primary" id="role-confirm-yes">Yes, update role</button>`;
    modalFooter.querySelector("[data-close]")?.addEventListener("click", () => {
      pendingRoleChange = null;
      closeModal();
    });
    modalFooter.querySelector("#role-confirm-yes")?.addEventListener("click", () => {
      if (!pendingRoleChange) return;
      const { user_id, full_name, email, role, status } = pendingRoleChange;
      pendingRoleChange = null;
      applyUserEdit(user_id, full_name, email, role, status);
    });
    openModal();
  }

  function applyUserEdit(user_id, full_name, email, role, status) {
    const list = readUsers();
    const u = list.find((x) => x.user_id === user_id);
    if (!u) return;
    if (list.some((x) => x.user_id !== user_id && x.email.toLowerCase() === email.toLowerCase())) {
      showToast("Another user already uses that email.");
      return;
    }
    if (user_id === session.userId && status === "Inactive") {
      showToast("You cannot set your own account to inactive.");
      return;
    }
    u.full_name = full_name;
    u.email = email;
    u.role = role;
    u.status = status;
    saveUsers(list);
    showToast("User updated.");
    closeModal();
    renderTable();
  }

  function openDeleteModal(u) {
    modalTitle.textContent = "Delete user";
    modalBody.innerHTML = `<p class="remove-explain">Remove <strong>${escapeHtml(u.full_name)}</strong> (${escapeHtml(u.email)})? This cannot be undone in the mock registry.</p>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-secondary" data-close>Cancel</button><button type="button" class="btn btn-danger" id="modal-confirm">Delete user</button>`;
    modalFooter.querySelector("[data-close]")?.addEventListener("click", closeModal);
    modalFooter.querySelector("#modal-confirm")?.addEventListener("click", () => {
      const list = readUsers().filter((x) => x.user_id !== u.user_id);
      if (!list.some((x) => x.role === "Admin")) {
        showToast("Cannot delete the last administrator.");
        closeModal();
        return;
      }
      saveUsers(list);
      showToast("User deleted.");
      closeModal();
      renderTable();
    });
    openModal();
  }

  document.getElementById("modal-users-close")?.addEventListener("click", closeModal);

  document.getElementById("btn-add-user")?.addEventListener("click", openAddModal);

  document.getElementById("user-search")?.addEventListener("input", () => {
    renderTable();
  });
  document.getElementById("user-filter-status")?.addEventListener("change", renderTable);
  document.getElementById("user-filter-role")?.addEventListener("change", renderTable);

  backdrop?.addEventListener("click", (e) => {
    if (e.target === backdrop) closeModal();
  });

  renderTable();
}
