import {
  MOCK_MACHINES,
  TYPE_CATALOG,
  NODE_TO_LOCATION,
  LOCATIONS,
} from "./mock-data.js";
import {
  initClock,
  initMobileNav,
  showToast,
  formatMoney,
  formatDateTime,
  machineStatusBadgeClass,
} from "./common.js";
import { requireAuth, getSession } from "./auth-session.js";
import { can, PERM, canAccessPage } from "./rbac.js";
import { initShell } from "./shell.js";

let machAdmin = false;
let machMonitor = false;

/** @type {Record<string, typeof TYPE_CATALOG[string]>} */
let typeCatalog = { ...TYPE_CATALOG };

/** @type {Array<Record<string, unknown>>} */
const machines = MOCK_MACHINES.map((m) => ({
  ...m,
  removed: false,
  ignored: false,
}));

function escapeHtml(s) {
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

function locationForNode(nodeId) {
  return NODE_TO_LOCATION[nodeId] ?? "Unknown Location";
}

function typeEntry(typeId) {
  return typeCatalog[typeId] ?? null;
}

function isUnknownType(m) {
  return !m.type_in_catalog || !typeEntry(m.type_id);
}

function isNewMachine(m) {
  return !m.in_database;
}

function activeMachines() {
  return machines.filter((m) => !m.removed && !m.ignored);
}

function countSummaries() {
  const list = activeMachines();
  const c = {
    total: list.length,
    Online: 0,
    Maintenance: 0,
    Offline: 0,
    Error: 0,
    newCount: 0,
    unknownType: 0,
  };
  for (const m of list) {
    if (m.status === "Online") c.Online++;
    else if (m.status === "Maintenance") c.Maintenance++;
    else if (m.status === "Offline") c.Offline++;
    else if (m.status === "Error") c.Error++;
    if (isNewMachine(m)) c.newCount++;
    if (isUnknownType(m)) c.unknownType++;
  }
  return c;
}

function renderKpis() {
  const c = countSummaries();
  const set = (id, v) => {
    const el = document.getElementById(id);
    if (el) el.textContent = String(v);
  };
  set("kpi-total", c.total);
  set("kpi-online", c.Online);
  set("kpi-maintenance", c.Maintenance);
  set("kpi-offline", c.Offline);
  set("kpi-error", c.Error);
  set("kpi-new", c.newCount);
  set("kpi-unknown-type", c.unknownType);
}

let filterStatus = "all";
let filterLocation = "all";
let filterTypeId = "all";
let filterSearch = "";

function populateFilterSelects() {
  const locSel = document.getElementById("filter-location");
  const typeSel = document.getElementById("filter-type");
  if (!locSel || !typeSel) return;

  const locs = [...LOCATIONS];
  locSel.innerHTML =
    `<option value="all">All locations</option>` +
    locs.map((l) => `<option value="${escapeHtml(l)}">${escapeHtml(l)}</option>`).join("");

  const typeIds = [...new Set(activeMachines().map((m) => m.type_id))].sort();
  typeSel.innerHTML =
    `<option value="all">All types</option>` +
    typeIds.map((t) => `<option value="${escapeHtml(t)}">${escapeHtml(t)}</option>`).join("");

  if ([...locSel.options].some((o) => o.value === filterLocation)) locSel.value = filterLocation;
  else locSel.value = "all";

  if ([...typeSel.options].some((o) => o.value === filterTypeId)) typeSel.value = filterTypeId;
  else typeSel.value = "all";
}

function filterList() {
  const q = filterSearch.trim().toLowerCase();
  return activeMachines().filter((m) => {
    const loc = locationForNode(m.node_id);
    if (filterLocation !== "all" && loc !== filterLocation) return false;
    if (filterTypeId !== "all" && m.type_id !== filterTypeId) return false;

    if (filterStatus === "server-new") {
      if (!isNewMachine(m)) return false;
    } else if (filterStatus === "server-unknown-type") {
      if (!isUnknownType(m)) return false;
    } else if (filterStatus !== "all" && m.status !== filterStatus) return false;

    if (q) {
      const hay = [m.node_id, m.reader_id, m.game_id, m.game_name].join(" ").toLowerCase();
      if (!hay.includes(q)) return false;
    }
    return true;
  });
}

function actionsHtml(m) {
  const parts = [];
  if (machAdmin) {
    if (isNewMachine(m)) {
      parts.push(
        `<button type="button" class="btn btn-primary btn-sm js-register" data-reader="${escapeHtml(m.reader_id)}">Register machine</button>`,
        `<button type="button" class="btn btn-secondary btn-sm js-edit" data-reader="${escapeHtml(m.reader_id)}">Edit info</button>`,
        `<button type="button" class="btn btn-ghost btn-sm js-ignore" data-reader="${escapeHtml(m.reader_id)}">Ignore</button>`
      );
    }
    if (isUnknownType(m)) {
      parts.push(
        `<button type="button" class="btn btn-secondary btn-sm js-add-type" data-reader="${escapeHtml(m.reader_id)}">Add type</button>`
      );
    }
    if (m.status === "Offline") {
      parts.push(
        `<button type="button" class="btn btn-secondary btn-sm js-remove" data-reader="${escapeHtml(m.reader_id)}">Remove</button>`
      );
    }
  }
  if (parts.length === 0) {
    return `<span class="muted">${machAdmin ? "—" : "View only"}</span>`;
  }
  return `<div class="action-cell">${parts.join("")}</div>`;
}

function costDisplay(m) {
  if (isUnknownType(m)) return `<span class="muted">—</span>`;
  const e = typeEntry(m.type_id);
  if (e) return escapeHtml(formatMoney(e.cost_per_play));
  if (typeof m.cost_per_play === "number") return escapeHtml(formatMoney(m.cost_per_play));
  return `<span class="muted">—</span>`;
}

function typeNameCell(m) {
  const unk = isUnknownType(m);
  const e = typeEntry(m.type_id);
  const name = e ? e.type_name : "—";
  const badge = unk ? `<span class="badge badge-unknown-type">Unknown type</span>` : "";
  return `${badge}<span class="type-name-text">${escapeHtml(name)}</span>`;
}

function newMarkCell(m) {
  if (isNewMachine(m)) {
    return `<span class="badge badge-new">New</span>`;
  }
  return `<span class="muted">—</span>`;
}

function renderTable() {
  const list = filterList();
  const tbody = document.querySelector("#machines-table tbody");
  if (!tbody) return;

  tbody.innerHTML = list
    .map((m) => {
      const loc = locationForNode(m.node_id);
      return `
    <tr data-reader="${escapeHtml(m.reader_id)}">
      <td class="td-checkbox"><input type="checkbox" class="row-select" value="${escapeHtml(m.reader_id)}" aria-label="Select ${escapeHtml(m.reader_id)}" /></td>
      <td><span class="badge ${machineStatusBadgeClass(m.status)}">${escapeHtml(m.status)}</span></td>
      <td>${newMarkCell(m)}</td>
      <td class="mono-cell">${escapeHtml(m.node_id)}</td>
      <td class="mono-cell">${escapeHtml(m.reader_id)}</td>
      <td class="mono-cell">${escapeHtml(m.game_id)}</td>
      <td class="mono-cell">${escapeHtml(m.type_id)}</td>
      <td class="type-name-cell">${typeNameCell(m)}</td>
      <td>${escapeHtml(m.game_name)}</td>
      <td>${escapeHtml(loc)}</td>
      <td class="text-right">${costDisplay(m)}</td>
      <td>${formatDateTime(m.last_status_check)}</td>
      <td>${m.last_swipe_at ? formatDateTime(m.last_swipe_at) : "—"}</td>
      <td class="mono-cell">${escapeHtml(m.firmware)}</td>
      <td>${actionsHtml(m)}</td>
    </tr>`;
    })
    .join("");

  syncSelectAllCheckbox();
  const table = document.getElementById("machines-table");
  if (table) table.classList.toggle("machines-table--no-select", !machMonitor);
}

function getSelectedReaders() {
  return [...document.querySelectorAll(".row-select:checked")].map((el) => el.value);
}

function syncSelectAllCheckbox() {
  const master = document.getElementById("select-all-visible");
  if (!master) return;
  const boxes = [...document.querySelectorAll(".row-select")];
  if (boxes.length === 0) {
    master.checked = false;
    master.indeterminate = false;
    return;
  }
  const n = boxes.filter((b) => b.checked).length;
  master.checked = n === boxes.length;
  master.indeterminate = n > 0 && n < boxes.length;
}

/** @type {string | null} */
let removeTargetReader = null;

function openModal(id) {
  const el = document.getElementById(id);
  if (!el) return;
  el.classList.add("open");
  el.setAttribute("aria-hidden", "false");
}

function closeModal(id) {
  const el = document.getElementById(id);
  if (!el) return;
  el.classList.remove("open");
  el.setAttribute("aria-hidden", "true");
}

function wireTableEvents() {
  const tbody = document.querySelector("#machines-table tbody");
  if (!tbody) return;

  tbody.addEventListener("change", (e) => {
    const t = e.target;
    if (t && t.classList && t.classList.contains("row-select")) syncSelectAllCheckbox();
  });

  tbody.addEventListener("click", (e) => {
    const btn = e.target.closest("button");
    if (!btn) return;
    const reader = btn.dataset.reader;
    const m = machines.find((x) => x.reader_id === reader);
    if (!m) return;

    if (btn.classList.contains("js-register")) {
      m.in_database = true;
      showToast("Machine registered (mock). New mark cleared.");
      refresh();
      return;
    }
    if (btn.classList.contains("js-ignore")) {
      m.ignored = true;
      showToast("Machine hidden (mock).");
      refresh();
      return;
    }
    if (btn.classList.contains("js-add-type")) {
      openAddTypeModal(m);
      return;
    }
    if (btn.classList.contains("js-remove")) {
      removeTargetReader = m.reader_id;
      const label = document.getElementById("remove-machine-label");
      if (label) {
        label.textContent = `${m.reader_id} · ${m.game_name} · ${locationForNode(m.node_id)}`;
      }
      openModal("modal-remove-machine");
      return;
    }
    if (btn.classList.contains("js-edit")) {
      const body = document.getElementById("edit-machine-body");
      if (body) {
        body.innerHTML = `<p>Mock editor for <strong>${escapeHtml(m.reader_id)}</strong>. In production this form would update registry fields tied to <span class="mono-inline">${escapeHtml(m.node_id)}</span>.</p><p>Game: ${escapeHtml(m.game_name)} (${escapeHtml(m.game_id)})</p>`;
      }
      openModal("modal-edit-machine");
    }
  });
}

function openAddTypeModal(m) {
  const modal = document.getElementById("modal-add-type");
  const idInput = document.getElementById("add-type-id");
  if (!modal || !idInput) return;
  idInput.value = m.type_id;
  document.getElementById("add-type-name").value = "";
  document.getElementById("add-cost").value = typeof m.cost_per_play === "number" ? String(m.cost_per_play) : "3.5";
  document.getElementById("add-category").value = "Arcade";
  document.getElementById("add-desc").value = "";
  document.getElementById("add-duration").value = "120";
  modal.dataset.targetReader = m.reader_id;
  openModal("modal-add-type");
}

function wireToolbar() {
  document.getElementById("btn-check-all")?.addEventListener("click", () => {
    const n = activeMachines().length;
    const now = new Date().toISOString();
    activeMachines().forEach((m) => {
      m.last_status_check = now;
    });
    showToast(`CheckStatus sent to ${n} machine(s) (mock). Timestamps updated.`);
    refresh();
  });

  document.getElementById("btn-check-selected")?.addEventListener("click", () => {
    const ids = getSelectedReaders();
    if (ids.length === 0) {
      showToast("Select one or more machines first.");
      return;
    }
    const now = new Date().toISOString();
    ids.forEach((rid) => {
      const m = machines.find((x) => x.reader_id === rid);
      if (m && !m.removed && !m.ignored) m.last_status_check = now;
    });
    showToast(`CheckStatus sent to ${ids.length} selected (mock).`);
    refresh();
  });

  document.getElementById("filter-status")?.addEventListener("change", (e) => {
    filterStatus = e.target.value;
    renderTable();
  });
  document.getElementById("filter-location")?.addEventListener("change", (e) => {
    filterLocation = e.target.value;
    renderTable();
  });
  document.getElementById("filter-type")?.addEventListener("change", (e) => {
    filterTypeId = e.target.value;
    renderTable();
  });
  let searchT;
  document.getElementById("filter-search")?.addEventListener("input", (e) => {
    clearTimeout(searchT);
    searchT = setTimeout(() => {
      filterSearch = e.target.value;
      renderTable();
    }, 180);
  });

  document.getElementById("select-all-visible")?.addEventListener("change", (e) => {
    const on = e.target.checked;
    document.querySelectorAll(".row-select").forEach((cb) => {
      cb.checked = on;
    });
    syncSelectAllCheckbox();
  });
}

function wireModals() {
  document.querySelectorAll("[data-close-modal]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const key = btn.getAttribute("data-close-modal");
      if (key === "add-type") closeModal("modal-add-type");
      if (key === "remove-machine") closeModal("modal-remove-machine");
      if (key === "edit-machine") closeModal("modal-edit-machine");
    });
  });

  document.getElementById("form-add-type")?.addEventListener("submit", (e) => {
    e.preventDefault();
    const modal = document.getElementById("modal-add-type");
    const reader = modal?.dataset.targetReader;
    const m = machines.find((x) => x.reader_id === reader);
    if (!m) return;

    const type_name = document.getElementById("add-type-name").value.trim();
    const cost = parseFloat(document.getElementById("add-cost").value);
    const category = document.getElementById("add-category").value.trim();
    const description = document.getElementById("add-desc").value.trim();
    const default_play_duration = parseInt(document.getElementById("add-duration").value, 10);

    typeCatalog[m.type_id] = {
      type_name,
      cost_per_play: cost,
      category,
      description: description || "—",
      default_play_duration,
    };
    m.type_in_catalog = true;
    if (typeof m.cost_per_play !== "number") m.cost_per_play = cost;

    showToast(`Type ${m.type_id} saved (mock).`);
    closeModal("modal-add-type");
    refresh();
  });

  document.getElementById("btn-confirm-remove")?.addEventListener("click", () => {
    if (!removeTargetReader) return;
    const m = machines.find((x) => x.reader_id === removeTargetReader);
    if (m) {
      m.removed = true;
      showToast("Machine removed from active list (mock).");
    }
    removeTargetReader = null;
    closeModal("modal-remove-machine");
    refresh();
  });

  document.getElementById("btn-edit-save-mock")?.addEventListener("click", () => {
    showToast("Changes saved (mock).");
    closeModal("modal-edit-machine");
  });

  ["modal-add-type", "modal-remove-machine", "modal-edit-machine"].forEach((id) => {
    document.getElementById(id)?.addEventListener("click", (e) => {
      if (e.target.id === id) closeModal(id);
    });
  });
}

function refresh() {
  populateFilterSelects();
  renderKpis();
  renderTable();
}

requireAuth();
const machSession = getSession();
initShell("machines.html");
initClock();
initMobileNav();

if (!machSession || !canAccessPage(machSession, "machines.html")) {
  const main = document.querySelector("main.content");
  if (main) {
    main.innerHTML = `
      <h1 class="page-title">Access restricted</h1>
      <p class="page-desc">Machine monitoring is available to operators and administrators.</p>
      <div class="card" style="max-width:32rem">
        <div class="card-body">
          <p class="muted" style="margin:0">Use the transaction log or dashboard for your permitted areas.</p>
          <p style="margin:1rem 0 0"><a class="btn btn-primary" href="index.html">Back to dashboard</a></p>
        </div>
      </div>`;
  }
} else {
  machAdmin = can(machSession, PERM.MACHINES_ADMIN);
  machMonitor = can(machSession, PERM.MACHINES_MONITOR);
  const tb = document.querySelector(".machine-toolbar-actions");
  if (tb) tb.style.display = machMonitor ? "" : "none";
  wireToolbar();
  wireTableEvents();
  wireModals();
  refresh();
}
