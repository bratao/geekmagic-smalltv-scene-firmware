function wifiHandler() {
  return {
    profiles: [], networks: [], scanning: false, saving: false, connecting: false,
    statusMsg: '', authRequired: false, scanSequence: 0,
    report(error) {
      this.authRequired = error.status === 401;
      this.statusMsg = this.authRequired
        ? 'Automatic device authorization failed. Reload the page and retry.'
        : (error.name === 'AbortError' ? 'Request timed out. Retry when the TV is reachable.' : error.message);
    },
    async request(path, options = {}) {
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), 10000);
      try {
        const response = await apiFetch('/api/v1/wifi/' + path, {...options, signal: controller.signal});
        if (!response.ok) {
          const error = new Error(response.status === 409
            ? 'Wi-Fi is connecting. Wait before scanning or editing networks.'
            : 'Wi-Fi request failed (HTTP ' + response.status + ').');
          error.status = response.status;
          throw error;
        }
        return {status: response.status, data: await response.json()};
      } finally { clearTimeout(timer); }
    },
    async loadProfiles() {
      const result = await this.request('networks');
      this.profiles = (result.data.networks || []).slice(0, 3).map(n => ({
        ssid: n.ssid, originalSsid: n.ssid, hasPassword: !!n.has_password,
        password: '', replacePassword: false
      }));
    },
    addProfile(ssid = '') {
      if (this.profiles.length >= 3) { this.statusMsg = 'Keep at most three networks.'; return; }
      this.profiles.push({ssid, originalSsid: null, hasPassword: false, password: '', replacePassword: true});
    },
    moveProfile(index, direction) {
      const target = index + direction;
      if (target < 0 || target >= this.profiles.length) return;
      [this.profiles[index], this.profiles[target]] = [this.profiles[target], this.profiles[index]];
    },
    selectNetwork(net) {
      if (this.profiles.some(p => p.ssid === net.ssid)) {
        this.statusMsg = 'This network is already in the list.'; return;
      }
      this.addProfile(net.ssid);
      this.statusMsg = 'Enter its password, then Save networks. Saving does not reconnect.';
    },
    cancelScan() { this.scanSequence++; this.scanning = false; },
    async scan() {
      this.cancelScan();
      const sequence = this.scanSequence;
      this.scanning = true; this.authRequired = false; this.statusMsg = 'Scanning…';
      try {
        for (let attempt = 0; attempt < 20; attempt++) {
          const result = await this.request('scan');
          if (sequence !== this.scanSequence) return;
          if (result.status === 202 || result.data.status === 'scanning') {
            await new Promise(resolve => setTimeout(resolve, 1000));
            continue;
          }
          const entries = Array.isArray(result.data) ? result.data : result.data.networks;
          if (!Array.isArray(entries)) throw new Error('Invalid scan response. Please retry.');
          this.networks = entries.filter(n => n.ssid).map(n => ({
            ssid: n.ssid, rssi: Number(n.rssi) || -100,
            secured: !!n.enc && n.enc !== 0
          })).sort((a, b) => b.rssi - a.rssi);
          this.statusMsg = this.networks.length ? 'Choose a network to add.' : 'No networks found. You can enter an SSID manually.';
          return;
        }
        throw new Error('Scan did not finish. Retry scanning.');
      } catch (error) { if (sequence === this.scanSequence) this.report(error); }
      finally { if (sequence === this.scanSequence) this.scanning = false; }
    },
    async save() {
      this.saving = true; this.authRequired = false;
      try {
        if (this.profiles.length > 3) throw new Error('Keep at most three networks.');
        const seen = new Set();
        const networks = this.profiles.map(p => {
          if (!p.ssid || seen.has(p.ssid)) throw new Error('Enter unique, non-empty network names.');
          seen.add(p.ssid);
          const row = {ssid: p.ssid};
          if (p.originalSsid !== p.ssid || p.replacePassword) row.password = p.password;
          return row;
        });
        await this.request('networks', {method: 'PUT', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({networks})});
        // Never retain typed passwords after a successful save.
        this.profiles.forEach(p => { p.password = ''; p.originalSsid = p.ssid; p.replacePassword = false; });
        this.statusMsg = 'Networks saved in priority order. The current connection is unchanged.';
        await this.loadProfiles();
      } catch (error) { this.report(error); }
      finally { this.saving = false; }
    },
    async connectSaved(profile) {
      if (profile.originalSsid !== profile.ssid || profile.replacePassword) {
        this.statusMsg = 'Save your changes before connecting.'; return;
      }
      this.cancelScan(); this.connecting = true; this.authRequired = false;
      try {
        const result = await this.request('connect', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ssid: profile.ssid})});
        this.statusMsg = result.status === 202 || result.data.status === 'connecting'
          ? 'Connection requested. Wi-Fi may disconnect now. Read the new IP on the TV and open that address. Authorization loads automatically.'
          : 'Connection request accepted. Check the IP on the TV.';
      } catch (error) { this.report(error); }
      finally { this.connecting = false; }
    },
    async init() {
      try {
        const result = await this.request('status');
        this.statusMsg = result.data.connected
          ? 'Connected: ' + result.data.ssid + ' — ' + result.data.ip
          : 'TV access point is available. Select a saved network or add one below.';
        await this.loadProfiles();
      } catch (error) { this.report(error); }
    },
    destroy() { this.cancelScan(); }
  };
}
