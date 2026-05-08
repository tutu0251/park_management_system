/**
 * Sample data for RFID park payment dashboard mockup.
 * Extend or replace when wiring a real backend.
 */

import { LOCATIONS as PARK_LOCATIONS } from "./constants.js";

export const LOCATIONS = PARK_LOCATIONS;

/** Server lookup: node_id → physical location (from spec). */
export const NODE_TO_LOCATION = {
  "NODE-F1-E": "Floor 1",
  "NODE-F1-W": "Floor 1",
  "NODE-F2-N": "Floor 2",
  "NODE-F2-S": "Floor 2",
  "NODE-F3-S": "Floor 3",
  "NODE-OUT-A": "Outdoor Zone A",
  "NODE-OUT-B": "Outdoor Zone B",
  "NODE-UNK-XX": "Unknown Location",
};

/**
 * Registered game types in the mock DB: type_id → display + pricing.
 * cost_per_play is derived from type_id when the type is known.
 */
export const TYPE_CATALOG = {
  "T-ARC-STD": {
    type_name: "Arcade Standard",
    cost_per_play: 3.5,
    category: "Arcade",
    description: "Standard arcade credit debit per play.",
    default_play_duration: 120,
  },
  "T-VR-SESS": {
    type_name: "VR Session",
    cost_per_play: 5.0,
    category: "VR",
    description: "Timed VR booth session.",
    default_play_duration: 300,
  },
  "T-PRI-STD": {
    type_name: "Prize Game Standard",
    cost_per_play: 2.5,
    category: "Prize Games",
    description: "Prize redemption game standard play.",
    default_play_duration: 90,
  },
  "T-TMR-STD": {
    type_name: "Attraction Timer",
    cost_per_play: 4.0,
    category: "Attraction",
    description: "Climbing wall / timed attraction slot.",
    default_play_duration: 180,
  },
  "T-KID-STD": {
    type_name: "Family Ride Credit",
    cost_per_play: 3.0,
    category: "Family",
    description: "Kid-friendly ride debit.",
    default_play_duration: 240,
  },
};

const FIRST_NAMES = [
  "Sarah",
  "James",
  "Mei",
  "David",
  "Emma",
  "Carlos",
  "Yuki",
  "Olivia",
  "Marcus",
  "Priya",
];
const LAST_NAMES = ["Chen", "Nguyen", "Patel", "Kowalski", "Silva", "Park", "Brown", "Mueller"];

function randomPick(arr) {
  return arr[Math.floor(Math.random() * arr.length)];
}

function pad(n) {
  return String(n).padStart(2, "0");
}

/** Generate 20 RFID cards */
export function buildCards() {
  const cards = [];
  const base = Date.now() - 90 * 86400000;
  for (let i = 1; i <= 20; i++) {
    const id = `RFID-${pad(Math.floor(100 + Math.random() * 900))}-${pad(i)}${pad((i * 7) % 100)}`;
    const hasOwner = Math.random() > 0.15;
    const credit = Math.round((15 + Math.random() * 85) * 100) / 100;
    const statusRoll = i === 6 ? "Inactive" : i === 11 ? "Inactive" : i === 3 ? "CheckedOut" : randomPick(["Active", "Active", "Active", "Active"]);
    const status = statusRoll;
    const created = new Date(base + i * 86400000 * 2);
    const lastUsed =
      status === "Active"
        ? new Date(Date.now() - Math.random() * 7200000 - i * 60000)
        : status === "CheckedOut"
          ? new Date(Date.now() - 86400000 * 2)
          : new Date(Date.now() - 86400000 * 3);
    const row = {
      card_id: id,
      owner: hasOwner ? `${randomPick(FIRST_NAMES)} ${randomPick(LAST_NAMES)}` : "",
      credit: status === "CheckedOut" ? 0 : credit,
      status,
      created_at: created.toISOString(),
      last_used_at: lastUsed.toISOString(),
    };
    if (status === "CheckedOut") {
      const rem = Math.round((5 + Math.random() * 40) * 100) / 100;
      row.remaining_credit = rem;
      row.checkout_records = [
        {
          transaction_id: `CO-${new Date(Date.now() - 86400000 * 2).toISOString().slice(0, 10).replace(/-/g, "")}-${pad(i)}${pad(88)}`,
          remaining_credit: rem,
          checkout_time: lastUsed.toISOString(),
          operator_comments: "Guest requested refund at counter",
        },
      ];
    }
    cards.push(row);
  }
  return cards;
}

const now = Date.now();

/**
 * Curated machines for Game Machine Monitoring mockup.
 * @type {Array<{
 *   reader_id: string,
 *   node_id: string,
 *   game_id: string,
 *   game_name: string,
 *   type_id: string,
 *   location: string,
 *   status: "Online"|"Offline"|"Error"|"Maintenance",
 *   last_status_check: string,
 *   last_swipe_at: string | null,
 *   firmware: string,
 *   in_database: boolean,
 *   type_in_catalog: boolean,
 * }>}
 */
export function buildCuratedMachines() {
  return [
    {
      reader_id: "RD-F1-ARC-01",
      node_id: "NODE-F1-E",
      game_id: "G-ARC-01",
      game_name: "Galaxy Racers",
      type_id: "T-ARC-STD",
      location: "Floor 1",
      status: "Online",
      last_status_check: new Date(now - 45_000).toISOString(),
      last_swipe_at: new Date(now - 120_000).toISOString(),
      firmware: "2.4.1",
      in_database: true,
      type_in_catalog: true,
    },
    {
      reader_id: "RD-F2-VRT-02",
      node_id: "NODE-F2-N",
      game_id: "G-VRT-02",
      game_name: "VR Canyon Quest",
      type_id: "T-VR-SESS",
      location: "Floor 2",
      status: "Maintenance",
      last_status_check: new Date(now - 180_000).toISOString(),
      last_swipe_at: new Date(now - 3600_000).toISOString(),
      firmware: "2.4.0",
      in_database: true,
      type_in_catalog: true,
    },
    {
      reader_id: "RD-F3-RNG-03",
      node_id: "NODE-F3-S",
      game_id: "G-RNG-03",
      game_name: "Ring Toss Arena",
      type_id: "T-PRI-STD",
      location: "Floor 3",
      status: "Offline",
      last_status_check: new Date(now - 7200_000).toISOString(),
      last_swipe_at: new Date(now - 86400_000).toISOString(),
      firmware: "2.3.9",
      in_database: true,
      type_in_catalog: true,
    },
    {
      reader_id: "RD-OUT-A-CLK-07",
      node_id: "NODE-OUT-A",
      game_id: "G-CLK-07",
      game_name: "Climbing Wall Timer",
      type_id: "T-TMR-STD",
      location: "Outdoor Zone A",
      status: "Online",
      last_status_check: new Date(now - 30_000).toISOString(),
      last_swipe_at: new Date(now - 600_000).toISOString(),
      firmware: "2.4.1",
      in_database: true,
      type_in_catalog: true,
    },
    {
      reader_id: "RD-OUT-B-KID-08",
      node_id: "NODE-OUT-B",
      game_id: "G-KID-08",
      game_name: "Mini Train Station",
      type_id: "T-KID-STD",
      location: "Outdoor Zone B",
      status: "Error",
      last_status_check: new Date(now - 90_000).toISOString(),
      last_swipe_at: new Date(now - 900_000).toISOString(),
      firmware: "2.3.9",
      in_database: true,
      type_in_catalog: true,
    },
    {
      reader_id: "RD-NEW-PROV-01",
      node_id: "NODE-F1-W",
      game_id: "G-PROV-TEMP-01",
      game_name: "Pop-up Kart (Unregistered)",
      type_id: "T-ARC-STD",
      location: "Floor 1",
      status: "Online",
      last_status_check: new Date(now - 60_000).toISOString(),
      last_swipe_at: new Date(now - 300_000).toISOString(),
      firmware: "2.4.0",
      in_database: false,
      type_in_catalog: true,
    },
    {
      reader_id: "RD-F2-SHO-TBAD",
      node_id: "NODE-F2-S",
      game_id: "G-SHO-06",
      game_name: "Shooting Gallery XL",
      type_id: "T-BAD-TYPE-999",
      cost_per_play: 3.75,
      location: "Floor 2",
      status: "Online",
      last_status_check: new Date(now - 55_000).toISOString(),
      last_swipe_at: new Date(now - 480_000).toISOString(),
      firmware: "2.4.1",
      in_database: true,
      type_in_catalog: false,
    },
    {
      reader_id: "RD-UNK-NODE-01",
      node_id: "NODE-UNK-XX",
      game_id: "G-FLC-09",
      game_name: "Floodlight Court (New Node)",
      type_id: "T-ARC-STD",
      location: "Unknown Location",
      status: "Online",
      last_status_check: new Date(now - 120_000).toISOString(),
      last_swipe_at: new Date(now - 240_000).toISOString(),
      firmware: "2.4.1",
      in_database: false,
      type_in_catalog: true,
    },
  ];
}

/** Generate 30 transactions referencing cards and machines */
export function buildTransactions(cards, machines) {
  const txs = [];
  const todayStart = new Date();
  todayStart.setHours(0, 0, 0, 0);
  for (let i = 1; i <= 30; i++) {
    const card = cards[(i * 3) % cards.length];
    const m = machines[(i * 5) % machines.length];
    const ts = new Date(todayStart.getTime() + ((i * 47) % 86400000) - 3600000 + i * 120000);
    const event_roll = i % 10;
    let event_type =
      event_roll === 7 ? "Fail" : event_roll === 9 ? "Error" : event_roll === 2 ? "Check" : "Success";
    if (m.status === "Offline" && event_type === "Success") event_type = "Error";
    const catalogEntry = TYPE_CATALOG[m.type_id];
    const cost =
      m.type_in_catalog && catalogEntry ? catalogEntry.cost_per_play : typeof m.cost_per_play === "number" ? m.cost_per_play : 3;
    let credit_before = Math.max(cost + 1, card.credit + ((i % 5) - 2) * 2);
    let credit_after = credit_before;
    if (event_type === "Success") credit_after = Math.round((credit_before - cost) * 100) / 100;
    if (event_type === "Fail") credit_after = credit_before;
    if (event_type === "Check") {
      credit_before = card.credit;
      credit_after = card.credit;
    }
    txs.push({
      timestamp: ts.toISOString(),
      transaction_id: `TX-${todayStart.toISOString().slice(0, 10).replace(/-/g, "")}-${pad(i)}${pad((i * 13) % 100)}`,
      node_id: m.node_id,
      reader_id: m.reader_id,
      card_id: card.card_id,
      game_id: m.game_id,
      game_name: m.game_name,
      event_type,
      credit_before,
      cost: event_type === "Check" ? 0 : cost,
      credit_after,
      location: m.location,
      detail_note:
        event_type === "Error"
          ? "Reader timeout / communication fault"
          : event_type === "Fail"
            ? "Insufficient balance at swipe"
            : event_type === "Check"
              ? "Balance inquiry only"
              : "Debit applied successfully",
    });
  }
  txs.sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp));
  return txs;
}

export const MOCK_CARDS = buildCards();
export const MOCK_MACHINES = buildCuratedMachines();
export const MOCK_TRANSACTIONS = buildTransactions(MOCK_CARDS, MOCK_MACHINES);
