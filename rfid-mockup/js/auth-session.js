/**
 * Mock authentication: session in sessionStorage, optional persistence in localStorage.
 */

import { INITIAL_MOCK_USERS, MOCK_USERS_STORAGE_KEY } from "./mock-users.js";

const SESSION_KEY = "park_rfid_auth_session_v1";
const REMEMBER_FLAG = "park_rfid_remember_v1";

/** @typedef {"Admin"|"Operator"|"Staff"} AppRole */

/**
 * @typedef {{
 *   user_id: string,
 *   full_name: string,
 *   email: string,
 *   password: string,
 *   role: AppRole,
 *   status: "Active"|"Inactive",
 *   last_login_at: string | null,
 *   registered_at: string,
 * }} MockUser
 */

/**
 * @typedef {{
 *   userId: string,
 *   email: string,
 *   fullName: string,
 *   role: AppRole,
 *   apiToken: string,
 *   issuedAt: string,
 * }} AuthSession
 */

function storageForSession() {
  try {
    if (localStorage.getItem(REMEMBER_FLAG) === "1") return localStorage;
  } catch {
    /* ignore */
  }
  return sessionStorage;
}

function randomToken() {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export function issueApiToken() {
  return `mock_live_${randomToken()}`;
}

/** @returns {MockUser[]} */
export function loadUsers() {
  try {
    const raw = localStorage.getItem(MOCK_USERS_STORAGE_KEY);
    if (raw) {
      const parsed = JSON.parse(raw);
      if (Array.isArray(parsed) && parsed.length) return parsed;
    }
  } catch {
    /* ignore */
  }
  const copy = INITIAL_MOCK_USERS.map((u) => ({ ...u }));
  saveUsers(copy);
  return copy;
}

/** @param {MockUser[]} users */
export function saveUsers(users) {
  try {
    localStorage.setItem(MOCK_USERS_STORAGE_KEY, JSON.stringify(users));
  } catch {
    /* ignore */
  }
}

/** @returns {AuthSession | null} */
export function getSession() {
  try {
    let raw = storageForSession().getItem(SESSION_KEY);
    if (!raw) raw = sessionStorage.getItem(SESSION_KEY) || localStorage.getItem(SESSION_KEY);
    if (!raw) return null;
    const s = JSON.parse(raw);
    if (!s?.userId || !s?.email || !s?.role || !s?.apiToken) return null;
    return s;
  } catch {
    return null;
  }
}

/** @param {AuthSession | null} session */
export function setSession(session) {
  try {
    sessionStorage.removeItem(SESSION_KEY);
    localStorage.removeItem(SESSION_KEY);
  } catch {
    /* ignore */
  }
  if (!session) return;
  try {
    storageForSession().setItem(SESSION_KEY, JSON.stringify(session));
  } catch {
    sessionStorage.setItem(SESSION_KEY, JSON.stringify(session));
  }
}

export function clearAllAuthStorage() {
  try {
    sessionStorage.removeItem(SESSION_KEY);
    localStorage.removeItem(SESSION_KEY);
    localStorage.removeItem(REMEMBER_FLAG);
  } catch {
    /* ignore */
  }
}

/**
 * @param {boolean} remember
 */
export function setRememberPreference(remember) {
  try {
    if (remember) localStorage.setItem(REMEMBER_FLAG, "1");
    else localStorage.removeItem(REMEMBER_FLAG);
  } catch {
    /* ignore */
  }
}

/**
 * Redirects to login if there is no session (call from every protected page module).
 * @param {string} [loginFileName]
 */
export function requireAuth(loginFileName = "login.html") {
  const path = location.pathname || "";
  const file = path.split("/").pop() || "";
  if (file === loginFileName) return;
  if (!getSession()) {
    const next = encodeURIComponent(file || "index.html");
    location.replace(`${loginFileName}?next=${next}`);
  }
}

/**
 * @param {string} identifier username or email
 * @param {string} password
 * @param {boolean} rememberMe
 * @returns {{ ok: true, session: AuthSession } | { ok: false, error: string }}
 */
export function login(identifier, password, rememberMe) {
  const id = identifier.trim().toLowerCase();
  const users = loadUsers();
  const u = users.find((x) => x.email.toLowerCase() === id || x.full_name.toLowerCase() === id);
  if (!u || u.password !== password) {
    return { ok: false, error: "Incorrect email or password." };
  }
  if (u.status !== "Active") {
    return { ok: false, error: "This account is inactive. Ask an administrator to activate it." };
  }

  setRememberPreference(rememberMe);
  const session = {
    userId: u.user_id,
    email: u.email,
    fullName: u.full_name,
    role: u.role,
    apiToken: issueApiToken(),
    issuedAt: new Date().toISOString(),
  };
  setSession(session);

  u.last_login_at = new Date().toISOString();
  saveUsers(users);

  return { ok: true, session };
}

export function logout() {
  clearAllAuthStorage();
}

/**
 * @param {string} userId
 * @param {string} newPassword
 * @returns {boolean}
 */
export function updatePasswordForUser(userId, newPassword) {
  const users = loadUsers();
  const u = users.find((x) => x.user_id === userId);
  if (!u) return false;
  u.password = newPassword;
  saveUsers(users);
  return true;
}

/**
 * @param {string} email
 * @param {string} newPassword
 */
export function updatePasswordByEmail(email, newPassword) {
  const users = loadUsers();
  const u = users.find((x) => x.email.toLowerCase() === email.trim().toLowerCase());
  if (!u) return false;
  u.password = newPassword;
  saveUsers(users);
  return true;
}
