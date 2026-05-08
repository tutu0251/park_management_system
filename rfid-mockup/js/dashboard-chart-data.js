/**
 * Dashboard chart datasets — mock / derived from sample transactions.
 * Hourly buckets use local time; park hours are configurable.
 */

import { LOCATIONS } from "./constants.js";

export const PARK_HOURS = { open: 9, close: 22 };

/** Map game_id → revenue category for management reporting (mock taxonomy). */
export const GAME_CATEGORY_BY_ID = {
  "G-ARC-01": "Arcade",
  "G-VRT-02": "VR",
  "G-RNG-03": "Prize Games",
  "G-BLW-04": "Prize Games",
  "G-DNC-05": "Arcade",
  "G-SHO-06": "Arcade",
  "G-CLK-07": "Drop Tower",
  "G-KID-08": "Train",
  "G-PROV-TEMP-01": "Arcade",
  "G-FLC-09": "Arcade",
};

/** Categories to always show on chart (even if zero for the day). */
export const GAME_CATEGORY_ORDER = [
  "Arcade",
  "VR",
  "Karting",
  "Train",
  "Drop Tower",
  "Swing Ride",
  "Prize Games",
];

function todayStart() {
  const t = new Date();
  t.setHours(0, 0, 0, 0);
  return t;
}

function isToday(ts) {
  const d = new Date(ts);
  const s = todayStart();
  return d >= s && d < s.getTime() + 86400000;
}

function hourLabels(open, close) {
  const labels = [];
  for (let h = open; h <= close; h++) {
    const ampm = h >= 12 ? "PM" : "AM";
    const hr12 = h % 12 === 0 ? 12 : h % 12;
    labels.push(`${hr12}:00 ${ampm}`);
  }
  return labels;
}

function hourIndex(ts, open, close) {
  const d = new Date(ts);
  const h = d.getHours();
  if (h < open || h > close) return -1;
  return h - open;
}

/**
 * @param {import('./mock-data.js').MOCK_TRANSACTIONS} transactions
 */
export function buildHourlyTransactionSeries(transactions) {
  const { open, close } = PARK_HOURS;
  const n = close - open + 1;
  const success = new Array(n).fill(0);
  const fail = new Array(n).fill(0);
  const error = new Array(n).fill(0);
  const revenue = new Array(n).fill(0);

  for (const t of transactions) {
    if (!isToday(t.timestamp)) continue;
    const idx = hourIndex(t.timestamp, open, close);
    if (idx < 0) continue;
    if (t.event_type === "Success") {
      success[idx]++;
      revenue[idx] += t.cost || 0;
    } else if (t.event_type === "Fail") fail[idx]++;
    else if (t.event_type === "Error") error[idx]++;
  }

  /* Enrich empty dashboards: blend in illustrative hourly curve (still mock). */
  const seedCurve = [2, 4, 7, 11, 14, 18, 22, 20, 16, 12, 9, 6, 4, 3];
  let needsBlend = success.every((v, i) => v + fail[i] + error[i] === 0);
  if (needsBlend) {
    for (let i = 0; i < n; i++) {
      const base = seedCurve[i % seedCurve.length] || 4;
      success[i] += Math.max(0, Math.round(base * 0.65));
      fail[i] += Math.round(base * 0.12);
      error[i] += Math.round(base * 0.05);
      revenue[i] += success[i] * 3.25;
    }
  }

  const labels = hourLabels(open, close);
  const totalsPerHour = success.map((s, i) => s + fail[i] + error[i]);
  let peakIdx = 0;
  for (let i = 1; i < n; i++) {
    if (totalsPerHour[i] > totalsPerHour[peakIdx]) peakIdx = i;
  }

  const totalSuccess = success.reduce((a, b) => a + b, 0);
  const totalFail = fail.reduce((a, b) => a + b, 0);
  const totalError = error.reduce((a, b) => a + b, 0);
  const totalRevenue = revenue.reduce((a, b) => a + b, 0);
  const openHoursCount = totalsPerHour.filter((v) => v > 0).length || 1;

  return {
    labels,
    success,
    fail,
    error,
    revenue,
    summary: {
      peakHourLabel: labels[peakIdx],
      successfulPlays: totalSuccess,
      failedAttempts: totalFail,
      errorCount: totalError,
      totalRevenue,
      avgRevenuePerHour: totalRevenue / (close - open + 1),
      activeHoursForAvg: openHoursCount,
    },
  };
}

/**
 * Zone activity: successful plays per location (Floor 1–3, Outdoor).
 * @param {import('./mock-data.js').MOCK_TRANSACTIONS} transactions
 */
export function buildZoneActivity(transactions) {
  const zones = [...LOCATIONS];
  const counts = Object.fromEntries(zones.map((z) => [z, 0]));
  for (const t of transactions) {
    if (!isToday(t.timestamp)) continue;
    if (t.event_type !== "Success") continue;
    if (counts[t.location] !== undefined) counts[t.location]++;
  }
  /* Illustrative fallback if sparse */
  const sum = Object.values(counts).reduce((a, b) => a + b, 0);
  if (sum === 0) {
    const fb = [42, 58, 36, 51, 44, 2];
    zones.forEach((z, i) => {
      counts[z] = fb[i] ?? 10;
    });
  }
  const totalAfter = zones.reduce((s, z) => s + counts[z], 0);
  const percents = zones.map((z) =>
    totalAfter ? Math.round((counts[z] / totalAfter) * 1000) / 10 : 0
  );
  return { zones, counts: zones.map((z) => counts[z]), percents, total: totalAfter };
}

/**
 * Revenue by category — successful debits today; adds Karting / Swing Ride mock if missing.
 * @param {import('./mock-data.js').MOCK_TRANSACTIONS} transactions
 */
export function buildCategoryRevenue(transactions) {
  const byCat = Object.fromEntries(GAME_CATEGORY_ORDER.map((c) => [c, 0]));
  for (const t of transactions) {
    if (!isToday(t.timestamp)) continue;
    if (t.event_type !== "Success") continue;
    const cat = GAME_CATEGORY_BY_ID[t.game_id] || "Arcade";
    if (byCat[cat] === undefined) byCat[cat] = 0;
    byCat[cat] += t.cost || 0;
  }
  /* Ensure example categories appear with modest mock values */
  if (byCat.Karting === 0) byCat.Karting = 128.5;
  if (byCat["Swing Ride"] === 0) byCat["Swing Ride"] = 96.25;

  const labels = GAME_CATEGORY_ORDER;
  const values = labels.map((c) => Math.round(byCat[c] * 100) / 100);
  return { labels, values };
}

/**
 * Machine status counts from fleet mock.
 * @param {import('./mock-data.js').MOCK_MACHINES} machines
 */
export function buildMachineStatusDistribution(machines) {
  const order = ["Online", "Offline", "Error", "Maintenance"];
  const counts = Object.fromEntries(order.map((k) => [k, 0]));
  for (const m of machines) {
    if (counts[m.status] !== undefined) counts[m.status]++;
  }
  return {
    labels: order,
    values: order.map((k) => counts[k]),
  };
}

/**
 * Low-balance failures (Fail) by location today.
 * @param {import('./mock-data.js').MOCK_TRANSACTIONS} transactions
 */
export function buildLowCreditByLocation(transactions) {
  const zones = [...LOCATIONS];
  const counts = Object.fromEntries(zones.map((z) => [z, 0]));
  for (const t of transactions) {
    if (!isToday(t.timestamp)) continue;
    if (t.event_type !== "Fail") continue;
    if (counts[t.location] !== undefined) counts[t.location]++;
  }
  if (Object.values(counts).every((v) => v === 0)) {
    const fb = [3, 5, 2, 4, 3, 1];
    zones.forEach((z, i) => {
      counts[z] = fb[i] ?? 1;
    });
  }
  return {
    labels: zones,
    values: zones.map((z) => counts[z]),
  };
}
