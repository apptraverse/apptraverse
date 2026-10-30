#!/usr/bin/env python3
"""Browser smoke for web_minimal_app initial presenter window."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from playwright.sync_api import Error as PlaywrightError
from playwright.sync_api import expect, sync_playwright


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--url", default="http://127.0.0.1:8766/web_minimal_app.html"
    )
    parser.add_argument(
        "--screenshot",
        default="docs/smoke_artifacts/minimal_app_web_initial.png",
    )
    args = parser.parse_args()

    page_errors: list[str] = []
    saw_render_ok = False

    def on_pageerror(err: Exception) -> None:
        page_errors.append(str(err))

    def on_console(msg) -> None:
        nonlocal saw_render_ok
        if "MINIMAL_APP_INITIAL_RENDER_OK" in msg.text:
            saw_render_ok = True
        if msg.type == "error":
            if "Failed to load resource:" in msg.text or "favicon.ico" in msg.text:
                return
            page_errors.append(f"console.error: {msg.text}")

    def on_requestfailed(request) -> None:
        name = request.url.rsplit("/", 1)[-1]
        if name.startswith("web_minimal_app"):
            page_errors.append(
                f"requestfailed: {request.url} ({request.failure})"
            )

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True, channel="chrome")
        context = browser.new_context()
        page = context.new_page()
        page.on("pageerror", on_pageerror)
        page.on("console", on_console)
        page.on("requestfailed", on_requestfailed)

        try:
            page.goto(args.url, wait_until="domcontentloaded", timeout=60000)
            window = page.locator(
                "#app-mount [data-apptraverse-desktop-window='1']"
            )
            expect(window).to_have_count(1, timeout=60000)

            box = window.bounding_box()
            mount = page.locator("#app-mount").bounding_box()
            if box is None or mount is None:
                print("FAIL: missing bounding box", file=sys.stderr)
                return 1

            rel_x = round(box["x"] - mount["x"])
            rel_y = round(box["y"] - mount["y"])
            if rel_x != 120 or rel_y != 80:
                print(
                    f"FAIL: window position ({rel_x},{rel_y}) expected (120,80)",
                    file=sys.stderr,
                )
                return 1
            if round(box["width"]) != 640 or round(box["height"]) != 480:
                print(
                    f"FAIL: client area {box['width']}x{box['height']} expected 640x480",
                    file=sys.stderr,
                )
                return 1

            isolated = page.evaluate("() => crossOriginIsolated === true")
            if not isolated:
                print("FAIL: crossOriginIsolated is not true", file=sys.stderr)
                return 1
            sab = page.evaluate(
                "() => typeof SharedArrayBuffer !== 'undefined'"
            )
            if not sab:
                print("FAIL: SharedArrayBuffer unavailable", file=sys.stderr)
                return 1
            if not saw_render_ok:
                print(
                    "FAIL: missing console marker MINIMAL_APP_INITIAL_RENDER_OK",
                    file=sys.stderr,
                )
                return 1

            shot = Path(args.screenshot)
            shot.parent.mkdir(parents=True, exist_ok=True)
            page.screenshot(path=str(shot), full_page=True)
            print(f"PASS screenshot={shot}")
        except (PlaywrightError, TimeoutError, AssertionError) as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 1
        finally:
            context.close()
            browser.close()

    if page_errors:
        for err in page_errors:
            print(f"PAGEERROR: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
