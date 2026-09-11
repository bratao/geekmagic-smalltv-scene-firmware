function humanFileSize(bytes) {
  if (bytes === 0) {
    return "0 B";
  }

  const k = 1024;
  const sizes = ["B", "KB", "MB"];
  const i = Math.floor(Math.log(bytes) / Math.log(k));

  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i];
}

function normalizeToken(value) {
  return (value || "").trim().replace(/^Bearer\s+/i, "").trim();
}

let webToken = "";
let webTokenPromise = null;

function rememberWebToken(value) {
  webToken = normalizeToken(value);
  try { localStorage.setItem("Authorization", webToken); } catch (_) {}
  return webToken;
}

function loadWebToken(refresh = false) {
  if (webTokenPromise) return webTokenPromise;
  if (webToken && !refresh) return Promise.resolve(webToken);
  webTokenPromise = (async () => {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 10000);
    try {
      const response = await fetch("/api/v1/web/token", {
        headers: {"X-SmallTV-Web": "1"}, credentials: "same-origin",
        cache: "no-store", redirect: "error", signal: controller.signal
      });
      if (!response.ok) throw new Error("Unable to load device authorization. Reload this page when the TV is reachable.");
      const data = await response.json();
      if (typeof data.token !== "string" || !normalizeToken(data.token)) {
        throw new Error("Device authorization response was invalid. Reload this page.");
      }
      return rememberWebToken(data.token);
    } finally { clearTimeout(timeout); }
  })().finally(() => { webTokenPromise = null; });
  return webTokenPromise;
}

async function apiFetch(url, options = {}) {
  const target = new URL(url, window.location.href);
  if (target.origin !== window.location.origin || target.username || target.password) {
    throw new Error("Device API requests must use this TV's origin.");
  }
  const headers = new Headers(options.headers || {});
  const fetchOptions = {...options, headers, credentials: "same-origin", redirect: "error"};
  let token = await loadWebToken();
  headers.set("Authorization", "Bearer " + token);
  let response = await fetch(url, fetchOptions);
  if (response.status === 401) {
    // Another browser tab may have changed the persisted token. Refresh once.
    token = await loadWebToken(true);
    headers.set("Authorization", "Bearer " + token);
    response = await fetch(url, fetchOptions);
  }
  if (response.status === 401) {
    const error = new Error("Device authorization failed after automatic refresh. Reload the page and retry.");
    error.status = 401;
    throw error;
  }
  return response;
}

window.addEventListener("pageshow", event => {
  if (event.persisted) {
    webToken = "";
    loadWebToken(true).catch(() => {});
  }
});

function includeHTML(id, url, callback) {
  fetch(url)
    .then((response) => response.text())
    .then((data) => {
      document.getElementById(id).innerHTML = data;
      if (typeof callback === "function") callback();
    });
}

function setHeaderTitle(title) {
  const interval = setInterval(() => {
    const h1 = document.getElementById("header-title");
    if (h1) {
      h1.textContent = title;
      clearInterval(interval);
    }
  }, 20);
}

document.addEventListener("DOMContentLoaded", () => {
  if (document.getElementById("header-placeholder")) {
    includeHTML("header-placeholder", "./header.html", () => {
      let pageTitle =
        document.title && document.title.trim()
          ? document.title.trim()
          : "Placeholder Title";
      setHeaderTitle(pageTitle);
    });
  }
  if (document.getElementById("footer-placeholder")) {
    includeHTML("footer-placeholder", "./footer.html");
  }
});
