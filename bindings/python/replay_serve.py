"""Tiny HTTP server that hosts the leek-wars-client static build and
reverse-proxies /image /sound /font /font/* requests to leekwars.com.

This is the local replacement for `npm run dev` -- the built client is
~200 small files, the standard library can serve it just fine, and we
avoid the Vite dev-server quirks that make module loading hang.

Usage:
    python replay_serve.py [--port 8080] [--dist <path>]
        [--report-source <fight.json>]

If --report-source is given, it is copied into <dist>/static/report.json
on every request to /static/report.json (so you can keep regenerating
fights without redeploying the dist/).

Open http://localhost:8080/fight/local in the browser.
"""
from __future__ import annotations

import argparse
import http.server
import json
import os
import shutil
import socket
import sys
import threading
import time
import urllib.request
import urllib.error
from urllib.parse import urlparse


# Folders proxied to leekwars.com (case sensitive). Anything matching one
# of these prefixes (root segment) is fetched from the CDN and streamed
# back to the browser.
PROXY_PREFIXES = ("/image/", "/sound/", "/font/", "/fonts/")
PROXY_TARGET = "https://leekwars.com"

# /api/* requests are stubbed locally -- the client tries to hit a real
# Leek Wars backend on http://localhost:7000/api/ for things like socket
# connect, farmer/get-from-token etc. None of that matters for replaying
# a local report.json, but the failed connections fill the console with
# ERR_CONNECTION_REFUSED and slow down page load.
STUB_API_PREFIX = "/api/"

# In-process cache of upstream proxy responses (path -> (status, headers,
# body)). Two reasons: (1) the dev tools refresh dozens of times in a few
# seconds, and re-fetching from leekwars.com every time was hitting our
# tiny urllib pool with timeouts (502s); (2) the same sprite is requested
# ~50 times per fight. Bounded LRU to keep memory in check.
_CACHE: dict[str, tuple[int, list, bytes]] = {}
_CACHE_LOCK = threading.Lock()
_CACHE_MAX = 512

# SPA fallback: any URL that does NOT match a real file in dist/ and
# does NOT start with these "real" prefixes is rewritten to /index-fr.html
SPA_FALLBACK_INDEX = "/index-fr.html"
REAL_PREFIXES = ("/assets/", "/image/", "/sound/", "/font/", "/fonts/",
                  "/static/", "/manifest.json", "/favicon", "/robots.txt",
                  "/maj.html", "/press-kit/", "/mail/")


class ReplayHandler(http.server.SimpleHTTPRequestHandler):
    """SimpleHTTPRequestHandler with three additions:

      * Reverse-proxy `/image/...`, `/sound/...`, `/font/...` to leekwars.com.
      * SPA fallback: unknown routes serve `index-fr.html`.
      * Optional refresh of `static/report.json` from a source file before
        serving (so regenerating a fight never requires touching dist/).
    """

    server_version = "LeekReplayServe/0.1"
    report_source: str | None = None

    # POST + GET both go through do_GET so the API stub catches them.
    def do_POST(self):
        return self.do_GET()

    def do_GET(self):
        path = urlparse(self.path).path

        # Stub the LeekWars backend API -- replay doesn't need it.
        if path.startswith(STUB_API_PREFIX):
            self._stub_api(path)
            return

        # Refresh the local report.json from --report-source if asked.
        if (self.report_source and
                path == "/static/report.json" and
                os.path.exists(self.report_source)):
            dst = os.path.join(self.directory, "static", "report.json")
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(self.report_source, dst)

        # Reverse-proxy assets.
        if any(path.startswith(p) for p in PROXY_PREFIXES):
            self._proxy(path)
            return

        # Map a real file? Serve it.
        local = os.path.join(self.directory, path.lstrip("/"))
        if os.path.isfile(local) and not local.endswith(("/", "\\")):
            return super().do_GET()

        # SPA fallback for /fight/..., /report/..., etc.
        if not any(path.startswith(p) for p in REAL_PREFIXES):
            self.path = SPA_FALLBACK_INDEX
            return super().do_GET()

        return super().do_GET()

    def _stub_api(self, path: str) -> None:
        """Return a minimal JSON envelope for any LeekWars API call.

        We don't have a backend, but the client doesn't need one for
        replay -- short-circuiting these calls just stops them from
        hammering the console with ERR_CONNECTION_REFUSED.
        """
        body = b'{"success":false,"error":"local_replay_stub"}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        try:
            self.wfile.write(body)
        except Exception:
            pass

    def _serve_with_range(self, body: bytes, headers: list, status: int = 200) -> None:
        """Send a response. For Range requests we slice the body so that
        HTMLAudioElement / HTMLVideoElement get a proper 206 (without it
        their `loadeddata` event never fires for media URLs)."""
        rng = self.headers.get("Range") or self.headers.get("range")
        if rng and rng.startswith("bytes="):
            try:
                spec = rng[len("bytes="):].split(",")[0]
                a, _, b = spec.partition("-")
                if not a and not b:
                    raise ValueError
                start = int(a) if a else 0
                end = int(b) if b else len(body) - 1
                if end >= len(body):
                    end = len(body) - 1
                if start < 0 or start > end:
                    raise ValueError
                slice_ = body[start:end + 1]
                new_headers = [(h, v) for h, v in headers
                               if h.lower() not in ("content-length", "content-range")]
                new_headers.append(("Content-Length", str(len(slice_))))
                new_headers.append(("Content-Range",
                                    f"bytes {start}-{end}/{len(body)}"))
                new_headers.append(("Accept-Ranges", "bytes"))
                try:
                    self.send_response(206)
                    for h, v in new_headers:
                        self.send_header(h, v)
                    self.end_headers()
                    self.wfile.write(slice_)
                except Exception:
                    pass
                return
            except (ValueError, TypeError):
                pass
        # Plain full-body response.
        try:
            self.send_response(status)
            for h, v in headers:
                self.send_header(h, v)
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()
            self.wfile.write(body)
        except Exception:
            pass

    def _proxy(self, path: str) -> None:
        # Cache hit?
        with _CACHE_LOCK:
            cached = _CACHE.get(path)
        if cached is not None:
            status, headers, body = cached
            self._serve_with_range(body, headers, status)
            return

        url = PROXY_TARGET + path
        # Up to 3 attempts with backoff -- transient timeouts under load
        # shouldn't be visible to the browser as 502s.
        for attempt in range(3):
            try:
                req = urllib.request.Request(url, headers={
                    "User-Agent": "LeekReplayServe/0.1",
                    "Accept": "*/*",
                })
                with urllib.request.urlopen(req, timeout=15) as resp:
                    body = resp.read()
                    headers = []
                    for h, v in resp.headers.items():
                        if h.lower() in ("transfer-encoding", "connection",
                                          "keep-alive", "content-encoding",
                                          "content-length"):
                            continue
                        headers.append((h, v))
                    headers.append(("Content-Length", str(len(body))))
                    headers.append(("Cache-Control", "public, max-age=86400"))
                    status = resp.status
                    with _CACHE_LOCK:
                        if len(_CACHE) >= _CACHE_MAX:
                            # Drop one arbitrary entry (FIFO-ish).
                            _CACHE.pop(next(iter(_CACHE)))
                        _CACHE[path] = (status, headers, body)
                    self._serve_with_range(body, headers, status)
                    return
            except urllib.error.HTTPError as e:
                # Real upstream 4xx -- forward it, don't retry.
                try:
                    self.send_response(e.code)
                    self.end_headers()
                except Exception:
                    pass
                return
            except (urllib.error.URLError, socket.timeout, TimeoutError):
                if attempt < 2:
                    time.sleep(0.4 * (attempt + 1))
                    continue
                # Final attempt failed -- return 200 with empty body so
                # the client's onerror branch isn't triggered (which can
                # leave the texture loader hung).
                try:
                    self.send_response(200)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                except Exception:
                    pass
                return
            except Exception:
                # Any other surprise -- treat as empty 200.
                try:
                    self.send_response(200)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                except Exception:
                    pass
                return

    # Quieter logging.
    def log_message(self, format, *args):
        # Drop the noisy default access log; keep only warnings/errors.
        pass


class _Handler(ReplayHandler):
    pass


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8080)
    p.add_argument(
        "--dist",
        default="C:/Users/aurel/Desktop/Training Weights Leekwars/"
                "Leekwars-Tools/leek-wars/dist",
        help="path to the built leek-wars-client (dist/) folder",
    )
    p.add_argument(
        "--report-source",
        default=None,
        help="if given, /static/report.json is copied from this file on "
             "every request (so regenerating a fight is always live)",
    )
    args = p.parse_args()

    dist = os.path.abspath(args.dist)
    if not os.path.isdir(dist):
        sys.exit(f"--dist not found: {dist}")

    _Handler.report_source = args.report_source
    # SimpleHTTPRequestHandler has a `directory` attribute since 3.7.
    server = http.server.ThreadingHTTPServer(
        ("0.0.0.0", args.port),
        lambda *a, **k: _Handler(*a, directory=dist, **k),
    )

    print(f"  serving {dist}")
    if args.report_source:
        print(f"  refreshing /static/report.json from {args.report_source}")
    print(f"  open    http://localhost:{args.port}/fight/local")
    print()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
