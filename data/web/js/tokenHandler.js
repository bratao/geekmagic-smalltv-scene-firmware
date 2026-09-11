function tokenHandler() {
  const storageKey = "Authorization";

  return {
    token: "",
    newToken: "",
    statusMsg: "",
    changeStatusMsg: "",
    showToken: false,
    showNewToken: false,
    loading: false,
    hasStoredToken: false,

    async init() {
      this.onPageShow = event => { if (event.persisted) this.loadDeviceToken(); };
      window.addEventListener("pageshow", this.onPageShow);
      await this.loadDeviceToken();
    },

    async loadDeviceToken() {
      this.loading = true;
      this.showToken = false;
      try {
        this.token = await loadWebToken(true);
        this.hasStoredToken = true;
        this.statusMsg = "Current token loaded automatically from the TV.";
      } catch (_) {
        this.statusMsg = "Could not load the TV token. Reload when the device is reachable.";
      } finally { this.loading = false; }
    },

    destroy() {
      window.removeEventListener("pageshow", this.onPageShow);
    },

    async saveToken() {
      const trimmed = normalizeToken(this.token);

      if (!trimmed) {
        this.statusMsg = "Please enter a token.";
        return;
      }

      this.loading = true;
      this.statusMsg = "Checking token...";

      try {
        const res = await fetch("/api/v1/token/check", {
          method: "GET",
          redirect: "error",
          headers: { Authorization: "Bearer " + trimmed },
        });
        if (!res.ok) {
          this.statusMsg = res.status === 401
            ? "Invalid token for this device. Check the token and try again."
            : "Token check failed (HTTP " + res.status + ").";

          return;
        }

        rememberWebToken(trimmed);
        this.token = trimmed;
        this.hasStoredToken = true;
        this.statusMsg = "Token valid and saved.";
      } catch (e) {
        this.statusMsg = "Token check failed.";
      } finally {
        this.loading = false;
      }
    },

    async changeToken() {
      const current = normalizeToken(this.token);
      const next = normalizeToken(this.newToken);

      if (!current) {
        this.changeStatusMsg = "No current token available.";
        this.hasStoredToken = false;
        return;
      }

      if (!next) {
        this.changeStatusMsg = "Please enter a new token.";
        return;
      }

      this.loading = true;
      this.changeStatusMsg = "Saving new token...";

      try {
        const res = await fetch("/api/v1/token/save", {
          method: "POST",
          redirect: "error",
          headers: {
            "Content-Type": "application/json",
            Authorization: "Bearer " + current,
          },
          body: JSON.stringify({ token: next }),
        });

        if (!res.ok) {
          this.changeStatusMsg = res.status === 401
            ? "Current token rejected. Validate the current token first."
            : "Token update failed (HTTP " + res.status + ").";

          return;
        }

        rememberWebToken(next);
        this.token = next;
        this.newToken = "";
        this.hasStoredToken = true;
        this.changeStatusMsg = "Token updated and saved.";
      } catch (e) {
        this.changeStatusMsg = "Token update request failed.";
      } finally {
        this.loading = false;
      }
    },

    clearToken() {
      localStorage.removeItem(storageKey);
      this.token = "";
      this.newToken = "";
      this.hasStoredToken = false;
      this.statusMsg = "Browser entry cleared. The TV token will load automatically on the next request.";
      webToken = "";
    },
  };
}
