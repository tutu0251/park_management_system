import { requireAuth, getSession } from "./auth-session.js";
import { canAccessPage } from "./rbac.js";
import { initShell } from "./shell.js";
import { initClock, initMobileNav } from "./common.js";

requireAuth();
const session = getSession();
initShell("permissions.html");
initClock();
initMobileNav();

function deny() {
  const main = document.querySelector("main.content");
  if (!main) return;
  main.innerHTML = `
    <h1 class="page-title">Access restricted</h1>
    <p class="page-desc">Role definitions are visible to administrators only in this mockup.</p>
    <div class="card" style="max-width:32rem">
      <div class="card-body">
        <p class="muted" style="margin:0">Use the dashboard or transaction log for your permitted areas.</p>
        <p style="margin:1rem 0 0"><a class="btn btn-primary" href="index.html">Back to dashboard</a></p>
      </div>
    </div>`;
}

if (!session || !canAccessPage(session, "permissions.html")) {
  deny();
}
