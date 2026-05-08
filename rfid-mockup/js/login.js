import { login, getSession } from "./auth-session.js";

function getQueryParam(name) {
  return new URLSearchParams(location.search).get(name);
}

const form = document.getElementById("login-form");
const errEl = document.getElementById("login-error");

if (getSession()) {
  const nextRaw = getQueryParam("next") || "index.html";
  location.replace(decodeURIComponent(nextRaw));
}

form?.addEventListener("submit", (e) => {
  e.preventDefault();
  if (errEl) errEl.textContent = "";
  const fd = new FormData(form);
  const identifier = String(fd.get("identifier") || "");
  const password = String(fd.get("password") || "");
  const remember = fd.get("remember") === "on";

  const result = login(identifier, password, remember);
  if (!result.ok) {
    if (errEl) errEl.textContent = result.error;
    return;
  }
  const nextRaw = getQueryParam("next") || "index.html";
  location.replace(decodeURIComponent(nextRaw));
});
