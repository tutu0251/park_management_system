import { requireAuth, getSession } from "./auth-session.js";
import { initShell } from "./shell.js";
import { initClock, initMobileNav } from "./common.js";

requireAuth();
initShell("api-docs.html");
initClock();
initMobileNav();

const session = getSession();
const tokenEl = document.getElementById("api-token-display");
if (tokenEl && session?.apiToken) {
  tokenEl.textContent = session.apiToken;
}
