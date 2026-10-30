#!/usr/bin/env python3
"""Browser smoke for web_minimal_app: UI commands, IDBFS save, cold reload."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from playwright.sync_api import Error as PlaywrightError
from playwright.sync_api import expect, sync_playwright

APPLY_CONTROL_ID = "7101"
LIST_CONTROL_ID = "7102"
AUX_OPENER_CONTROL_ID = "7103"
AUX_APPLY_CONTROL_ID = "7201"
AUX_LIST_CONTROL_ID = "7202"
AUX_CLOSER_CONTROL_ID = "7203"


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
    saw_idbfs_saved = False
    saw_shutdown_stopped = False
    reload_profile_loads: list[bool] = []

    def on_pageerror(err: Exception) -> None:
        page_errors.append(str(err))

    def on_console(msg) -> None:
        nonlocal saw_render_ok, saw_idbfs_saved
        text = msg.text
        if "MINIMAL_APP_INITIAL_RENDER_OK" in text:
            saw_render_ok = True
        if "MINIMAL_APP_IDBFS_SAVED" in text:
            saw_idbfs_saved = True
            page.evaluate("() => { window.__minimalIdbfsSaved = true; }")
        if "MINIMAL_APP_SHUTDOWN_STOPPED" in text:
            saw_shutdown_stopped = True
            page.evaluate("() => { window.__minimalShutdownStopped = true; }")
        if "MINIMAL_APP_LOADED_FROM_PROFILE" in text:
            page.evaluate("() => { window.__minimalLoadedFromProfile = true; }")
        if "MINIMAL_APP_DEV_BOOTSTRAP" in text:
            page.evaluate("() => { window.__minimalDevBootstrap = true; }")
        if msg.type == "error":
            if "Failed to load resource:" in text or "favicon.ico" in text:
                return
            if "MINIMAL_APP_SAVE_FAILED" in text:
                page_errors.append(f"console.error: {text}")
                return
            if "MINIMAL_APP_SHUTDOWN_FAILED" in text:
                page_errors.append(f"console.error: {text}")
                return
            page_errors.append(f"console.error: {text}")

    def on_requestfailed(request) -> None:
        name = request.url.rsplit("/", 1)[-1]
        if name.startswith("web_minimal_app"):
            page_errors.append(
                f"requestfailed: {request.url} ({request.failure})"
            )

    def wait_title(page, text: str, timeout_ms: int = 60000) -> None:
        page.wait_for_function(
            """(expected) => {
              const wins = document.querySelectorAll(
                '#app-mount [data-apptraverse-desktop-window="1"]');
              for (const win of wins) {
                if (win.dataset.apptraverseWindowTitle === expected) {
                  return true;
                }
              }
              return false;
            }""",
            arg=text,
            timeout=timeout_ms,
        )

    def wait_aux_title(page, text: str, timeout_ms: int = 60000) -> None:
        page.wait_for_function(
            """(expected) => {
              const wins = document.querySelectorAll(
                '#app-mount [data-apptraverse-desktop-window="1"]');
              for (const win of wins) {
                if (win.dataset.apptraverseWindowTitle === expected) {
                  return true;
                }
              }
              return false;
            }""",
            arg=text,
            timeout=timeout_ms,
        )

    def count_desktop_windows(page) -> int:
        return page.evaluate(
            """() => document.querySelectorAll(
              '#app-mount [data-apptraverse-desktop-window="1"]').length"""
        )

    def wait_saved(page) -> None:
        expect(page.locator("#save-status")).to_have_text("Saved", timeout=60000)
        page.wait_for_function(
            "() => window.__minimalIdbfsSaved === true",
            timeout=60000,
        )

    def wait_loaded_from_profile(page) -> None:
        page.wait_for_function(
            "() => window.__minimalLoadedFromProfile === true",
            timeout=60000,
        )
        if page.evaluate("() => window.__minimalDevBootstrap === true"):
            raise AssertionError("DEV_BOOTSTRAP after reload")

    def wait_shutdown_stopped(page) -> None:
        nonlocal saw_shutdown_stopped
        expect(page.locator("#shutdown-status")).to_have_text(
            "Stopped", timeout=60000
        )
        page.wait_for_function(
            """() => {
              if (document.getElementById('shutdown-status').textContent !== 'Stopped') {
                return false;
              }
              window.__minimalShutdownStopped = true;
              return true;
            }""",
            timeout=60000,
        )
        saw_shutdown_stopped = True

    def ensure_two_desktop_windows(page) -> None:
        if count_desktop_windows(page) == 1:
            page.locator(
                f"[data-apptraverse-control-id='{AUX_OPENER_CONTROL_ID}']"
            ).click()
            page.wait_for_function(
                f"() => document.querySelectorAll("
                f"'#app-mount [data-apptraverse-desktop-window=\"1\"]').length === 2",
                timeout=60000,
            )
        elif count_desktop_windows(page) != 2:
            raise AssertionError("expected one or two desktop windows")

    def assert_geometry(page) -> None:
        window = page.locator(
            "#app-mount [data-apptraverse-desktop-window='1']"
        ).first
        box = window.bounding_box()
        mount = page.locator("#app-mount").bounding_box()
        if box is None or mount is None:
            raise AssertionError("missing bounding box")
        rel_x = round(box["x"] - mount["x"])
        rel_y = round(box["y"] - mount["y"])
        if rel_x != 120 or rel_y != 80:
            raise AssertionError(
                f"window position ({rel_x},{rel_y}) expected (120,80)"
            )
        if round(box["width"]) != 640 or round(box["height"]) != 480:
            raise AssertionError(
                f"client area {box['width']}x{box['height']} expected 640x480"
            )

    def run_ui_sequence_to_row_a(page) -> None:
        button = page.locator(
            f"#app-mount [data-apptraverse-control-id='{APPLY_CONTROL_ID}']"
        )
        list_box = page.locator(
            f"#app-mount [data-apptraverse-control-id='{LIST_CONTROL_ID}']"
        )
        rows = list_box.locator(".apptraverse-list-row")
        wait_title(page, "Minimal App")
        rows.nth(1).click()
        expect(rows.nth(1)).to_have_attribute("aria-selected", "true")
        button.click()
        wait_title(page, "Applied")
        expect(button).to_be_disabled()
        expect(list_box.locator(".apptraverse-list-row-selected")).to_have_count(
            0
        )
        rows.nth(0).click()
        expect(rows.nth(0)).to_have_attribute("aria-selected", "true")
        rows.nth(0).dblclick()
        wait_title(page, "Activated Row A")

    def run_dual_window_lifecycle(page) -> None:
        opener = page.locator(
            f"#app-mount [data-apptraverse-control-id='{AUX_OPENER_CONTROL_ID}']"
        )
        expect(opener).to_have_count(1)
        opener.click()
        page.wait_for_function(
            f"() => document.querySelectorAll("
            f"'#app-mount [data-apptraverse-control-id=\"{AUX_LIST_CONTROL_ID}\"]"
            f"').length === 1",
            timeout=60000,
        )
        if count_desktop_windows(page) != 2:
            raise AssertionError("expected two desktop windows after open")

        main_list = page.locator(
            f"#app-mount [data-apptraverse-control-id='{LIST_CONTROL_ID}']"
        )
        aux_list = page.locator(
            f"#app-mount [data-apptraverse-control-id='{AUX_LIST_CONTROL_ID}']"
        )
        aux_apply = page.locator(
            f"#app-mount [data-apptraverse-control-id='{AUX_APPLY_CONTROL_ID}']"
        )
        aux_rows = aux_list.locator(".apptraverse-list-row")
        wait_aux_title(page, "Detail Panel")
        aux_rows.nth(1).click()
        expect(aux_rows.nth(1)).to_have_attribute("aria-selected", "true")
        aux_apply.click()
        wait_aux_title(page, "Applied")
        expect(aux_apply).to_be_disabled()
        expect(aux_list.locator(".apptraverse-list-row-selected")).to_have_count(
            0
        )
        wait_title(page, "Activated Row A")

        main_rows = main_list.locator(".apptraverse-list-row")
        main_rows.nth(1).click()
        expect(main_rows.nth(1)).to_have_attribute("aria-selected", "true")
        main_rows.nth(1).dblclick()
        wait_title(page, "Activated Row B")
        wait_aux_title(page, "Applied")

        closer = page.locator(
            f"#app-mount [data-apptraverse-control-id='{AUX_CLOSER_CONTROL_ID}']"
        )
        closer.click()
        page.wait_for_function(
            f"() => document.querySelectorAll("
            f"'#app-mount [data-apptraverse-control-id=\"{AUX_LIST_CONTROL_ID}\"]"
            f"').length === 0",
            timeout=60000,
        )
        if count_desktop_windows(page) != 1:
            raise AssertionError("expected one desktop window after close")
        main_rows.nth(1).click()
        expect(main_rows.nth(1)).to_have_attribute("aria-selected", "true")

        opener.click()
        page.wait_for_function(
            f"() => document.querySelectorAll("
            f"'#app-mount [data-apptraverse-control-id=\"{AUX_LIST_CONTROL_ID}\"]"
            f"').length === 1",
            timeout=60000,
        )
        if count_desktop_windows(page) != 2:
            raise AssertionError("expected two windows after reopen")
        wait_aux_title(page, "Applied")
        expect(aux_apply).to_be_disabled()
        expect(aux_list.locator(".apptraverse-list-row-selected")).to_have_count(
            0
        )

        closer.click()
        page.wait_for_function(
            f"() => document.querySelectorAll("
            f"'#app-mount [data-apptraverse-control-id=\"{AUX_LIST_CONTROL_ID}\"]"
            f"').length === 0",
            timeout=60000,
        )
        opener.click()
        page.wait_for_function(
            f"() => document.querySelectorAll("
            f"'#app-mount [data-apptraverse-control-id=\"{AUX_LIST_CONTROL_ID}\"]"
            f"').length === 1",
            timeout=60000,
        )
        if count_desktop_windows(page) != 2:
            raise AssertionError("expected two windows after second open")
        if page.locator(
            f"[data-apptraverse-control-id='{AUX_LIST_CONTROL_ID}']"
        ).count() != 1:
            raise AssertionError("duplicate auxiliary list DOM")

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
                  window.__minimalLoadedFromProfile = false;
                  window.__minimalDevBootstrap = false;
                  window.__minimalIdbfsSaved = false;
                  window.__minimalShutdownStopped = false;
                }"""
            )

            window = page.locator(
                "#app-mount [data-apptraverse-desktop-window='1']"
            )
            expect(window).to_have_count(1, timeout=60000)
            assert_geometry(page)

            button = page.locator(
                f"#app-mount [data-apptraverse-control-id='{APPLY_CONTROL_ID}']"
            )
            list_box = page.locator(
                f"#app-mount [data-apptraverse-control-id='{LIST_CONTROL_ID}']"
            )
            expect(button).to_have_count(1, timeout=60000)
            expect(list_box).to_have_count(1, timeout=60000)
            expect(button).to_have_text("Apply")
            expect(button).to_be_enabled()

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
                    "FAIL: missing MINIMAL_APP_INITIAL_RENDER_OK",
                    file=sys.stderr,
                )
                return 1

            run_ui_sequence_to_row_a(page)
            run_dual_window_lifecycle(page)

            page.evaluate("() => { window.__minimalIdbfsSaved = false; }")
            page.locator("#save-button").click()
            wait_saved(page)
            if not saw_idbfs_saved:
                print("FAIL: missing MINIMAL_APP_IDBFS_SAVED", file=sys.stderr)
                return 1

            page.evaluate(
                """() => {
                  window.__minimalLoadedFromProfile = false;
                  window.__minimalDevBootstrap = false;
                }"""
            )
            page.reload(wait_until="domcontentloaded", timeout=60000)
            wait_loaded_from_profile(page)
            reload_profile_loads.append(True)
            page.wait_for_function(
                """() => document.querySelectorAll(
                  '#app-mount [data-apptraverse-desktop-window="1"]').length >= 1""",
                timeout=60000,
            )
            if count_desktop_windows(page) == 1:
                page.locator(
                    f"[data-apptraverse-control-id='{AUX_OPENER_CONTROL_ID}']"
                ).click()
                page.wait_for_function(
                    f"() => document.querySelectorAll("
                    f"'#app-mount [data-apptraverse-desktop-window=\"1\"]').length === 2",
                    timeout=60000,
                )
            elif count_desktop_windows(page) != 2:
                raise AssertionError(
                    "expected one or two desktop windows after first cold reload"
                )
            wait_title(page, "Activated Row B")
            expect(button).to_be_disabled()
            rows = list_box.locator(".apptraverse-list-row")
            expect(rows).to_have_count(2)
            expect(rows.nth(1)).to_have_attribute("aria-selected", "true")
            assert_geometry(page)
            aux_list = page.locator(
                f"[data-apptraverse-control-id='{AUX_LIST_CONTROL_ID}']"
            )
            wait_aux_title(page, "Applied")
            expect(
                aux_list.locator(".apptraverse-list-row-selected")
            ).to_have_count(0)

            rows.nth(0).click()
            expect(rows.nth(0)).to_have_attribute("aria-selected", "true")
            rows.nth(0).dblclick()
            wait_title(page, "Activated Row A")

            saw_idbfs_saved = False
            page.evaluate("() => { window.__minimalIdbfsSaved = false; }")
            page.locator("#save-button").click()
            wait_saved(page)

            page.evaluate(
                """() => {
                  window.__minimalLoadedFromProfile = false;
                  window.__minimalDevBootstrap = false;
                }"""
            )
            page.reload(wait_until="domcontentloaded", timeout=60000)
            wait_loaded_from_profile(page)
            reload_profile_loads.append(True)
            page.wait_for_function(
                """() => document.querySelectorAll(
                  '#app-mount [data-apptraverse-desktop-window="1"]').length >= 1""",
                timeout=60000,
            )
            if count_desktop_windows(page) == 1:
                page.locator(
                    f"[data-apptraverse-control-id='{AUX_OPENER_CONTROL_ID}']"
                ).click()
                page.wait_for_function(
                    f"() => document.querySelectorAll("
                    f"'#app-mount [data-apptraverse-desktop-window=\"1\"]').length === 2",
                    timeout=60000,
                )
            elif count_desktop_windows(page) != 2:
                raise AssertionError(
                    "expected one or two desktop windows after second cold reload"
                )
            wait_title(page, "Activated Row A")
            wait_aux_title(page, "Applied")
            expect(rows.nth(0)).to_have_attribute("aria-selected", "true")
            expect(
                aux_list.locator(".apptraverse-list-row-selected")
            ).to_have_count(0)
            expect(button).to_be_disabled()
            assert_geometry(page)

            ensure_two_desktop_windows(page)
            rows.nth(1).click()
            expect(rows.nth(1)).to_have_attribute("aria-selected", "true")
            rows.nth(1).dblclick()
            wait_title(page, "Activated Row B")

            page.evaluate(
                """() => {
                  window.__staleBtn = document.querySelector(
                    '#app-mount [data-apptraverse-control-id="7101"]');
                  window.__staleList = document.querySelector(
                    '#app-mount [data-apptraverse-control-id="7102"]');
                  if (!window.__staleBtn || !window.__staleList) {
                    throw new Error('expected live controls before Stop');
                  }
                }"""
            )

            page.evaluate(
                """() => {
                  window.__minimalShutdownStopped = false;
                  window.__minimalIdbfsSaved = false;
                }"""
            )
            page.locator("#stop-button").click()
            wait_shutdown_stopped(page)
            if not saw_shutdown_stopped:
                print(
                    "FAIL: missing MINIMAL_APP_SHUTDOWN_STOPPED",
                    file=sys.stderr,
                )
                return 1

            page.wait_for_function(
                """() => document.querySelectorAll(
                  '#app-mount [data-apptraverse-control-id]').length === 0""",
                timeout=60000,
            )
            page.wait_for_function(
                """() => document.querySelectorAll(
                  '#app-mount [data-apptraverse-desktop-window="1"]').length === 0""",
                timeout=60000,
            )
            page.evaluate(
                """() => {
                  window.__staleClickErr = '';
                  for (const el of [window.__staleBtn, window.__staleList]) {
                    if (!el) continue;
                    try {
                      el.dispatchEvent(new MouseEvent('click', { bubbles: true }));
                    } catch (e) {
                      window.__staleClickErr = String(e);
                    }
                  }
                }"""
            )
            if page.evaluate("() => window.__staleClickErr"):
                raise AssertionError(
                    "stale presenter DOM click threw: "
                    + page.evaluate("() => window.__staleClickErr")
                )

            saw_shutdown_stopped = False
            page.evaluate(
                """() => {
                  window.__minimalLoadedFromProfile = false;
                  window.__minimalDevBootstrap = false;
                  window.__minimalShutdownStopped = false;
                }"""
            )
            page.reload(wait_until="domcontentloaded", timeout=60000)
            wait_loaded_from_profile(page)
            reload_profile_loads.append(True)
            page.wait_for_function(
                """() => document.querySelectorAll(
                  '#app-mount [data-apptraverse-desktop-window="1"]').length >= 1""",
                timeout=60000,
            )
            ensure_two_desktop_windows(page)
            wait_title(page, "Activated Row B")
            expect(button).to_be_disabled()
            rows = list_box.locator(".apptraverse-list-row")
            expect(rows.nth(1)).to_have_attribute("aria-selected", "true")

            rows.nth(0).click()
            expect(rows.nth(0)).to_have_attribute("aria-selected", "true")
            rows.nth(0).dblclick()
            wait_title(page, "Activated Row A")

            shot = Path(args.screenshot)
            shot.parent.mkdir(parents=True, exist_ok=True)
            page.screenshot(path=str(shot), full_page=True)
            print("PASS screenshot=" + str(shot))
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
    if len(reload_profile_loads) != 3:
        print("FAIL: expected three profile reloads", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
