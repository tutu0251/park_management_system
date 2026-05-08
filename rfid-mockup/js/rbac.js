/**
 * Role-based access for the mock console (mirrors intended server enforcement).
 */

/** @typedef {"Admin"|"Operator"|"Staff"} AppRole */

export const PERM = {
  DASHBOARD_VIEW: "dashboard.view",
  CARDS_VIEW: "cards.view",
  CARDS_MANAGE: "cards.manage",
  MACHINES_VIEW: "machines.view",
  MACHINES_MONITOR: "machines.monitor",
  MACHINES_ADMIN: "machines.admin",
  TRANSACTIONS_VIEW: "transactions.view",
  TRANSACTIONS_MANAGE: "transactions.manage",
  USERS_MANAGE: "users.manage",
  API_DOCS_VIEW: "api.docs.view",
};

/** @type {Record<AppRole, Set<string>>} */
const ROLE_PERMS = {
  Admin: new Set(Object.values(PERM)),
  Operator: new Set([
    PERM.DASHBOARD_VIEW,
    PERM.MACHINES_VIEW,
    PERM.MACHINES_MONITOR,
    PERM.TRANSACTIONS_VIEW,
    PERM.API_DOCS_VIEW,
  ]),
  Staff: new Set([PERM.DASHBOARD_VIEW, PERM.TRANSACTIONS_VIEW, PERM.API_DOCS_VIEW]),
};

/**
 * @param {{ role: AppRole } | null | undefined} session
 * @param {string} permission
 */
export function can(session, permission) {
  if (!session?.role) return false;
  const set = ROLE_PERMS[session.role];
  return Boolean(set?.has(permission));
}

/**
 * @param {string} htmlFile e.g. "index.html"
 * @returns {string | null} permission required to see nav link, or null if public within app shell
 */
export function navLinkPermission(htmlFile) {
  const map = {
    "index.html": PERM.DASHBOARD_VIEW,
    "cards.html": PERM.CARDS_VIEW,
    "machines.html": PERM.MACHINES_VIEW,
    "transactions.html": PERM.TRANSACTIONS_VIEW,
    "users.html": PERM.USERS_MANAGE,
    "permissions.html": PERM.USERS_MANAGE,
    "api-docs.html": PERM.API_DOCS_VIEW,
  };
  return map[htmlFile] ?? null;
}

/**
 * @param {{ role: AppRole } | null} session
 * @param {string} htmlFile
 */
export function canAccessPage(session, htmlFile) {
  const p = navLinkPermission(htmlFile);
  if (!p) return true;
  return can(session, p);
}
