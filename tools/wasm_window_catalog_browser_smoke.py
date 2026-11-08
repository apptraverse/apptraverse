#!/usr/bin/env python3
"""Browser smoke for web_window_catalog_demo initial chrome render."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from playwright.sync_api import Error as PlaywrightError
from playwright.sync_api import Page, expect, sync_playwright

EXPECTED_CONTROL_IDS = ("5002", "5003", "5004", "5005")


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
    console_errors: list[str] = []
    request_failures: list[str] = []
    markers = {
        "idbfs_loaded": False,
        "wasm_ready": False,
        "initial_render_ok": False,
    }

    def on_pageerror(err: Exception) -> None:
        page_errors.append(str(err))

    def on_console(msg) -> None:
        text = msg.text
        if "WINDOW_CATALOG_IDBFS_LOADED" in text:
            markers["idbfs_loaded"] = True
        if "WINDOW_CATALOG_WASM_READY" in text:
            markers["wasm_ready"] = True
        if "WINDOW_CATALOG_INITIAL_RENDER_OK" in text:
            markers["initial_render_ok"] = True
        if msg.type == "error":
            if "Failed to load resource:" in text or "favicon.ico" in text:
                return
            console_errors.append(text)

    def on_requestfailed(request) -> None:
        request_failures.append(f"{request.url} ({request.failure})")

    def dump_diagnostics(page: Page | None, reason: str) -> None:
        print(f"FAIL: {reason}", file=sys.stderr)
        print(
            "stage markers: "
            f"IDBFS_LOADED={markers['idbfs_loaded']} "
            f"WASM_READY={markers['wasm_ready']} "
            f"INITIAL_RENDER_OK={markers['initial_render_ok']}",
            file=sys.stderr,
        )
        if page is not None:
            try:
                title = page.locator("#page-title").inner_text(timeout=2000)
                status = page.locator("#status").inner_text(timeout=2000)
                main_page = page.locator("#catalog-mount .catalog-page-main")
                button_count = main_page.locator(
                    'button[data-apptraverse-control-id]'
                ).count()
                print(
                    f"DOM: page-title={title!r} status={status!r} "
                    f"main_page_buttons={button_count}",
                    file=sys.stderr,
                )
            except (PlaywrightError, TimeoutError) as dom_exc:
                print(f"DOM probe failed: {dom_exc}", file=sys.stderr)
        for err in page_errors:
            print(f"PAGEERROR: {err}", file=sys.stderr)
        for err in console_errors:
            print(f"CONSOLE.ERROR: {err}", file=sys.stderr)
        for err in request_failures:
            print(f"REQUESTFAILED: {err}", file=sys.stderr)

    exit_code = 0
    page: Page | None = None

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True, channel="chrome")
        context = browser.new_context()
        page = context.new_page()
        page.on("pageerror", on_pageerror)
        page.on("console", on_console)
        page.on("requestfailed", on_requestfailed)

        try:
            page.goto(args.url, wait_until="domcontentloaded", timeout=60000)
            expect(page.locator("#page-title")).not_to_have_text("Loading…", timeout=60000)
            main_page = page.locator("#catalog-mount .catalog-page-main.page-active")
            expect(main_page).to_have_count(1, timeout=60000)
            for control_id in EXPECTED_CONTROL_IDS:
                btn = main_page.locator(
                    f'button[data-apptraverse-control-id="{control_id}"]'
                )
                expect(btn).to_have_count(1, timeout=60000)

            isolated = page.evaluate("() => crossOriginIsolated === true")
            if not isolated:
                exit_code = 1
                dump_diagnostics(page, "crossOriginIsolated is not true")
            elif not page.evaluate(
                "() => typeof SharedArrayBuffer !== 'undefined'"
            ):
                exit_code = 1
                dump_diagnostics(page, "SharedArrayBuffer unavailable")
            elif not markers["initial_render_ok"]:
                exit_code = 1
                dump_diagnostics(
                    page, "missing console marker WINDOW_CATALOG_INITIAL_RENDER_OK"
                )
            else:
                shot = Path(args.screenshot)
                shot.parent.mkdir(parents=True, exist_ok=True)
                page.screenshot(path=str(shot), full_page=True)
                if page_errors or console_errors or request_failures:
                    exit_code = 1
                    dump_diagnostics(page, "page reported errors during load")
                else:
                    print(f"PASS screenshot={shot}")
        except (PlaywrightError, TimeoutError, AssertionError) as exc:
            exit_code = 1
            dump_diagnostics(page, str(exc))
        finally:
            context.close()
            browser.close()

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
