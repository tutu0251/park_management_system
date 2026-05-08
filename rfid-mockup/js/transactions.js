import { MOCK_TRANSACTIONS, MOCK_CARDS, MOCK_MACHINES } from "./mock-data.js";
import {
  initClock,
  initMobileNav,
  showToast,
  formatMoney,
  formatDateTime,
  eventBadgeClass,
} from "./common.js";
import { requireAuth, getSession } from "./auth-session.js";
import { can, PERM } from "./rbac.js";
import { initShell } from "./shell.js";

const transactions = [...MOCK_TRANSACTIONS];

function escapeHtml(s) {
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

let filtered = [...transactions];

function applyFilters() {
  const from = document.getElementById("f-from").value;
  const to = document.getElementById("f-to").value;
  const cardId = document.getElementById("f-card").value.trim().toLowerCase();
  const readerId = document.getElementById("f-reader").value.trim().toLowerCase();
  const gameId = document.getElementById("f-game").value.trim().toLowerCase();
  const eventType = document.getElementById("f-event").value;
  const location = document.getElementById("f-location").value;

  const fromT = from ? new Date(`${from}T00:00:00`).getTime() : null;
  const toT = to ? new Date(`${to}T23:59:59.999`).getTime() : null;

  filtered = transactions.filter((t) => {
    const ts = new Date(t.timestamp).getTime();
    if (fromT !== null && ts < fromT) return false;
    if (toT !== null && ts > toT) return false;
    if (cardId && !t.card_id.toLowerCase().includes(cardId)) return false;
    if (readerId && !t.reader_id.toLowerCase().includes(readerId)) return false;
    if (gameId && !t.game_id.toLowerCase().includes(gameId)) return false;
    if (eventType && t.event_type !== eventType) return false;
    if (location && t.location !== location) return false;
    return true;
  });
  renderTable();
}

function renderTable() {
  const tbody = document.querySelector("#tx-table tbody");
  tbody.innerHTML = filtered
    .map(
      (t) => `
    <tr class="clickable" role="button" tabindex="0" data-tx-id="${escapeHtml(t.transaction_id)}">
      <td class="mono-cell">${formatDateTime(t.timestamp)}</td>
      <td class="mono-cell">${escapeHtml(t.transaction_id)}</td>
      <td class="mono-cell">${escapeHtml(t.node_id)}</td>
      <td class="mono-cell">${escapeHtml(t.reader_id)}</td>
      <td class="mono-cell">${escapeHtml(t.card_id)}</td>
      <td class="mono-cell">${escapeHtml(t.game_id)}</td>
      <td>${escapeHtml(t.game_name)}</td>
      <td><span class="badge ${eventBadgeClass(t.event_type)}">${escapeHtml(t.event_type)}</span></td>
      <td class="text-right">${formatMoney(t.credit_before)}</td>
      <td class="text-right">${formatMoney(t.cost)}</td>
      <td class="text-right">${formatMoney(t.credit_after)}</td>
      <td>${escapeHtml(t.location)}</td>
    </tr>`
    )
    .join("");

  tbody.querySelectorAll("tr").forEach((tr) => {
    const open = () => {
      const id = tr.dataset.txId;
      const tx = transactions.find((x) => x.transaction_id === id);
      if (tx) openDrawer(tx);
    };
    tr.addEventListener("click", open);
    tr.addEventListener("keydown", (e) => {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        open();
      }
    });
  });
}

const drawerBackdrop = document.getElementById("drawer-backdrop");
const drawer = document.getElementById("tx-drawer");

function closeDrawer() {
  drawer.classList.remove("open");
  drawerBackdrop.classList.remove("open");
}

function openDrawer(tx) {
  drawer.querySelector("#drawer-title").textContent = tx.transaction_id;
  drawer.querySelector("#drawer-body").innerHTML = `
    <dl class="dl-detail">
      <dt>Timestamp</dt><dd>${formatDateTime(tx.timestamp)}</dd>
      <dt>Event type</dt><dd><span class="badge ${eventBadgeClass(tx.event_type)}">${escapeHtml(tx.event_type)}</span></dd>
      <dt>Location</dt><dd>${escapeHtml(tx.location)}</dd>
      <dt>Node</dt><dd class="mono-cell">${escapeHtml(tx.node_id)}</dd>
      <dt>Reader</dt><dd class="mono-cell">${escapeHtml(tx.reader_id)}</dd>
      <dt>Card</dt><dd class="mono-cell">${escapeHtml(tx.card_id)}</dd>
      <dt>Game</dt><dd>${escapeHtml(tx.game_name)} (${escapeHtml(tx.game_id)})</dd>
      <dt>Credit before</dt><dd>${formatMoney(tx.credit_before)}</dd>
      <dt>Cost</dt><dd>${formatMoney(tx.cost)}</dd>
      <dt>Credit after</dt><dd>${formatMoney(tx.credit_after)}</dd>
      <dt>Notes</dt><dd>${escapeHtml(tx.detail_note)}</dd>
    </dl>`;
  drawer.classList.add("open");
  drawerBackdrop.classList.add("open");
}

function wireManualTx() {
  document.getElementById("btn-manual-tx")?.addEventListener("click", () => {
    const card_id = document.getElementById("mt-card")?.value.trim() || "";
    const game_name = document.getElementById("mt-game")?.value.trim() || "Manual adjustment";
    const cost = parseFloat(String(document.getElementById("mt-cost")?.value || "0"));
    if (!card_id) {
      showToast("Enter a card ID");
      return;
    }
    const m = MOCK_MACHINES[0];
    const now = new Date();
    const nowIso = now.toISOString();
    const y = now.getFullYear();
    const mo = String(now.getMonth() + 1).padStart(2, "0");
    const day = String(now.getDate()).padStart(2, "0");
    const h = String(now.getHours()).padStart(2, "0");
    const min = String(now.getMinutes()).padStart(2, "0");
    const r = String(Math.floor(Math.random() * 900) + 100);
    const id = `TX-MAN-${y}${mo}${day}-${h}${min}-${r}`;
    const credit_before = 40;
    const credit_after = Math.max(0, Math.round((credit_before - cost) * 100) / 100);
    transactions.unshift({
      timestamp: nowIso,
      transaction_id: id,
      node_id: m.node_id,
      reader_id: m.reader_id,
      card_id,
      game_id: "MANUAL",
      game_name,
      event_type: "Success",
      credit_before,
      cost: Math.max(0, cost),
      credit_after,
      location: m.location,
      detail_note: "Manual entry (mock POST /api/transactions)",
    });
    filtered = [...transactions];
    showToast(`Created ${id}`);
    renderTable();
  });
}

function initTxUi() {
  document.getElementById("drawer-close").addEventListener("click", closeDrawer);
  drawerBackdrop.addEventListener("click", closeDrawer);

  document.getElementById("btn-filter").addEventListener("click", applyFilters);
  document.getElementById("btn-reset-filter").addEventListener("click", () => {
    document.getElementById("f-from").value = "";
    document.getElementById("f-to").value = "";
    document.getElementById("f-card").value = "";
    document.getElementById("f-reader").value = "";
    document.getElementById("f-game").value = "";
    document.getElementById("f-event").value = "";
    document.getElementById("f-location").value = "";
    filtered = [...transactions];
    renderTable();
  });

  document.getElementById("export-csv").addEventListener("click", () => {
    showToast("Export CSV — mock (no file generated)");
  });
  document.getElementById("export-xlsx").addEventListener("click", () => {
    showToast("Export Excel — mock (no file generated)");
  });
}

/** Populate filter dropdown helpers from data */
function seedFilters() {
  const uniq = (arr) => [...new Set(arr)].sort();
  const cards = uniq(MOCK_CARDS.map((c) => c.card_id));
  const readers = uniq(MOCK_MACHINES.map((m) => m.reader_id));
  const games = uniq(MOCK_MACHINES.map((m) => m.game_id));
  const dlCard = document.getElementById("dl-cards");
  const dlReader = document.getElementById("dl-readers");
  const dlGame = document.getElementById("dl-games");
  if (dlCard) dlCard.innerHTML = cards.map((c) => `<option value="${escapeHtml(c)}">`).join("");
  if (dlReader) dlReader.innerHTML = readers.map((c) => `<option value="${escapeHtml(c)}">`).join("");
  if (dlGame) dlGame.innerHTML = games.map((c) => `<option value="${escapeHtml(c)}">`).join("");
}

requireAuth();
const txSession = getSession();
initShell("transactions.html");
initClock();
initMobileNav();
initTxUi();

const manualCard = document.getElementById("manual-tx-card");
if (manualCard && txSession && can(txSession, PERM.TRANSACTIONS_MANAGE)) {
  manualCard.hidden = false;
  wireManualTx();
}

seedFilters();
filtered = [...transactions];
renderTable();
