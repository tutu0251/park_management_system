import { MOCK_CARDS, MOCK_MACHINES, MOCK_TRANSACTIONS, LOCATIONS } from "./mock-data.js";
import { initClock, initMobileNav, formatMoney, formatDateTime, eventBadgeClass } from "./common.js";
import { requireAuth } from "./auth-session.js";
import { initShell } from "./shell.js";
import {
  PARK_HOURS,
  buildHourlyTransactionSeries,
  buildZoneActivity,
  buildCategoryRevenue,
  buildMachineStatusDistribution,
  buildLowCreditByLocation,
} from "./dashboard-chart-data.js";

const COL = {
  success: "#067647",
  fail: "#b54708",
  error: "#b42318",
  accent: "#175cd3",
  online: "#067647",
  offline: "#98a2b3",
  machineError: "#b42318",
  maintenance: "#ea580c",
  zone: ["#175cd3", "#0ba5ec", "#6938ef", "#f79009", "#12b76a", "#667085"],
};

function todayBounds() {
  const start = new Date();
  start.setHours(0, 0, 0, 0);
  const end = new Date(start);
  end.setDate(end.getDate() + 1);
  return { start, end };
}

function aggregate() {
  const { start, end } = todayBounds();
  const activeCards = MOCK_CARDS.filter((c) => c.status === "Active").length;
  const txsToday = MOCK_TRANSACTIONS.filter((t) => {
    const d = new Date(t.timestamp);
    return d >= start && d < end;
  });
  const online = MOCK_MACHINES.filter((m) => m.status === "Online").length;
  const offline = MOCK_MACHINES.filter((m) => m.status === "Offline").length;

  const zoneStats = LOCATIONS.map((loc) => {
    const machines = MOCK_MACHINES.filter((m) => m.location === loc);
    const plays = txsToday.filter((t) => t.location === loc && t.event_type === "Success").length;
    const errs = txsToday.filter((t) => t.location === loc && t.event_type === "Error").length;
    return { loc, machines: machines.length, plays, errs };
  });

  const alerts = [];
  MOCK_MACHINES.filter((m) => m.status === "Offline").forEach((m) => {
    alerts.push({
      type: "warning",
      title: `Reader offline: ${m.reader_id}`,
      meta: `${m.game_name} · ${m.location}`,
    });
  });
  MOCK_MACHINES.filter((m) => m.status === "Error").forEach((m) => {
    alerts.push({
      type: "error",
      title: `Hardware fault: ${m.reader_id}`,
      meta: `${m.game_name} · ${m.location}`,
    });
  });
  txsToday
    .filter((t) => t.event_type === "Fail")
    .slice(0, 3)
    .forEach((t) => {
      alerts.push({
        type: "warning",
        title: "Low balance swipe attempt",
        meta: `${t.card_id.slice(0, 14)}… · ${t.game_name}`,
      });
    });

  const recent = MOCK_TRANSACTIONS.slice(0, 8);

  return {
    activeCards,
    online,
    offline,
    zoneStats,
    alerts,
    recent,
  };
}

function escapeHtml(s) {
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

function formatHourRangeLabel(open, close) {
  const fmt = (h) => {
    const ampm = h >= 12 ? "PM" : "AM";
    const hr12 = h % 12 === 0 ? 12 : h % 12;
    return `${hr12} ${ampm}`;
  };
  return `Park hours ${fmt(open)} – ${fmt(close)}`;
}

function chartTooltipMoney() {
  return {
    callbacks: {
      label(ctx) {
        const v = ctx.raw;
        if (typeof v !== "number") return `${ctx.dataset.label || ""}: ${v}`;
        return `${ctx.dataset.label || "Revenue"}: ${formatMoney(v)}`;
      },
    },
  };
}

function initCharts(hourly, zone, category, machine, lowCredit) {
  const Chart = window.Chart;
  if (!Chart) {
    console.warn("Chart.js not loaded — dashboard charts skipped.");
    return;
  }

  /** Destroy existing Chart.js instance so the canvas can be reused (avoids "canvas already in use"). */
  function chartCanvas(id) {
    const el = document.getElementById(id);
    if (!el) return null;
    const existing = typeof Chart.getChart === "function" ? Chart.getChart(el) : null;
    if (existing) existing.destroy();
    return el;
  }

  Chart.defaults.font.family = '"Segoe UI", system-ui, -apple-system, sans-serif';
  Chart.defaults.color = "#475467";

  const commonResponsive = {
    responsive: true,
    maintainAspectRatio: false,
  };

  const gridColor = "rgba(16, 24, 40, 0.06)";

  const cTrend = chartCanvas("chart-tx-trend");
  if (cTrend)
    new Chart(cTrend, {
    type: "bar",
    data: {
      labels: hourly.labels,
      datasets: [
        {
          label: "Success",
          data: hourly.success,
          backgroundColor: COL.success,
          borderRadius: 4,
          stack: "tx",
        },
        {
          label: "Fail",
          data: hourly.fail,
          backgroundColor: COL.fail,
          borderRadius: 4,
          stack: "tx",
        },
        {
          label: "Error",
          data: hourly.error,
          backgroundColor: COL.error,
          borderRadius: 4,
          stack: "tx",
        },
      ],
    },
    options: {
      ...commonResponsive,
      plugins: {
        legend: { position: "bottom", labels: { boxWidth: 10, usePointStyle: true, padding: 16 } },
        tooltip: { mode: "index", intersect: false },
      },
      scales: {
        x: {
          stacked: true,
          grid: { display: false },
          ticks: { maxRotation: 45, minRotation: 0, autoSkip: true, maxTicksLimit: 14 },
        },
        y: {
          stacked: true,
          beginAtZero: true,
          ticks: { precision: 0 },
          border: { display: false },
          grid: { color: gridColor },
        },
      },
    },
  });

  const cRev = chartCanvas("chart-revenue-hour");
  if (cRev)
    new Chart(cRev, {
    type: "bar",
    data: {
      labels: hourly.labels,
      datasets: [
        {
          label: "Revenue",
          data: hourly.revenue,
          backgroundColor: "rgba(23, 92, 211, 0.88)",
          borderRadius: 5,
          borderSkipped: false,
        },
      ],
    },
    options: {
      ...commonResponsive,
      plugins: {
        legend: { display: false },
        tooltip: chartTooltipMoney(),
      },
      scales: {
        x: { grid: { display: false }, ticks: { maxRotation: 45, minRotation: 0 } },
        y: {
          beginAtZero: true,
          border: { display: false },
          grid: { color: gridColor },
          ticks: {
            callback(value) {
              return "$" + value;
            },
          },
        },
      },
    },
  });

  const zoneColors = zone.counts.map((_, i) => COL.zone[i % COL.zone.length]);
  const cZone = chartCanvas("chart-zone-mix");
  if (cZone)
    new Chart(cZone, {
    type: "doughnut",
    data: {
      labels: zone.zones,
      datasets: [
        {
          data: zone.counts,
          backgroundColor: zoneColors,
          borderWidth: 2,
          borderColor: "#fff",
          hoverOffset: 6,
        },
      ],
    },
    options: {
      ...commonResponsive,
      cutout: "62%",
      plugins: {
        legend: { display: false },
        tooltip: {
          callbacks: {
            label(ctx) {
              const i = ctx.dataIndex;
              return `${ctx.label}: ${zone.counts[i]} (${zone.percents[i]}%)`;
            },
          },
        },
      },
    },
  });

  const machineColors = [COL.online, COL.offline, COL.machineError, COL.maintenance];
  const cMach = chartCanvas("chart-machine-status");
  if (cMach)
    new Chart(cMach, {
    type: "doughnut",
    data: {
      labels: machine.labels,
      datasets: [
        {
          data: machine.values,
          backgroundColor: machineColors,
          borderWidth: 2,
          borderColor: "#fff",
          hoverOffset: 6,
        },
      ],
    },
    options: {
      ...commonResponsive,
      cutout: "62%",
      plugins: { legend: { display: false }       },
    },
  });

  const cLow = chartCanvas("chart-low-credit");
  if (cLow)
    new Chart(cLow, {
    type: "bar",
    data: {
      labels: lowCredit.labels,
      datasets: [
        {
          label: "Low balance fails",
          data: lowCredit.values,
          backgroundColor: "rgba(181, 71, 8, 0.92)",
          borderRadius: 5,
          borderSkipped: false,
        },
      ],
    },
    options: {
      ...commonResponsive,
      indexAxis: "y",
      plugins: {
        legend: { display: false },
        tooltip: { mode: "nearest", intersect: true },
      },
      scales: {
        x: {
          beginAtZero: true,
          ticks: { precision: 0 },
          grid: { color: gridColor },
          border: { display: false },
        },
        y: {
          grid: { display: false },
          border: { display: false },
        },
      },
    },
  });

  const cCat = chartCanvas("chart-category-revenue");
  if (cCat)
    new Chart(cCat, {
    type: "bar",
    data: {
      labels: category.labels,
      datasets: [
        {
          label: "Revenue",
          data: category.values,
          backgroundColor: "rgba(23, 92, 211, 0.82)",
          borderRadius: 5,
          borderSkipped: false,
        },
      ],
    },
    options: {
      ...commonResponsive,
      plugins: {
        legend: { display: false },
        tooltip: chartTooltipMoney(),
      },
      scales: {
        x: {
          grid: { display: false },
          ticks: { maxRotation: 35, minRotation: 0, autoSkip: false },
        },
        y: {
          beginAtZero: true,
          border: { display: false },
          grid: { color: gridColor },
          ticks: {
            callback(value) {
              return "$" + value;
            },
          },
        },
      },
    },
  });
}

function renderZoneLegend(zone, total) {
  const el = document.getElementById("zone-legend");
  if (!el) return;
  el.innerHTML = zone.zones
    .map(
      (z, i) => `
    <li>
      <span class="swatch" style="background:${COL.zone[i % COL.zone.length]}"></span>
      <span class="name">${escapeHtml(z)}</span>
      <span class="num">${zone.counts[i]} <span style="color:var(--muted);font-weight:500">(${escapeHtml(String(zone.percents[i]))}%)</span></span>
    </li>`
    )
    .join("");
  if (total != null) {
    el.insertAdjacentHTML(
      "beforeend",
      `<li style="border-top:1px solid var(--border);padding-top:0.5rem;margin-top:0.25rem;font-weight:600"><span class="name">Total plays</span><span class="num">${total}</span></li>`
    );
  }
}

function renderMachineLegend(machine) {
  const el = document.getElementById("machine-legend");
  if (!el) return;
  const machineColors = [COL.online, COL.offline, COL.machineError, COL.maintenance];
  const tot = machine.values.reduce((a, b) => a + b, 0);
  el.innerHTML = machine.labels
    .map((lbl, i) => {
      const pct = tot ? Math.round((machine.values[i] / tot) * 1000) / 10 : 0;
      return `
    <li>
      <span class="swatch" style="background:${machineColors[i]}"></span>
      <span class="name">${escapeHtml(lbl)}</span>
      <span class="num">${machine.values[i]} <span style="color:var(--muted);font-weight:500">(${pct}%)</span></span>
    </li>`;
    })
    .join("");
}

function render() {
  const a = aggregate();
  const hourly = buildHourlyTransactionSeries(MOCK_TRANSACTIONS);
  const zone = buildZoneActivity(MOCK_TRANSACTIONS);
  const category = buildCategoryRevenue(MOCK_TRANSACTIONS);
  const machine = buildMachineStatusDistribution(MOCK_MACHINES);
  const lowCredit = buildLowCreditByLocation(MOCK_TRANSACTIONS);

  const ph = PARK_HOURS;
  const lblPh = document.getElementById("park-hours-label");
  if (lblPh) lblPh.textContent = formatHourRangeLabel(ph.open, ph.close);

  const txVol =
    hourly.summary.successfulPlays + hourly.summary.failedAttempts + hourly.summary.errorCount;
  document.getElementById("dpi-cards").textContent = String(a.activeCards);
  document.getElementById("dpi-tx").textContent = String(txVol);
  document.getElementById("dpi-revenue").textContent = formatMoney(hourly.summary.totalRevenue);
  document.getElementById("kpi-online").textContent = String(a.online);
  document.getElementById("kpi-offline").textContent = String(a.offline);

  const sys = document.getElementById("system-status");
  if (sys) {
    const errCount = MOCK_MACHINES.filter((m) => m.status === "Error").length;
    if (errCount > 0 || a.offline > 2) {
      sys.textContent = "Degraded";
      sys.classList.add("degraded");
    } else {
      sys.textContent = "Operational";
      sys.classList.remove("degraded");
    }
  }

  document.getElementById("tx-trend-summary").innerHTML = `
    <div class="chart-stat"><div class="lbl">Peak hour</div><div class="val">${escapeHtml(hourly.summary.peakHourLabel)}</div></div>
    <div class="chart-stat"><div class="lbl">Successful plays</div><div class="val">${hourly.summary.successfulPlays}</div></div>
    <div class="chart-stat"><div class="lbl">Failed attempts</div><div class="val">${hourly.summary.failedAttempts}</div></div>
    <div class="chart-stat"><div class="lbl">Errors</div><div class="val">${hourly.summary.errorCount}</div></div>`;

  const hoursOpen = ph.close - ph.open + 1;
  const avgPerHour = hourly.summary.totalRevenue / hoursOpen;
  document.getElementById("revenue-summary").innerHTML = `
    <div class="chart-stat"><div class="lbl">Total revenue today</div><div class="val">${formatMoney(hourly.summary.totalRevenue)}</div></div>
    <div class="chart-stat"><div class="lbl">Avg revenue / hour</div><div class="val">${formatMoney(avgPerHour)}</div><div class="lbl" style="margin-top:0.35rem;text-transform:none;letter-spacing:0;color:var(--muted);font-weight:400;font-size:0.6875rem;">Across ${hoursOpen}h park window</div></div>`;

  const zoneEl = document.getElementById("zone-summary");
  zoneEl.innerHTML = a.zoneStats
    .map(
      (z) => `
    <div class="zone-row">
      <span class="zone-name">${escapeHtml(z.loc)}</span>
      <span class="zone-stats">${z.machines} machines · ${z.plays} plays · ${z.errs} errors today</span>
    </div>`
    )
    .join("");

  const alertsEl = document.getElementById("alert-cards");
  if (a.alerts.length === 0) {
    alertsEl.innerHTML = `<div class="alert-item info"><div><div class="alert-title">No critical alerts</div><div class="alert-meta">All monitored readers within normal parameters.</div></div></div>`;
  } else {
    alertsEl.innerHTML = a.alerts
      .slice(0, 6)
      .map(
        (al) => `
      <div class="alert-item ${escapeHtml(al.type)}">
        <div>
          <div class="alert-title">${escapeHtml(al.title)}</div>
          <div class="alert-meta">${escapeHtml(al.meta)}</div>
        </div>
      </div>`
      )
      .join("");
  }

  const tbody = document.querySelector("#recent-tx tbody");
  tbody.innerHTML = a.recent
    .map(
      (t) => `
    <tr>
      <td class="mono-cell">${formatDateTime(t.timestamp)}</td>
      <td class="mono-cell">${escapeHtml(t.transaction_id)}</td>
      <td>${escapeHtml(t.card_id)}</td>
      <td>${escapeHtml(t.game_name)}</td>
      <td><span class="badge ${eventBadgeClass(t.event_type)}">${escapeHtml(t.event_type)}</span></td>
      <td>${escapeHtml(t.location)}</td>
      <td class="text-right">${formatMoney(t.cost)}</td>
    </tr>`
    )
    .join("");

  renderZoneLegend(zone, zone.total);
  renderMachineLegend(machine);

  initCharts(hourly, zone, category, machine, lowCredit);
}

requireAuth();
initShell("index.html");
initClock();
initMobileNav();
render();
