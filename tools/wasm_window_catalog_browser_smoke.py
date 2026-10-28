#!/usr/bin/env python3
"""Browser smoke for web_window_catalog_demo initial chrome render."""

from __future__ import annotations

import argparse
import sys

from playwright.sync_api import sync_playwright


def wait_ready(page, timeout_ms: int = 60000) -> None:
    deadline_steps = timeout_ms // 100
    for _ in range(deadline_steps):
        ready = page.evaluate(
            """() => {
              const title = document.getElementById('catalog-title');
              const buttons = document.querySelectorAll('#catalog-buttons button');
              return title && title.textContent && title.textContent !== 'Loading…'
                     && buttons.length >= 4;
            }"""
        )
        if ready:
            return
        page.wait_for_timeout(100)
    title = page.eval_on_selector("#catalog-title", "el => el ? el.textContent : ''")
    count = page.eval_on_selector(
        "#catalog-buttons", "el => el ? el.querySelectorAll('button').length : 0"
    )
    raise TimeoutError(
        f"Timed out waiting for WindowCatalog chrome; title={title!r} buttons={count}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--url", default="http://127.0.0.1:8765/web_window_catalog_demo.html"
    )
    args = parser.parse_args()

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True, channel="chrome")
        page = browser.new_page()
        page.on("pageerror", lambda err: print(f"PAGEERROR: {err}", file=sys.stderr))
        page.on(
            "console",
            lambda msg: print(f"CONSOLE[{msg.type}]: {msg.text}")
            if "WINDOW_CATALOG" in msg.text or msg.type == "error"
            else None,
        )
        page.goto(args.url, wait_until="domcontentloaded", timeout=60000)
        wait_ready(page, timeout_ms=60000)
        title = page.eval_on_selector("#catalog-title", "el => el.textContent.trim()")
        labels = page.eval_on_selector_all(
            "#catalog-buttons button", "els => els.map(e => e.textContent.trim())"
        )
        print(f"PASS title={title!r} buttons={labels}")
        browser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
