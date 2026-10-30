#!/usr/bin/env python3
"""Browser regression for Web presenter DOM teardown (ApplicationUiSession)."""

from __future__ import annotations

import argparse
import sys

from playwright.sync_api import Error as PlaywrightError
from playwright.sync_api import sync_playwright


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--url",
        default="http://127.0.0.1:8770/apptraverse_web_presenter_lifecycle_test.html",
    )
    args = parser.parse_args()

    page_errors: list[str] = []
    saw_ok = False

    def on_pageerror(err: Exception) -> None:
        page_errors.append(str(err))

    def on_console(msg) -> None:
        nonlocal saw_ok
        if "WEB_PRESENTER_LIFECYCLE_OK" in msg.text:
            saw_ok = True
        if msg.type == "error":
            if "Failed to load resource:" in msg.text or "favicon.ico" in msg.text:
                return
            page_errors.append(f"console.error: {msg.text}")

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True, channel="chrome")
        context = browser.new_context()
        page = context.new_page()
        page.on("pageerror", on_pageerror)
        page.on("console", on_console)
        try:
            page.goto(args.url, wait_until="load", timeout=120000)
            page.wait_for_function(
                "() => globalThis.__webPresenterLifecycleDone === true",
                timeout=60000,
            )
        except (PlaywrightError, TimeoutError) as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 1
        finally:
            context.close()
            browser.close()

    if page_errors:
        for err in page_errors:
            print(f"PAGEERROR: {err}", file=sys.stderr)
        return 1
    if not saw_ok:
        print("FAIL: missing WEB_PRESENTER_LIFECYCLE_OK", file=sys.stderr)
        return 1
    print("PASS web_presenter_lifecycle")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
