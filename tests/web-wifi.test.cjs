const { test } = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');

function app(fetch, token = '', bootstrap = null) {
  const storage = new Map([['Authorization', token]]);
  const listeners = new Map();
  const bootstrapCalls = [];
  const context = vm.createContext({
    fetch: async (url, options) => {
      if (url === '/api/v1/web/token') {
        bootstrapCalls.push(options);
        return bootstrap ? bootstrap(options) : response(200, {token: token.trim().replace(/^Bearer\s+/i, '') || 'device-token'});
      }
      return fetch(url, options);
    }, AbortController, URL, Headers,
    window: {location: {href: 'http://192.0.2.1/wifi.html', origin: 'http://192.0.2.1'},
      addEventListener: (event, fn) => { if (!listeners.has(event)) listeners.set(event, new Set()); listeners.get(event).add(fn); },
      removeEventListener: (event, fn) => listeners.get(event)?.delete(fn)},
    localStorage: {getItem: k => storage.get(k), setItem: (k, v) => storage.set(k, v), removeItem: k => storage.delete(k)},
    document: {addEventListener() {}},
    setTimeout: (callback, ms) => ms === 1000 ? (queueMicrotask(callback), 0) : setTimeout(callback, ms),
    clearTimeout, setInterval, clearInterval
  });
  for (const file of ['utils.js', 'wifiHandler.js', 'tokenHandler.js', 'logsHandler.js', 'otaUploadHandler.js']) {
    vm.runInContext(fs.readFileSync(path.join(__dirname, '../data/web/js', file), 'utf8'), context);
  }
  return {context, handler: context.wifiHandler(), storage, bootstrapCalls, listeners};
}
const response = (status, data) => ({status, ok: status >= 200 && status < 300, json: async () => data});

test('401 refreshes once automatically, normalizes prefix and never echoes secrets', async () => {
  let header;
  const {handler} = app(async (url, options) => {
    header = options.headers.get('Authorization');
    return response(401, {message: 'private-secret'});
  }, '  Bearer private-secret  ');
  await handler.init();
  assert.equal(header, 'Bearer private-secret');
  assert.equal(handler.authRequired, true);
  assert.match(handler.statusMsg, /Reload/);
  assert.doesNotMatch(handler.statusMsg, /private-secret/);
  assert.equal(handler.connecting, false);
});

test('scan polls pending responses and clears busy state', async () => {
  let count = 0;
  const {handler} = app(async () => ++count < 3 ? response(202, {status: 'scanning'})
    : response(200, [{ssid: 'Network', rssi: -55, enc: 4}]));
  await handler.scan();
  assert.equal(count, 3);
  assert.equal(handler.networks[0].ssid, 'Network');
  assert.equal(handler.scanning, false);
});

test('scan polling is bounded and 409 is informative', async () => {
  let count = 0;
  const pending = app(async () => { count++; return response(202, {status: 'scanning'}); }).handler;
  await pending.scan();
  assert.equal(count, 20);
  assert.match(pending.statusMsg, /Retry/);
  assert.equal(pending.scanning, false);
  const busy = app(async () => response(409, {})).handler;
  await busy.scan();
  assert.match(busy.statusMsg, /connecting/);
  assert.equal(busy.scanning, false);
});

test('save three ordered profiles preserves existing secret and never reconnects', async () => {
  const calls = [];
  const {handler} = app(async (url, options) => {
    calls.push({url, options});
    return response(200, {networks: [{ssid: 'Existing', has_password: true},
      {ssid: 'New', has_password: true}, {ssid: 'Open', has_password: false}]});
  });
  handler.profiles = [
    {ssid: 'Existing', originalSsid: 'Existing', password: '', replacePassword: false},
    {ssid: 'New', originalSsid: null, password: 'private-secret', replacePassword: true},
    {ssid: 'Open', originalSsid: null, password: '', replacePassword: true}
  ];
  await handler.save();
  assert.deepEqual(JSON.parse(calls[0].options.body), {networks: [
    {ssid: 'Existing'}, {ssid: 'New', password: 'private-secret'}, {ssid: 'Open', password: ''}
  ]});
  assert.equal(calls[0].options.method, 'PUT');
  assert.ok(calls.every(c => c.url.endsWith('/networks')));
  assert.ok(handler.profiles.every(p => p.password === ''));
  assert.doesNotMatch(handler.statusMsg, /private-secret/);
});

test('saved connect omits password and 202 releases spinner before Wi-Fi change', async () => {
  let body;
  const {handler} = app(async (url, options) => { body = JSON.parse(options.body); return response(202, {status: 'connecting'}); });
  await handler.connectSaved({ssid: 'Saved', originalSsid: 'Saved', replacePassword: false});
  assert.deepEqual(body, {ssid: 'Saved'});
  assert.equal(handler.connecting, false);
  assert.match(handler.statusMsg, /new IP/);
});

test('token entry validates and stores normalized token without default fallback', async () => {
  let header;
  const {context, storage} = app(async (url, options) => { header = options.headers.Authorization; return response(200, {}); });
  const handler = context.tokenHandler(); handler.token = ' Bearer entered-token ';
  await handler.saveToken();
  assert.equal(header, 'Bearer entered-token');
  assert.equal(storage.get('Authorization'), 'entered-token');
  assert.doesNotMatch(handler.statusMsg, /entered-token/);
});

test('fresh browser shares one bootstrap for concurrent API calls', async () => {
  const headers = [];
  const {context, bootstrapCalls, storage} = app(async (url, options) => {
    headers.push(options.headers.get('Authorization')); return response(200, {});
  });
  await Promise.all([context.apiFetch('/api/v1/wifi/status'), context.apiFetch('/api/v1/wifi/networks')]);
  assert.equal(bootstrapCalls.length, 1);
  assert.equal(bootstrapCalls[0].headers['X-SmallTV-Web'], '1');
  assert.equal(bootstrapCalls[0].cache, 'no-store');
  assert.deepEqual(headers, ['Bearer device-token', 'Bearer device-token']);
  assert.equal(storage.get('Authorization'), 'device-token');
});

test('401 fetches current TV token and retries exactly once', async () => {
  let serial = 0;
  const headers = [];
  const {context, bootstrapCalls} = app(async (url, options) => {
    headers.push(options.headers.get('Authorization'));
    return response(headers.length === 1 ? 401 : 200, {});
  }, '', () => response(200, {token: ++serial === 1 ? 'old-token' : 'new-token'}));
  await context.apiFetch('/api/v1/wifi/status');
  assert.deepEqual(headers, ['Bearer old-token', 'Bearer new-token']);
  assert.equal(bootstrapCalls.length, 2);
});

test('external URLs are rejected before loading or sending a TV token', async () => {
  let requests = 0;
  const {context, bootstrapCalls} = app(async () => { requests++; return response(200, {}); });
  await assert.rejects(context.apiFetch('https://example.com/api'), /origin/);
  assert.equal(requests, 0);
  assert.equal(bootstrapCalls.length, 0);
});

test('token page prefills masked and refreshes when restored from bfcache', async () => {
  let serial = 0;
  const {context, listeners, bootstrapCalls} = app(async () => response(200, {}), '',
    () => response(200, {token: ++serial === 1 ? 'first-token' : 'changed-token'}));
  const handler = context.tokenHandler();
  await handler.init();
  assert.equal(handler.token, 'first-token');
  assert.equal(handler.showToken, false);
  for (const callback of listeners.get('pageshow')) callback({persisted: true});
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(handler.token, 'changed-token');
  assert.equal(bootstrapCalls.length, 2);
  handler.destroy();
  assert.equal(listeners.get('pageshow').size, 1);
});

test('log download loads TV authorization instead of legacy storage key', async () => {
  let header;
  const {context, bootstrapCalls} = app(async (url, options) => {
    assert.equal(url, '/api/v1/logs/download');
    header = options.headers.get('Authorization');
    return response(500, {});
  });
  const handler = context.logsHandler();
  await handler.downloadLogs();
  assert.equal(header, 'Bearer device-token');
  assert.equal(bootstrapCalls.length, 1);
  assert.equal(handler.loading, false);
});

test('OTA obtains fresh authorization before sending once without stored token', async () => {
  const {context, bootstrapCalls} = app(async () => response(200, {}));
  let sent = 0, header, endpoint;
  context.FormData = class { append() {} };
  context.XMLHttpRequest = class {
    constructor() { this.upload = {}; }
    open(method, url) { assert.equal(method, 'POST'); endpoint = url; }
    setRequestHeader(key, value) { header = value; }
    send() { sent++; }
  };
  const handler = context.otaUploadHandler();
  handler.$refs = {fileInput: {files: [{name: 'firmware.bin'}]}};
  await handler.uploadFile();
  assert.equal(endpoint, '/api/v1/ota/fw');
  assert.equal(header, 'Bearer device-token');
  assert.equal(sent, 1);
  assert.equal(bootstrapCalls.length, 1);
});
