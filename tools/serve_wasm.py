#!/usr/bin/env python3
"""Minimal static server with COOP/COEP for Emscripten pthreads."""

from __future__ import annotations

import argparse
import functools
import http.server
import os
import socketserver


class CoopCoepHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **getattr(http.server.SimpleHTTPRequestHandler, "extensions_map", {}),
        ".wasm": "application/wasm",
        ".js": "text/javascript",
        ".mjs": "text/javascript",
        ".html": "text/html",
        ".json": "application/json",
    }

    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # Required so pthread Workers / .wasm can load under COEP require-corp.
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", help="Directory to serve")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    os.chdir(args.directory)
    handler = functools.partial(CoopCoepHandler, directory=os.getcwd())
    with socketserver.ThreadingTCPServer(("127.0.0.1", args.port), handler) as httpd:
        print(f"Serving {os.getcwd()} on http://127.0.0.1:{args.port}/")
        print("COOP=same-origin COEP=require-corp")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
