import { MOCK_CARDS } from "./mock-data.js";
import {
  initClock,
  initMobileNav,
  showToast,
  formatMoney,
  formatDateTime,
  cardStatusBadgeClass,
} from "./common.js";
import { requireAuth, getSession } from "./auth-session.js";
import { can, PERM, canAccessPage } from "./rbac.js";
import { initShell } from "./shell.js";

/** RFID card registry mutations (admin / operator with card access in real system; here Admin only). */
let cardsManage = false;

const STORAGE_KEY = "park_rfid_user_cards_v1";

function escapeHtml(s) {
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

function normalizeStatus(status) {
  if (status === "Disabled" || status === "Lost") return "Inactive";
  return status;
}

function normalizeCard(raw) {
  const c = { ...raw };
  c.status = normalizeStatus(c.status);
  if (!Array.isArray(c.checkout_records)) c.checkout_records = [];
  if (c.status === "CheckedOut" && typeof c.remaining_credit !== "number" && c.checkout_records.length) {
    const last = c.checkout_records[c.checkout_records.length - 1];
    if (last && typeof last.remaining_credit === "number") c.remaining_credit = last.remaining_credit;
  }
  return c;
}

function cloneInitialCards() {
  return MOCK_CARDS.map((x) => normalizeCard(JSON.parse(JSON.stringify(x))));
}

function loadCardsFromStorage() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed)) return null;
    return parsed.map(normalizeCard);
  } catch {
    return null;
  }
}

function persistCards() {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(cards));
  } catch {
    showToast("Could not save to browser storage");
  }
}

let cards = loadCardsFromStorage() || cloneInitialCards();

function generateCheckoutTxId() {
  const d = new Date();
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, "0");
  const day = String(d.getDate()).padStart(2, "0");
  const h = String(d.getHours()).padStart(2, "0");
  const min = String(d.getMinutes()).padStart(2, "0");
  const sec = String(d.getSeconds()).padStart(2, "0");
  const r = String(Math.floor(Math.random() * 900) + 100);
  return `CO-${y}${m}${day}-${h}${min}${sec}-${r}`;
}

function countByStatus(st) {
  return cards.filter((c) => c.status === st).length;
}

function renderSummary() {
  const el = document.getElementById("cards-summary");
  if (!el) return;
  const active = countByStatus("Active");
  const checkedOut = countByStatus("CheckedOut");
  el.innerHTML = `
    <div class="cards-stat">
      <p class="stat-label">Active cards</p>
      <p class="stat-value">${active}</p>
    </div>
    <div class="cards-stat">
      <p class="stat-label">Checked out</p>
      <p class="stat-value">${checkedOut}</p>
    </div>
    <div class="cards-stat">
      <p class="stat-label">Total in registry</p>
      <p class="stat-value">${cards.length}</p>
    </div>`;

  const hint = document.getElementById("reissue-hint");
  if (hint) hint.hidden = checkedOut === 0;
}

function getFilters() {
  const q = document.getElementById("search-card")?.value.trim().toLowerCase() || "";
  const statusFilter = document.getElementById("filter-status")?.value || "Active";
  return { q, statusFilter };
}

function rowMatches(card, q, statusFilter) {
  if (q && !card.card_id.toLowerCase().includes(q)) return false;
  if (statusFilter !== "all" && card.status !== statusFilter) return false;
  return true;
}

function detailsCell(card) {
  if (card.status === "CheckedOut") {
    return `<span class="details-cell-note">This card is no longer in use. Available to be reissued.</span>`;
  }
  return '<span class="muted">—</span>';
}

function renderTable() {
  const { q, statusFilter } = getFilters();
  const rows = cards.filter((c) => rowMatches(c, q, statusFilter));
  const tbody = document.querySelector("#cards-table tbody");
  tbody.innerHTML = rows
    .map(
      (c) => `
    <tr data-card-id="${escapeHtml(c.card_id)}">
      <td class="mono-cell">${escapeHtml(c.card_id)}</td>
      <td>${c.owner ? escapeHtml(c.owner) : '<span class="muted">—</span>'}</td>
      <td class="text-right">${formatMoney(c.credit)}</td>
      <td><span class="badge ${cardStatusBadgeClass(c.status)}">${escapeHtml(c.status)}</span></td>
      <td>${detailsCell(c)}</td>
      <td>${formatDateTime(c.created_at)}</td>
      <td>${formatDateTime(c.last_used_at)}</td>
      <td class="action-cell">${actionButtons(c)}</td>
    </tr>`
    )
    .join("");

  tbody.querySelectorAll("button").forEach((btn) => {
    btn.addEventListener("click", (e) => {
      const tr = e.target.closest("tr");
      const id = tr?.dataset.cardId;
      const card = cards.find((x) => x.card_id === id);
      if (!card) return;
      if (btn.disabled) return;
      if (btn.classList.contains("btn-recharge")) openModal("recharge", card);
      else if (btn.classList.contains("btn-deduct")) openModal("deduct", card);
      else if (btn.classList.contains("btn-disable")) openModal("disable", card);
      else if (btn.classList.contains("btn-history")) openModal("history", card);
      else if (btn.classList.contains("btn-remove")) openModal("remove", card);
      else if (btn.classList.contains("btn-checkout-logs")) openModal("checkout_logs", card);
    });
  });
}

function actionButtons(c) {
  if (!cardsManage) {
    if (c.status === "CheckedOut") {
      return `<button type="button" class="btn btn-secondary btn-sm btn-checkout-logs">Checkout logs</button>`;
    }
    return `<button type="button" class="btn btn-ghost btn-sm btn-history">History</button>`;
  }
  if (c.status === "CheckedOut") {
    return `
      <button type="button" class="btn btn-secondary btn-sm btn-checkout-logs">Checkout logs</button>
      <button type="button" class="btn btn-ghost btn-sm btn-remove" disabled title="Cannot remove checked-out cards from this view">Remove</button>`;
  }
  const activeOps =
    c.status === "Active"
      ? `
        <button type="button" class="btn btn-secondary btn-sm btn-recharge">Recharge</button>
        <button type="button" class="btn btn-secondary btn-sm btn-deduct">Deduct</button>
        <button type="button" class="btn btn-secondary btn-sm btn-disable">Make inactive</button>`
      : `
        <button type="button" class="btn btn-secondary btn-sm btn-recharge" disabled>Recharge</button>
        <button type="button" class="btn btn-secondary btn-sm btn-deduct" disabled>Deduct</button>
        <button type="button" class="btn btn-secondary btn-sm btn-disable" disabled>Make inactive</button>`;
  return `
    ${activeOps}
    <button type="button" class="btn btn-ghost btn-sm btn-history">History</button>
    <button type="button" class="btn btn-ghost btn-sm btn-remove">Remove</button>`;
}

const backdrop = document.getElementById("modal-backdrop");
const modalTitle = document.getElementById("modal-title");
const modalBody = document.getElementById("modal-body");
const modalFooter = document.getElementById("modal-footer");

function closeModal() {
  backdrop.classList.remove("open");
}

function openModal(kind, card) {
  backdrop.classList.add("open");
  modalFooter.innerHTML = "";

  if (kind === "recharge") {
    modalTitle.textContent = "Recharge credit";
    modalBody.innerHTML = `
      <p style="margin:0 0 1rem;color:var(--muted);font-size:0.875rem;">Card <strong class="mono-cell">${escapeHtml(card.card_id)}</strong><br>Current balance: ${formatMoney(card.credit)}</p>
      <div class="form-row">
        <label for="amt-recharge">Amount (USD)</label>
        <input type="number" id="amt-recharge" min="0.01" step="0.01" value="10" />
      </div>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-secondary" data-close>Cancel</button><button type="button" class="btn btn-primary" id="confirm-action">Apply recharge</button>`;
    modalFooter.querySelector("#confirm-action").addEventListener("click", () => {
      const inp = document.getElementById("amt-recharge");
      const amt = parseFloat(inp?.value || "0");
      if (amt > 0) {
        card.credit = Math.round((card.credit + amt) * 100) / 100;
        persistCards();
        showToast(`Recharged ${formatMoney(amt)} to ${card.card_id}`);
        renderSummary();
        renderTable();
      }
      closeModal();
    });
  } else if (kind === "deduct") {
    modalTitle.textContent = "Deduct credit";
    modalBody.innerHTML = `
      <p style="margin:0 0 1rem;color:var(--muted);font-size:0.875rem;">Card <strong class="mono-cell">${escapeHtml(card.card_id)}</strong><br>Current balance: ${formatMoney(card.credit)}</p>
      <div class="form-row">
        <label for="amt-deduct">Amount (USD)</label>
        <input type="number" id="amt-deduct" min="0.01" step="0.01" value="5" />
      </div>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-secondary" data-close>Cancel</button><button type="button" class="btn btn-primary" id="confirm-action">Apply deduction</button>`;
    modalFooter.querySelector("#confirm-action").addEventListener("click", () => {
      const amt = parseFloat(document.getElementById("amt-deduct")?.value || "0");
      if (amt > 0) {
        card.credit = Math.max(0, Math.round((card.credit - amt) * 100) / 100);
        persistCards();
        showToast(`Deducted ${formatMoney(amt)} from ${card.card_id}`);
        renderSummary();
        renderTable();
      }
      closeModal();
    });
  } else if (kind === "disable") {
    modalTitle.textContent = "Make card inactive";
    modalBody.innerHTML = `
      <p style="margin:0;color:var(--text-secondary);font-size:0.875rem;">Mark <strong class="mono-cell">${escapeHtml(card.card_id)}</strong> as <strong>Inactive</strong>? Guests cannot use it until you register a replacement.</p>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-secondary" data-close>Cancel</button><button type="button" class="btn btn-danger" id="confirm-action">Confirm</button>`;
    modalFooter.querySelector("#confirm-action").addEventListener("click", () => {
      card.status = "Inactive";
      persistCards();
      showToast(`Card ${card.card_id} is now inactive`);
      renderSummary();
      renderTable();
      closeModal();
    });
  } else if (kind === "remove") {
    modalTitle.textContent = "Remove card from registry";
    modalBody.innerHTML = `
      <p class="remove-explain">Remove <strong class="mono-remove-target">${escapeHtml(card.card_id)}</strong> from this list? This only affects the mock registry in your browser.</p>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-secondary" data-close>Cancel</button><button type="button" class="btn btn-danger" id="confirm-action">Remove</button>`;
    modalFooter.querySelector("#confirm-action").addEventListener("click", () => {
      cards = cards.filter((x) => x.card_id !== card.card_id);
      persistCards();
      showToast(`Removed ${card.card_id}`);
      renderSummary();
      renderTable();
      closeModal();
    });
  } else if (kind === "history") {
    modalTitle.textContent = "Card history";
    modalBody.innerHTML = `
      <p style="margin:0 0 0.75rem;font-size:0.8125rem;color:var(--muted);">Timeline for <span class="mono-cell">${escapeHtml(card.card_id)}</span></p>
      <ul style="margin:0;padding-left:1.1rem;font-size:0.8125rem;color:var(--text-secondary);">
        <li>${formatDateTime(card.last_used_at)} — Last activity</li>
        <li>${formatDateTime(card.created_at)} — Card issued</li>
        <li>Full transaction feed lives on the Transactions page when connected to an API.</li>
      </ul>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-primary" data-close>Close</button>`;
  } else if (kind === "checkout_logs") {
    modalTitle.textContent = "Checkout history";
    const recs = card.checkout_records || [];
    const rows =
      recs.length === 0
        ? `<tr><td colspan="5" style="color:var(--muted);">No checkout records.</td></tr>`
        : recs
            .slice()
            .reverse()
            .map(
              (r) => `
      <tr>
        <td class="mono-cell">${escapeHtml(card.card_id)}</td>
        <td class="text-right">${formatMoney(r.remaining_credit)}</td>
        <td class="mono-cell">${escapeHtml(r.transaction_id)}</td>
        <td>${formatDateTime(r.checkout_time)}</td>
        <td>${r.operator_comments ? escapeHtml(r.operator_comments) : '<span class="muted">—</span>'}</td>
      </tr>`
            )
            .join("");
    modalBody.innerHTML = `
      <p style="margin:0 0 0.5rem;font-size:0.8125rem;color:var(--muted);">Recorded when this card was checked out. <span class="mono-cell">${escapeHtml(card.card_id)}</span> cannot be reactivated here — reissue using a <strong>new card ID</strong>.</p>
      <div class="table-wrap">
        <table class="checkout-log-table">
          <thead>
            <tr>
              <th>card_id</th>
              <th class="text-right">remaining_credit</th>
              <th>transaction_id</th>
              <th>checkout_time</th>
              <th>operator_comments</th>
            </tr>
          </thead>
          <tbody>${rows}</tbody>
        </table>
      </div>`;
    modalFooter.innerHTML = `<button type="button" class="btn btn-primary" data-close>Close</button>`;
  }

  modalFooter.querySelectorAll("[data-close]").forEach((b) => b.addEventListener("click", closeModal));
}

function openCheckoutConfirmModal(card) {
  backdrop.classList.add("open");
  modalTitle.textContent = "Confirm card checkout";
  modalBody.innerHTML = `
    <p style="margin:0 0 1rem;line-height:1.5;color:var(--text-secondary);font-size:0.875rem;">You are about to check out this card. Remaining credits will be returned to the user. Do you want to proceed?</p>
    <p style="margin:0 0 0.5rem;font-size:0.8125rem;color:var(--muted);">Card <strong class="mono-cell">${escapeHtml(card.card_id)}</strong> · Balance to return: <strong>${formatMoney(card.credit)}</strong></p>
    <div class="form-row">
      <label for="checkout-comments-confirm">Operator comments (optional)</label>
      <input type="text" id="checkout-comments-confirm" placeholder="e.g. Guest closing visit" autocomplete="off" />
    </div>`;
  modalFooter.innerHTML = `<button type="button" class="btn btn-secondary" data-close id="checkout-cancel">Cancel</button><button type="button" class="btn btn-primary" id="checkout-confirm-btn">Confirm</button>`;

  const finalize = () => {
    const comments = document.getElementById("checkout-comments-confirm")?.value.trim() || "";
    const remaining = Math.round(card.credit * 100) / 100;
    const txId = generateCheckoutTxId();
    const checkout_time = new Date().toISOString();
    const record = {
      transaction_id: txId,
      remaining_credit: remaining,
      checkout_time,
      operator_comments: comments || undefined,
    };
    if (!Array.isArray(card.checkout_records)) card.checkout_records = [];
    card.checkout_records.push(record);
    card.remaining_credit = remaining;
    card.credit = 0;
    card.status = "CheckedOut";
    card.last_used_at = checkout_time;
    persistCards();
    showToast(`Checked out ${card.card_id}. Returned ${formatMoney(remaining)} to guest. (${txId})`);
    document.getElementById("checkout-card-id").value = "";
    renderSummary();
    renderTable();
    closeModal();
  };

  modalFooter.querySelector("#checkout-confirm-btn").addEventListener("click", finalize);
  modalFooter.querySelectorAll("[data-close]").forEach((b) => b.addEventListener("click", closeModal));
}

function startCheckoutFromInput() {
  const raw = document.getElementById("checkout-card-id")?.value.trim() || "";
  if (!raw) {
    showToast("Enter or scan a card ID");
    return;
  }
  const card = cards.find((c) => c.card_id === raw);
  if (!card) {
    showToast(`No card found: ${raw}`);
    return;
  }
  if (card.status === "CheckedOut") {
    showToast("This card is already checked out");
    return;
  }
  if (card.status !== "Active") {
    showToast("Only active cards can be checked out. Use filters to review inactive cards.");
    return;
  }
  openCheckoutConfirmModal(card);
}

function initCardsUi() {
  backdrop.addEventListener("click", (e) => {
    if (e.target === backdrop) closeModal();
  });

  document.getElementById("modal-close-btn").addEventListener("click", closeModal);

  document.getElementById("search-card").addEventListener("input", () => {
    renderTable();
  });

  document.getElementById("filter-status").addEventListener("change", () => {
    renderTable();
  });

  document.getElementById("register-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const fd = new FormData(e.target);
    let card_id = String(fd.get("card_id") || "").trim();
    const owner = String(fd.get("owner") || "").trim();
    const credit = parseFloat(String(fd.get("credit") || "0"));
    if (!card_id) {
      showToast("Enter a card ID");
      return;
    }
    const existing = cards.find((c) => c.card_id === card_id);
    if (existing) {
      if (existing.status === "CheckedOut") {
        showToast("This card ID is checked out. Use a new card ID when reissuing.");
      } else {
        showToast("Card ID already exists in the registry");
      }
      return;
    }
    const now = new Date().toISOString();
    cards.unshift({
      card_id,
      owner,
      credit: Math.max(0, credit),
      status: "Active",
      created_at: now,
      last_used_at: now,
      checkout_records: [],
    });
    persistCards();
    showToast(`Registered ${card_id} with ${formatMoney(Math.max(0, credit))}`);
    e.target.reset();
    renderSummary();
    renderTable();
  });

  document.getElementById("btn-checkout-start").addEventListener("click", startCheckoutFromInput);

  document.getElementById("checkout-card-id").addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      startCheckoutFromInput();
    }
  });

  document.getElementById("btn-scroll-add-card")?.addEventListener("click", () => {
    document.getElementById("add-card-panel")?.scrollIntoView({ behavior: "smooth", block: "start" });
    setTimeout(() => document.getElementById("reg-card-id")?.focus(), 400);
  });
}

requireAuth();
const authSession = getSession();
initShell("cards.html");
initClock();
initMobileNav();

if (!authSession || !canAccessPage(authSession, "cards.html")) {
  const main = document.querySelector("main.content");
  if (main) {
    main.innerHTML = `
      <h1 class="page-title">Access restricted</h1>
      <p class="page-desc">RFID card management is limited to park administrators in this mockup.</p>
      <div class="card" style="max-width:32rem">
        <div class="card-body">
          <p class="muted" style="margin:0">Your role can use the <strong>Transactions</strong> log for reporting. Contact an administrator if you need card desk access.</p>
          <p style="margin:1rem 0 0"><a class="btn btn-primary" href="index.html">Back to dashboard</a></p>
        </div>
      </div>`;
  }
} else {
  cardsManage = can(authSession, PERM.CARDS_MANAGE);
  const managePanels = document.getElementById("cards-manage-panels");
  const hint = document.getElementById("reissue-hint");
  if (managePanels) managePanels.hidden = !cardsManage;
  if (hint && !cardsManage) hint.hidden = true;
  initCardsUi();
  renderSummary();
  renderTable();
}
