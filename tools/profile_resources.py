"""Opt-in resource capture; save the JSON report on the PC, never on the TV."""
import argparse
import json
import os
from pathlib import Path
import time
from urllib.request import Request, build_opener, ProxyHandler, HTTPRedirectHandler
from urllib.parse import urlsplit


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True, help='Device HTTP URL, e.g. http://192.168.0.38')
    parser.add_argument('--seconds', type=int, default=60, choices=range(1, 601), metavar='1..600')
    parser.add_argument('--output', type=Path, default=Path('resource-capture.json'))
    args = parser.parse_args()
    target = urlsplit(args.url)
    if target.scheme != 'http' or not target.hostname or target.username or target.password:
        parser.error('Use a device HTTP URL without embedded credentials')
    token = os.environ.get('SMALLTV_TOKEN')
    if not token:
        parser.error('Set SMALLTV_TOKEN in the environment')
    endpoint = args.url.rstrip('/') + '/api/v1/diagnostics/resources'
    opener = build_opener(ProxyHandler({}), NoRedirect())
    def request(method, body=None):
        payload = None if body is None else json.dumps(body).encode()
        req = Request(endpoint, data=payload, method=method,
                      headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'})
        with opener.open(req, timeout=10) as response:
            return json.load(response)
    try:
        request('POST', {'enabled': True, 'duration_s': args.seconds})
        time.sleep(args.seconds + 1)
        result = request('GET')
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('Saved:', args.output.resolve())
        print('Cooperative work:', result['cooperative_work_pct'], '% (not true CPU utilization)')
        print('Pixels written:', result['pixels_written'])
    finally:
        # Auto-expiry also stops capture if the PC disappears or this request fails.
        try:
            request('POST', {'enabled': False})
        except Exception:
            pass

if __name__ == '__main__':
    main()
