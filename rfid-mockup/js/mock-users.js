/**
 * Seed users for the RFID operations mockup (browser localStorage).
 * Passwords are stored in plain text for demo only — never do this in production.
 */

const base = Date.now() - 120 * 86400000;

export const INITIAL_MOCK_USERS = [
  {
    user_id: "usr-admin-1",
    full_name: "Alex Kim",
    email: "admin@riverside.park",
    password: "demo123",
    role: "Admin",
    status: "Active",
    last_login_at: new Date(base + 5 * 86400000).toISOString(),
    registered_at: new Date(base).toISOString(),
  },
  {
    user_id: "usr-op-1",
    full_name: "Jordan Lee",
    email: "operator@riverside.park",
    password: "demo123",
    role: "Operator",
    status: "Active",
    last_login_at: new Date(Date.now() - 3600_000).toISOString(),
    registered_at: new Date(base + 86400000).toISOString(),
  },
  {
    user_id: "usr-staff-1",
    full_name: "Sam Rivera",
    email: "staff@riverside.park",
    password: "demo123",
    role: "Staff",
    status: "Active",
    last_login_at: new Date(Date.now() - 86400_000 * 2).toISOString(),
    registered_at: new Date(base + 2 * 86400000).toISOString(),
  },
  {
    user_id: "usr-pending-1",
    full_name: "Casey Morgan",
    email: "pending@riverside.park",
    password: "demo123",
    role: "Staff",
    status: "Inactive",
    last_login_at: null,
    registered_at: new Date(base + 10 * 86400000).toISOString(),
  },
];

export const MOCK_USERS_STORAGE_KEY = "park_rfid_mock_users_v2";
