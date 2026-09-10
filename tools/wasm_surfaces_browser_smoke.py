#!/usr/bin/env python3
"""Browser smoke for web_surfaces_demo on the integration SHA."""

from __future__ import annotations

import argparse
import sys

from playwright.sync_api import sync_playwright


def tab_labels(page) -> list[str]:
    return page.eval_on_selector_all(
        "#tab-strip button.surface-tab", "els => els.map(e => e.textContent.trim())"
    )


def active_tab(page) -> str:
    return page.eval_on_selector(
        "#tab-strip button.surface-tab.active",
        "el => el ? el.textContent.trim() : ''",
    )


def content_text(page) -> str:
    return page.eval_on_selector("#content", "el => el.textContent.trim()")


def wait_ready(page, timeout_ms: int = 90000) -> None:
    deadline_steps = timeout_ms // 100
    for _ in range(deadline_steps):
        labels = tab_labels(page)
        content = content_text(page)
        if labels and content and content != "Loading…":
            return
        page.wait_for_timeout(100)
    raise TimeoutError(
        f"Timed out waiting for UI ready; tabs={tab_labels(page)!r} "
        f"content={content_text(page)!r}"
    )


def clear_idb(page) -> None:
    page.evaluate(
        """async () => {
          const dbs = await indexedDB.databases();
          await Promise.all((dbs || []).map(db => new Promise((resolve) => {
            const req = indexedDB.deleteDatabase(db.name);
            req.onsuccess = () => resolve();
            req.onerror = () => resolve();
            req.onblocked = () => resolve();
          })));
        }"""
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--url", default="http://127.0.0.1:8765/web_surfaces_demo.html"
    )
    args = parser.parse_args()

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True, channel="chrome")
        context = browser.new_context()
        page = context.new_page()
        page.on("pageerror", lambda err: print(f"PAGEERROR: {err}", file=sys.stderr))
        page.on(
            "console",
            lambda msg: print(f"CONSOLE[{msg.type}]: {msg.text}")
            if any(
                k in msg.text
                for k in ("SURFACES_", "Error", "assert", "crossOrigin")
            )
            else None,
        )

        print("Phase 0: clear IndexedDB for fresh state")
        page.goto(args.url, wait_until="domcontentloaded", timeout=90000)
        clear_idb(page)

        print("Phase 1: fresh load")
        page.goto(args.url, wait_until="domcontentloaded", timeout=90000)
        wait_ready(page)
        iso = page.evaluate("() => crossOriginIsolated")
        sab = page.evaluate("() => typeof SharedArrayBuffer !== 'undefined'")
        print(f"  crossOriginIsolated={iso} SharedArrayBuffer={sab}")
        if not iso or not sab:
            raise RuntimeError("crossOriginIsolated/SharedArrayBuffer required")
        labels = tab_labels(page)
        print(f"  tabs={labels} active={active_tab(page)!r}")
        assert labels == ["Surface 1"], labels
        assert active_tab(page) == "Surface 1"

        print("Phase 2: Add Surface 2 and Surface 3")
        page.evaluate("() => Module._AppTraverseWebAddCurrent()")
        page.wait_for_function(
            "() => document.querySelectorAll('#tab-strip button.surface-tab').length >= 2",
            timeout=30000,
        )
        page.evaluate("() => Module._AppTraverseWebAddCurrent()")
        page.wait_for_function(
            "() => document.querySelectorAll('#tab-strip button.surface-tab').length >= 3",
            timeout=30000,
        )
        page.wait_for_timeout(800)
        labels = tab_labels(page)
        print(f"  tabs={labels} active={active_tab(page)!r}")
        assert labels == ["Surface 1", "Surface 2", "Surface 3"], labels

        print("Phase 3: select Surface 2, checkpoint, real reload")
        page.click("#tab-strip button.surface-tab >> text=Surface 2")
        page.wait_for_function(
            "() => document.querySelector('#tab-strip button.surface-tab.active')"
            "?.textContent.trim() === 'Surface 2'",
            timeout=30000,
        )
        page.wait_for_timeout(2500)
        print(f"  before reload active={active_tab(page)!r}")
        page.reload(wait_until="domcontentloaded", timeout=90000)
        wait_ready(page)
        labels = tab_labels(page)
        active = active_tab(page)
        print(f"  after reload tabs={labels} active={active!r}")
        assert labels == ["Surface 1", "Surface 2", "Surface 3"], labels
        assert active == "Surface 2", active

        print("Phase 4: select Surface 2 then Remove current")
        page.click("#tab-strip button.surface-tab >> text=Surface 2")
        page.wait_for_timeout(300)
        page.evaluate("() => Module._AppTraverseWebRemoveCurrent()")
        page.wait_for_function(
            "() => document.querySelectorAll('#tab-strip button.surface-tab').length === 2",
            timeout=30000,
        )
        page.wait_for_timeout(800)
        labels = tab_labels(page)
        active = active_tab(page)
        print(f"  after remove tabs={labels} active={active!r}")
        assert labels == ["Surface 1", "Surface 3"], labels
        assert active in {"Surface 1", "Surface 3"}, active

        print("Phase 5: rapid Add from fresh IDB (back-to-back, no settle)")
        clear_idb(page)
        page.goto(args.url, wait_until="domcontentloaded", timeout=90000)
        wait_ready(page)
        page.evaluate(
            """() => {
              Module._AppTraverseWebAddCurrent();
              Module._AppTraverseWebAddCurrent();
            }"""
        )
        page.wait_for_function(
            "() => document.querySelectorAll('#tab-strip button.surface-tab').length >= 3",
            timeout=60000,
        )
        page.wait_for_timeout(800)
        labels = tab_labels(page)
        print(f"  rapid add tabs={labels}")
        assert len(labels) == 3, labels

        print("WASM browser smoke passed.")
        browser.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"WASM browser smoke FAILED: {exc}", file=sys.stderr)
        raise
