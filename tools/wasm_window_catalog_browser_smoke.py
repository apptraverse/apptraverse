#!/usr/bin/env python3
"""Browser E2E for web_window_catalog_demo (WebApplicationHost + library presenters)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from playwright.sync_api import Error as PlaywrightError
from playwright.sync_api import expect, sync_playwright

LIST_ID = "5001"
ADD_ID = "5002"
OPEN_ID = "5003"
DELETE_ID = "5004"
LANG_ID = "5005"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--url",
        default="http://127.0.0.1:8765/examples/surfaces_demo/web/web_window_catalog_demo.html",
    )
    parser.add_argument(
        "--screenshot",
        default="docs/smoke_artifacts/window_catalog_web_initial.png",
    )
    args = parser.parse_args()

    page_errors: list[str] = []
    saw_render_ok = False
    saw_idbfs_saved = False

    def on_pageerror(err: Exception) -> None:
        page_errors.append(str(err))

    def on_console(msg) -> None:
        nonlocal saw_render_ok, saw_idbfs_saved
        text = msg.text
        if "WINDOW_CATALOG_INITIAL_RENDER_OK" in text:
            saw_render_ok = True
        if "WINDOW_CATALOG_IDBFS_SAVED" in text:
            saw_idbfs_saved = True
            page.evaluate("() => { window.__catalogIdbfsSaved = true; }")
        if "WINDOW_CATALOG_LOADED_FROM_PROFILE" in text:
            page.evaluate("() => { window.__catalogLoadedFromProfile = true; }")
        if "WINDOW_CATALOG_DEV_BOOTSTRAP" in text:
            page.evaluate("() => { window.__catalogDevBootstrap = true; }")
        if msg.type == "error":
            if "Failed to load resource:" in text or "favicon.ico" in text:
                return
            if "WINDOW_CATALOG_SAVE_FAILED" in text:
                page_errors.append(f"console.error: {text}")
                return
            page_errors.append(f"console.error: {text}")

    def on_requestfailed(request) -> None:
        name = request.url.rsplit("/", 1)[-1]
        if name.startswith("web_window_catalog_demo"):
            page_errors.append(
                f"requestfailed: {request.url} ({request.failure})"
            )

    def mount(page):
        return page.locator("#app-mount")

    def chrome_button(page, control_id: str):
        return mount(page).locator(
            f'button.apptraverse-push-button[data-apptraverse-control-id="{control_id}"]'
        )

    def list_rows(page):
        return mount(page).locator(
            f'.apptraverse-list[data-apptraverse-control-id="{LIST_ID}"] '
            ".apptraverse-list-row"
        )

    def desktop_windows(page):
        return mount(page).locator(".apptraverse-desktop-window")

    def window_by_title(page, title: str):
        return mount(page).locator(
            f'.apptraverse-desktop-window[data-apptraverse-window-title="{title}"]'
        )

    def run_catalog_actions(page) -> None:
        chrome_button(page, ADD_ID).click()
        expect(list_rows(page)).to_have_count(1, timeout=60000)
        expect(desktop_windows(page)).to_have_count(2, timeout=60000)
        expect(window_by_title(page, "Window 1")).to_have_count(1, timeout=60000)

        chrome_button(page, ADD_ID).click()
        expect(list_rows(page)).to_have_count(2, timeout=60000)
        expect(desktop_windows(page)).to_have_count(3, timeout=60000)
        expect(window_by_title(page, "Window 2")).to_have_count(1, timeout=60000)

        chrome_button(page, LANG_ID).click()
        expect(chrome_button(page, LANG_ID)).to_have_text("EN", timeout=60000)
        expect(window_by_title(page, "Окно 1")).to_have_count(1, timeout=60000)
        expect(window_by_title(page, "Окно 2")).to_have_count(1, timeout=60000)

        list_rows(page).nth(0).click()
        expect(list_rows(page).nth(0)).to_have_attribute(
            "aria-selected", "true", timeout=5000
        )
        expect(chrome_button(page, OPEN_ID)).to_be_enabled(timeout=5000)
        expect(chrome_button(page, DELETE_ID)).to_be_enabled(timeout=5000)

        window_by_title(page, "Окно 1").locator(
            ".apptraverse-desktop-close"
        ).click()
        expect(desktop_windows(page)).to_have_count(2, timeout=60000)
        expect(window_by_title(page, "Окно 1")).to_have_count(0, timeout=60000)
        expect(window_by_title(page, "Окно 2")).to_have_count(1, timeout=60000)

        list_rows(page).nth(0).click()
        chrome_button(page, OPEN_ID).click()
        expect(window_by_title(page, "Окно 1")).to_have_count(1, timeout=60000)
        expect(desktop_windows(page)).to_have_count(3, timeout=60000)

        list_rows(page).nth(0).click()
        chrome_button(page, DELETE_ID).click()
        expect(list_rows(page)).to_have_count(1, timeout=60000)
        expect(desktop_windows(page)).to_have_count(2, timeout=60000)
        expect(window_by_title(page, "Окно 2")).to_have_count(1, timeout=60000)

    def wait_saved(page) -> None:
        expect(page.locator("#save-status")).to_have_text("Saved", timeout=60000)
        page.wait_for_function(
            "() => window.__catalogIdbfsSaved === true", timeout=60000
        )

    def wait_loaded_from_profile(page) -> None:
        page.wait_for_function(
            "() => window.__catalogLoadedFromProfile === true", timeout=60000
        )
        if page.evaluate("() => window.__catalogDevBootstrap === true"):
            raise AssertionError("DEV_BOOTSTRAP after reload")

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True, channel="chrome")
        context = browser.new_context()
        page = context.new_page()
        page.on("pageerror", on_pageerror)
        page.on("console", on_console)
        page.on("requestfailed", on_requestfailed)

        try:
            page.goto(args.url, wait_until="domcontentloaded", timeout=60000)
            page.evaluate(
                """() => {
                  window.__catalogLoadedFromProfile = false;
                  window.__catalogDevBootstrap = false;
                  window.__catalogIdbfsSaved = false;
                }"""
            )
            expect(page.locator("#loading")).to_have_text("", timeout=60000)
            expect(desktop_windows(page)).to_have_count(1, timeout=60000)
            expect(window_by_title(page, "Window Catalog")).to_have_count(
                1, timeout=60000
            )
            expect(list_rows(page)).to_have_count(0, timeout=5000)
            expect(chrome_button(page, ADD_ID)).to_have_text("Add", timeout=5000)
            expect(chrome_button(page, OPEN_ID)).to_be_disabled(timeout=5000)
            expect(chrome_button(page, DELETE_ID)).to_be_disabled(timeout=5000)

            if not page.evaluate("() => crossOriginIsolated === true"):
                print("FAIL: crossOriginIsolated is not true", file=sys.stderr)
                return 1
            if not page.evaluate(
                "() => typeof SharedArrayBuffer !== 'undefined'"
            ):
                print("FAIL: SharedArrayBuffer unavailable", file=sys.stderr)
                return 1
            if not saw_render_ok:
                print(
                    "FAIL: missing WINDOW_CATALOG_INITIAL_RENDER_OK",
                    file=sys.stderr,
                )
                return 1

            run_catalog_actions(page)

            page.locator("#save-button").click()
            wait_saved(page)

            page.goto(args.url, wait_until="domcontentloaded", timeout=60000)
            wait_loaded_from_profile(page)
            expect(page.locator("#loading")).to_have_text("", timeout=60000)
            expect(list_rows(page)).to_have_count(1, timeout=60000)
            expect(chrome_button(page, LANG_ID)).to_have_text("EN", timeout=60000)
            expect(window_by_title(page, "Окно 2")).to_have_count(1, timeout=60000)

            chrome_button(page, ADD_ID).click()
            expect(list_rows(page)).to_have_count(2, timeout=60000)
            expect(window_by_title(page, "Окно 3")).to_have_count(1, timeout=60000)

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
    if not saw_idbfs_saved:
        print("FAIL: missing WINDOW_CATALOG_IDBFS_SAVED", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
