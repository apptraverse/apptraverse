#!/usr/bin/env python3
"""Real browser lifecycle acceptance using one persistent Chrome profile."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlencode, urlsplit, urlunsplit

from playwright.sync_api import Page, expect, sync_playwright

from wasm_window_catalog_browser_smoke import (
    ACTIVE, MAIN, ROWS, child_page, control, load as load_app, main_page, snapshot,
)


def instrument_checkpoint(page: Page) -> None:
    page.evaluate("""() => {
      const original = Module._ApptraverseWindowCatalogPageHidden;
      if (!original || original.measured) return;
      const measured = function() {
        const start = performance.now();
        try {
          const result = original.apply(this, arguments);
          console.log('LIFECYCLE_CAPTURE_RESULT=' + result + ' visibility=' + document.visibilityState);
          return result;
        }
        finally { console.log('LIFECYCLE_CAPTURE_MS=' + (performance.now() - start).toFixed(3)); }
      };
      measured.measured = true;
      Module._ApptraverseWindowCatalogPageHidden = measured;
      document.addEventListener('visibilitychange', () => console.log('LIFECYCLE_EVENT visibility=' + document.visibilityState));
      window.addEventListener('pagehide', event => console.log('LIFECYCLE_EVENT pagehide persisted=' + event.persisted));
    }""")


def load(page: Page, url: str) -> None:
    load_app(page, url)
    instrument_checkpoint(page)


def durable_snapshot(page: Page):
    """Read IndexedDB itself, independently of Emscripten's in-memory tree."""
    return page.evaluate("""async () => {
      const entries = await new Promise((resolve, reject) => {
        const request = indexedDB.open('/persistent');
        request.onerror = () => reject(request.error);
        request.onsuccess = () => {
          const db = request.result;
          const tx = db.transaction(['FILE_DATA'], 'readonly');
          const result = [];
          const cursor = tx.objectStore('FILE_DATA').openCursor();
          cursor.onsuccess = () => {
            const item = cursor.result;
            if (item) {
              if (item.value.contents) result.push({path: item.key, ...item.value});
              item.continue();
            }
          };
          tx.oncomplete = () => { db.close(); resolve(result); };
          tx.onabort = () => { db.close(); reject(tx.error); };
        };
      });
      const result = [];
      for (const item of entries) {
        const bytes = item.contents;
        const digest = await crypto.subtle.digest('SHA-256', bytes);
        result.push({path: item.path, size: bytes.length, mtime: +item.timestamp,
          sha: Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, '0')).join('')});
      }
      return result.sort((a,b) => a.path.localeCompare(b.path));
    }""")


def active_id(page: Page) -> str:
    return page.locator(ACTIVE).get_attribute('data-window-id')


def expect_one_file(files) -> None:
    assert len(files) == 1 and files[0]['path'].endswith('/user.bin') and files[0]['size'] > 0, files


def close_tab(page: Page) -> None:
    with page.expect_event('close', timeout=15000):
        page.close(run_before_unload=True)


def close_native_browser_window(process_id: int) -> None:
    """Use the native window Close boundary, scoped to our launched process."""
    assert sys.platform == 'win32', 'Native browser-window acceptance currently requires Windows'
    windows = []
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]

    @callback_type
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == process_id:
            class_name = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, class_name, len(class_name))
            if class_name.value == 'Chrome_WidgetWin_1':
                windows.append(hwnd)
        return True

    user32.EnumWindows(visit, 0)
    assert len(windows) == 1, f'Expected one window for our Chrome PID {process_id}, found {windows}'
    assert user32.PostMessageW(windows[0], 0x0010, 0, 0), ctypes.get_last_error()


def observe(page: Page, errors: list[str], messages: list[str]) -> None:
    page.on('pageerror', lambda error: errors.append(str(error)))
    def record_console(message):
        if (message.type == 'error' and message.text.startswith('Failed to load resource:')
                and message.location.get('url', '').endswith('/favicon.ico')):
            return
        messages.append(message.type + ': ' + message.text)
    page.on('console', record_console)


def main() -> int:
    sys.stdout.reconfigure(encoding='utf-8', line_buffering=True)
    parser = argparse.ArgumentParser()
    parser.add_argument('--url', default='http://127.0.0.1:8785/web_window_catalog_demo_load_only.html')
    parser.add_argument('--artifacts', default='.artifacts/wasm-browser-lifecycle')
    parser.add_argument('--headless', action='store_true', help='Skip the real tab-hide case; Chrome headless keeps all tabs visible.')
    parser.add_argument('--chrome-executable', default='C:/Program Files/Google/Chrome/Application/chrome.exe')
    args = parser.parse_args()
    artifacts = Path(args.artifacts).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    profile = Path(tempfile.mkdtemp(prefix='chrome-', dir=artifacts))
    url = urlsplit(args.url)
    url = urlunsplit((url.scheme, url.netloc, url.path,
                     urlencode({'state-dir': '/persistent/lifecycle-user'}), url.fragment))
    errors: list[str] = []
    messages: list[str] = []
    started = time.monotonic()
    context = None
    browser = None
    chrome_process = None
    page = None
    with sync_playwright() as playwright:
        def launch():
            nonlocal browser, chrome_process
            port_file = profile / 'DevToolsActivePort'
            port_file.unlink(missing_ok=True)
            command = [args.chrome_executable, f'--user-data-dir={profile}',
                       '--remote-debugging-port=0', '--no-first-run',
                       '--no-default-browser-check', '--disable-background-timer-throttling',
                       '--disable-backgrounding-occluded-windows',
                       '--window-position=-30000,-30000', '--window-size=450,950']
            if args.headless:
                command.append('--headless=new')
            command.append('about:blank')
            with (artifacts / 'chrome.log').open('a', encoding='utf-8') as chrome_log:
                chrome_process = subprocess.Popen(command, stdout=chrome_log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 15
            while not port_file.exists():
                assert chrome_process.poll() is None, 'Chrome exited before opening its debugging port'
                assert time.monotonic() < deadline, 'Chrome debugging port did not become ready'
                time.sleep(.05)
            port = port_file.read_text(encoding='utf-8').splitlines()[0]
            # Playwright normally forces every tab to appear focused/visible.
            # Attaching without its defaults preserves real browser lifecycle.
            browser = playwright.chromium.connect_over_cdp(
                f'http://127.0.0.1:{port}', no_defaults=True)
            return browser.contexts[0]

        def reopen():
            new_page = context.new_page()
            new_page.set_viewport_size({'width': 430, 'height': 860})
            observe(new_page, errors, messages)
            load(new_page, url)
            return new_page

        try:
            context = launch()
            print('Chrome:', context.browser.version)
            print('Persistent profile:', profile)
            page = context.pages[0]
            page.set_viewport_size({'width': 430, 'height': 860})
            # Keep the browser alive while closing the application's only tab.
            # Create this before loading the app, so setup hides no live model.
            keeper = context.new_page()
            keeper.goto('about:blank')
            page.bring_to_front()
            observe(page, errors, messages)
            load(page, url)
            expect(page.locator(ROWS)).to_have_count(0)
            assert durable_snapshot(page) == []
            control(page, 5002).click()
            child_page(page)
            first_id = active_id(page)
            main_page(page)
            control(page, 5005).click()
            expect(control(page, 5002)).to_have_text('Добавить')
            page.locator('#next-page').click()
            child_page(page, 'Окно 1')
            page.locator(ACTIVE + ' button').click()
            expect(page.locator(ROWS)).to_have_text(['Окно 1 (закрыто)'])
            assert durable_snapshot(page) == [], 'working actions or child Close wrote IndexedDB'
            assert snapshot(page) == [], 'working actions or child Close wrote USER file'
            page.locator(ROWS).first.dblclick()
            child_page(page, 'Окно 1')
            print('CASE: ordinary tab close')
            close_tab(page)

            page = reopen()
            child_page(page, 'Окно 1')
            assert active_id(page) == first_id
            saved = durable_snapshot(page)
            expect_one_file(saved)
            print('PASS: ordinary tab close restored active child, stable ID and locale')

            # Native reload immediately after enqueueing an Add handler in the
            # same JS turn: no model/publication settling wait before navigation.
            main_page(page)
            print('CASE: reload with the last Add command still queued')
            with page.expect_navigation(wait_until='domcontentloaded'):
                page.evaluate("""() => {
                  document.querySelector('button[data-apptraverse-control-id="5002"]').click();
                  location.reload();
                }""")
            child_page(page, 'Окно 2')
            instrument_checkpoint(page)
            second_id = active_id(page)
            expect(page.locator(ROWS)).to_have_text(['Окно 1', 'Окно 2'])
            saved = durable_snapshot(page)
            expect_one_file(saved)
            print('PASS: real reload drained the last queued Add command')

            # Actual navigation away must save a closed child's lazy state.
            page.locator(ACTIVE + ' button').click()
            expect(page.locator(ROWS)).to_have_text(['Окно 1', 'Окно 2 (закрыто)'])
            child_page(page, 'Окно 1')
            assert durable_snapshot(page) == saved, 'child Close wrote IndexedDB'
            page.goto('about:blank')
            load(page, url)
            child_page(page, 'Окно 1')
            assert active_id(page) == first_id
            expect(page.locator(ROWS)).to_have_text(['Окно 1', 'Окно 2 (закрыто)'])
            main_page(page)
            page.locator(ROWS).nth(1).dblclick()
            child_page(page, 'Окно 2')
            assert active_id(page) == second_id
            print('PASS: real navigation restored closed child and reopened its original ID')

            if not args.headless:
                # This is a real Chromium tab switch, not a dispatched event.
                identity = page.evaluate("window.__lifecycleLiveIdentity = crypto.randomUUID()")
                other = context.new_page()
                other.goto('about:blank')
                other.bring_to_front()
                page.wait_for_function("document.visibilityState === 'hidden'", polling=100)
                hidden_saved = durable_snapshot(page)
                expect_one_file(hidden_saved)
                page.bring_to_front()
                page.wait_for_function("document.visibilityState === 'visible'", polling=100)
                assert page.evaluate('window.__lifecycleLiveIdentity') == identity
                child_page(page, 'Окно 2')
                assert active_id(page) == second_id
                close_tab(other)
                print('PASS: actual tab hide/return kept the live model and persisted one USER file')

            main_page(page)
            control(page, 5005).click()
            expect(control(page, 5002)).to_have_text('Add')
            page.locator('#next-page').click()
            child_page(page, 'Window 1')
            before_quit = durable_snapshot(page)
            expect_one_file(before_quit)
            print('Before browser Quit:', page.evaluate('document.visibilityState'), page.locator('#page-title').inner_text())
            page.screenshot(path=str(artifacts / 'before-browser-close.png'), full_page=True)

            # CDP Browser.close skips page lifecycle in this Chrome version.
            # Exercise the actual native Close message on our own browser only.
            with context.expect_event('close', timeout=15000):
                if args.headless:
                    close_tab(page)
                    browser.new_browser_cdp_session().send('Browser.close')
                else:
                    close_native_browser_window(chrome_process.pid)
            chrome_process.wait(timeout=15)
            context = launch()
            page = context.pages[0]
            page.set_viewport_size({'width': 430, 'height': 860})
            observe(page, errors, messages)
            load(page, url)
            child_page(page, 'Window 1')
            assert active_id(page) == first_id
            expect(page.locator(ROWS)).to_have_text(['Window 1', 'Window 2'])
            after_quit = durable_snapshot(page)
            expect_one_file(after_quit)
            assert after_quit[0]['sha'] != before_quit[0]['sha'], 'browser Quit did not save last language change'
            main_page(page)
            # A real app-close click and navigation in the same event-loop turn
            # must not skip persistence merely because host stop has begun.
            page.evaluate("document.querySelector('button[data-apptraverse-control-id=\"5002\"]').click()")
            child_page(page, 'Window 3')
            with page.expect_navigation(wait_until='domcontentloaded'):
                page.evaluate("document.getElementById('shutdown-button').click(); location.reload()")
            child_page(page, 'Window 3')
            expect(page.locator(ROWS)).to_have_text(['Window 1', 'Window 2', 'Window 3'])
            print('PASS: application Close and reload in the same JS turn preserved the latest child')
            assert not errors, errors
            known_warning = 'error: Blocking on the main thread is very dangerous, see https://emscripten.org/docs/porting/pthreads.html#blocking-on-the-main-browser-thread'
            unexpected = [message for message in messages
                          if message.startswith('error:') and message != known_warning]
            assert not unexpected, unexpected
            print('PASS: full Chrome process restart restored the latest model from the same profile')
            print('Checkpoint timings:', [message for message in messages if 'LIFECYCLE_CAPTURE_MS=' in message])
            print(f'PASS: Web lifecycle acceptance ({time.monotonic() - started:.3f}s)')
            return 0
        except Exception as error:
            print('FAIL:', error)
            print('PAGEERROR:', errors)
            print('\n'.join(messages[-40:]))
            if page is not None and not page.is_closed():
                print('DOM:', page.locator('body').inner_text(timeout=3000))
                page.screenshot(path=str(artifacts / 'failed.png'), full_page=True)
            return 1
        finally:
            if browser is not None and browser.is_connected():
                browser.new_browser_cdp_session().send('Browser.close')
            if chrome_process is not None:
                chrome_process.wait(timeout=15)


if __name__ == '__main__':
    raise SystemExit(main())
