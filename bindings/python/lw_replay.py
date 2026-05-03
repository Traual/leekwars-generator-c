"""lw-replay -- one-shot CLI to generate a fight via the C engine and
serve it through the local leek-wars-client viewer.

Usage:
    python lw_replay.py [--seed N] [--turns N] [--port 8080]

This is the convenience wrapper around:
    1. replay_demo.py     -- runs a 1v1 fight in the C engine, dumps
                              report.json into the leek-wars-client
                              public/static/ folder.
    2. replay_serve.py    -- serves the leek-wars-client dist/ over
                              http://localhost:<port> with a transparent
                              reverse-proxy to leekwars.com for sprites
                              and sounds.
    3. opens the browser to the replay URL.
"""
from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import threading
import time
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
LW = "C:/Users/aurel/Desktop/Training Weights Leekwars/Leekwars-Tools/leek-wars"
DIST = os.path.join(LW, "dist")
REPORT = os.path.join(LW, "public", "static", "report.json")


def _free_port(port: int) -> bool:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind(("0.0.0.0", port))
        s.close()
        return True
    except OSError:
        return False


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seed", type=int, default=1, help="fight RNG seed")
    p.add_argument("--turns", type=int, default=20, help="max turns")
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--no-browser", action="store_true",
                    help="don't open the browser automatically")
    p.add_argument("--keep", action="store_true",
                    help="keep the server running until Ctrl-C "
                         "(default: same)")
    args = p.parse_args()

    # 1. Generate the fight.
    print(f"[1/3] generating fight (seed={args.seed}, turns={args.turns})...")
    r = subprocess.run(
        [sys.executable, os.path.join(HERE, "replay_demo.py"),
         "--seed", str(args.seed),
         "--turns", str(args.turns),
         "--out", REPORT],
        check=True,
    )
    if r.returncode != 0:
        sys.exit(r.returncode)

    # 2. Make sure the port is free + verify the built client exists.
    if not os.path.isdir(DIST):
        sys.exit(
            f"[!] {DIST} does not exist. Build the client first:\n"
            f"    cd \"{LW}\"\n"
            f"    npm run build\n"
        )
    if not _free_port(args.port):
        sys.exit(
            f"[!] port {args.port} is in use. Stop whatever's listening,\n"
            f"    or pick another port with --port.\n"
        )

    # 3. Spawn the server in a background thread.
    print(f"[2/3] starting replay server on http://localhost:{args.port}/...")
    server_proc = subprocess.Popen(
        [sys.executable, os.path.join(HERE, "replay_serve.py"),
         "--port", str(args.port),
         "--dist", DIST,
         "--report-source", REPORT],
    )

    # Give the server a moment to bind.
    time.sleep(0.5)

    # 4. Open the browser.
    url = f"http://localhost:{args.port}/fight/local"
    print(f"[3/3] opening {url}")
    if not args.no_browser:
        webbrowser.open(url)

    print()
    print(f"  Replay URL: {url}")
    print( "  Press Ctrl-C to stop the server.")
    try:
        server_proc.wait()
    except KeyboardInterrupt:
        print("\nstopping server...")
        server_proc.terminate()
        try:
            server_proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server_proc.kill()


if __name__ == "__main__":
    main()
