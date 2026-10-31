#!/usr/bin/env python3
"""Browser smoke for web_window_catalog_demo initial chrome render."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from playwright.sync_api import Error as PlaywrightError
from playwright.sync_api import expect, sync_playwright

EXPECTED_BUTTONS = {
    "5002": {"label": "Add", "enabled": True},
    "5003": {"label": "Open", "enabled": False},
    "5004": {"label": "Delete", "enabled": False},
    "5005": {"label": "RU", "enabled": True},
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--url", default="http://127.0.0.1:8765/web_window_catalog_demo.html"
    )
    parser.add_argument(
        "--screenshot",
        default="docs/smoke_artifacts/window_catalog_web_initial.png",
    )
    args = parser.parse_args()

    page_errors: list[str] = []
    saw_render_ok = False

    def on_pageerror(err: Exception) -> None:
        page_errors.append(str(err))

    def on_console(msg) -> None:
        nonlocal saw_render_ok
        text = msg.text
        if "WINDOW_CATALOG_INITIAL_RENDER_OK" in text:
            saw_render_ok = True
        if msg.type == "error":
            if "Failed to load resource:" in text or "favicon.ico" in text:
                return
            page_errors.append(f"console.error: {text}")

    def on_requestfailed(request) -> None:
        name = request.url.rsplit("/", 1)[-1]
        if name.startswith("web_window_catalog_demo"):
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
            expect(page.locator("h1")).to_have_text("WindowCatalog (Web)", timeout=60000)
            page.wait_for_function(
                "() => typeof crossOriginIsolated !== 'undefined' && crossOriginIsolated",
                timeout=60000,
            )
            page.wait_for_function(
                "() => typeof SharedArrayBuffer !== 'undefined'",
                timeout=60000,
            )
            deadline_ms = 120000
            while not saw_render_ok and deadline_ms > 0:
                page.wait_for_timeout(250)
                deadline_ms -= 250
            if not saw_render_ok:
                print(
                    "FAIL: missing console marker WINDOW_CATALOG_INITIAL_RENDER_OK",
                    file=sys.stderr,
                )
                return 1

            for control_id, spec in EXPECTED_BUTTONS.items():
                btn = page.locator(
                    f'[data-apptraverse-control-id="{control_id}"]'
                )
                expect(btn).to_have_count(1, timeout=60000)
                expect(btn).to_have_text(spec["label"], timeout=5000)
                if spec["enabled"]:
                    expect(btn).to_be_enabled(timeout=5000)
                else:
                    expect(btn).to_be_disabled(timeout=5000)

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
