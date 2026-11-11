#!/usr/bin/env python3
"""WindowCatalog browser acceptance: native input, RAM USER and cold IDBFS reload."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
from urllib.parse import urlencode, urlsplit, urlunsplit
from playwright.sync_api import Page, expect, sync_playwright

MAIN = ".catalog-page-main"
ROWS = MAIN + " .apptraverse-list-row"
ACTIVE = ".catalog-page.page-active"


def control(page: Page, control_id: int):
    return page.locator(f'{MAIN} button[data-apptraverse-control-id="{control_id}"]')


def main_page(page: Page) -> None:
    if not page.locator(MAIN).evaluate("el => el.classList.contains('page-active')"):
        page.locator('#page-dots button').first.click()
    expect(page.locator(MAIN)).to_have_class(re.compile(r".*page-active.*"))


def child_page(page: Page, title: str = "Window 1") -> None:
    expect(page.locator('#page-title')).to_have_text(title)
    expect(page.locator(ACTIVE + ':not(.catalog-page-main)')).to_have_count(1)
    assert page.locator(ACTIVE).evaluate("el => Number(el.dataset.windowId) > 0"), 'ObjId became signed across Wasm/JS'


def snapshot(page: Page):
    return page.evaluate("""async () => {
      const fs = Module.FS;
      const result = [];
      async function walk(path) {
        if (!fs.analyzePath(path).exists) return;
        for (const name of fs.readdir(path)) {
          if (name === '.' || name === '..') continue;
          const child = path + '/' + name, stat = fs.stat(child);
          if (fs.isDir(stat.mode)) await walk(child);
          else {
            const bytes = fs.readFile(child);
            const digest = await crypto.subtle.digest('SHA-256', bytes);
            result.push({path: child, size: bytes.length, mtime: +stat.mtime,
              sha: Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, '0')).join('')});
          }
        }
      }
      await walk('/persistent');
      return result.sort((a,b) => a.path.localeCompare(b.path));
    }""")


def load(page: Page, url: str) -> None:
    page.goto(url, wait_until='domcontentloaded', timeout=60000)
    expect(page.locator(ACTIVE)).to_have_count(1, timeout=60000)
    expect(control(page, 5002)).to_have_count(1)
    assert page.evaluate("crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined'")


def shutdown(page: Page):
    page.locator('#shutdown-button').click()
    expect(page.locator('#status')).to_have_text('Stopped', timeout=60000)
    files = snapshot(page)
    assert files and all(f['path'].endswith('/user.bin') and f['size'] > 0 for f in files), files
    assert len({f['path'].rsplit('/', 1)[0] for f in files}) == len(files), files
    assert page.locator('.catalog-page').count() == 0
    return files


def check_child_layout(page: Page) -> None:
    page.wait_for_function("""() => {
      const mount = document.querySelector('#catalog-mount').getBoundingClientRect();
      const button = document.querySelector('.catalog-page.page-active:not(.catalog-page-main) button');
      if (!button) return false;
      const r = button.getBoundingClientRect();
      return r.width >= mount.width * .8 && r.bottom <= mount.bottom + 1 &&
        r.top >= mount.top + mount.height * .7;
    }""", timeout=15000)


def swipe_page(page: Page, delta: int) -> None:
    mount = page.locator('#catalog-mount').bounding_box()
    assert mount is not None
    y = mount['y'] + mount['height'] * .35
    left, right = mount['x'] + 70, mount['x'] + mount['width'] - 70
    start, end = (right, left) if delta > 0 else (left, right)
    session = page.context.new_cdp_session(page)
    try:
        session.send('Input.dispatchTouchEvent', {
            'type': 'touchStart', 'touchPoints': [{'x': start, 'y': y}]})
        session.send('Input.dispatchTouchEvent', {
            'type': 'touchMove', 'touchPoints': [{'x': end, 'y': y}]})
        session.send('Input.dispatchTouchEvent', {
            'type': 'touchEnd', 'touchPoints': []})
    finally:
        session.detach()


def acceptance(page: Page, fresh_url: str, load_url: str, screenshot: Path) -> None:
    load(page, fresh_url)
    expect(page.locator(ROWS)).to_have_count(0)
    before = snapshot(page)
    assert before == [], before
    control(page, 5002).click()
    child_page(page)
    check_child_layout(page)
    swipe_page(page, 1)
    expect(page.locator(MAIN)).to_have_class(re.compile(r".*page-active.*"))
    swipe_page(page, -1)
    child_page(page)
    page.set_viewport_size({'width': 812, 'height': 480})
    check_child_layout(page)
    page.set_viewport_size({'width': 430, 'height': 860})
    check_child_layout(page)
    page.locator(ACTIVE).get_by_role('button', name='Close', exact=True).click()
    expect(page.locator(ROWS)).to_have_text(['Window 1 (Closed)'])
    page.locator(ROWS).first.click()
    control(page, 5003).click()
    child_page(page)
    main_page(page)
    control(page, 5005).click()
    expect(page.locator('#page-title')).to_have_text('Каталог окон')
    expect(control(page, 5002)).to_have_text('Добавить')
    page.locator('#next-page').click()
    child_page(page, 'Окно 1')
    expect(page.locator(ACTIVE + ' button')).to_have_text('Закрыть')
    main_page(page)
    control(page, 5005).click()
    expect(control(page, 5002)).to_have_text('Add')
    control(page, 5002).click()
    child_page(page, 'Window 2')
    main_page(page)
    expect(page.locator(ROWS)).to_have_text(['Window 1', 'Window 2'])
    page.locator(ROWS).first.click()
    control(page, 5004).click()
    expect(page.locator(ROWS)).to_have_text(['Window 2'])
    page.locator(ROWS).first.dblclick()
    child_page(page, 'Window 2')
    expect(page.locator('.catalog-page')).to_have_count(2)
    page.evaluate("window.dispatchEvent(new Event('pagehide')); document.dispatchEvent(new Event('visibilitychange'))")
    assert snapshot(page) == before, 'UI actions/child Close/background notification wrote USER'
    screenshot.parent.mkdir(parents=True, exist_ok=True)
    page.screenshot(path=str(screenshot), full_page=True)
    saved = shutdown(page)
    assert len(saved) == 1
    url = urlsplit(load_url)
    load_url = urlunsplit((url.scheme, url.netloc, url.path,
                          urlencode({'state-dir': saved[0]['path'].rsplit('/', 1)[0]}), url.fragment))

    load(page, load_url)
    child_page(page, 'Window 2')
    assert snapshot(page) == saved, 'startup rewrote USER'
    page.locator(ACTIVE + ' button').click()
    expect(page.locator(ROWS)).to_have_text(['Window 2 (Closed)'])
    assert snapshot(page) == saved, 'child Close wrote USER'
    page.locator(ROWS).first.dblclick()
    child_page(page, 'Window 2')
    main_page(page)
    control(page, 5005).click()
    expect(control(page, 5002)).to_have_text('Добавить')
    page.locator('#next-page').click()
    child_page(page, 'Окно 2')
    assert snapshot(page) == saved, 'locale/publications wrote USER'
    saved_ru = shutdown(page)
    assert saved_ru[0]['sha'] != saved[0]['sha'], 'shutdown did not save changed language'

    load(page, load_url)
    child_page(page, 'Окно 2')
    expect(page.locator(ACTIVE + ' button')).to_have_text('Закрыть')
    assert snapshot(page) == saved_ru
    shutdown(page)
    prior_debug = snapshot(page)
    load(page, fresh_url)
    expect(page.locator(ROWS)).to_have_count(0)
    expect(page.locator('#page-title')).to_have_text('Window Catalog')
    assert snapshot(page) == prior_debug, 'fresh Debug loaded or rewrote prior USER'
    new_debug = shutdown(page)
    assert len(new_debug) == 2 and prior_debug[0] in new_debug


def main() -> int:
    sys.stdout.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser()
    parser.add_argument('--url', default='http://127.0.0.1:8765/web_window_catalog_demo.html')
    parser.add_argument('--load-only-url')
    parser.add_argument('--screenshot', default='docs/smoke_artifacts/window_catalog_web_initial.png')
    args = parser.parse_args()
    load_url = args.load_only_url or args.url.replace('web_window_catalog_demo.html', 'web_window_catalog_demo_load_only.html')
    errors: list[str] = []
    messages: list[str] = []
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True, channel='chrome')
        context = browser.new_context(viewport={'width': 430, 'height': 860}, has_touch=True)
        page = context.new_page()
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.on('console', lambda msg: messages.append(msg.type + ': ' + msg.text))
        try:
            acceptance(page, args.url, load_url, Path(args.screenshot))
            assert not errors, errors
            unexpected = [m for m in messages if m.startswith('error:') and 'favicon' not in m and 'Failed to load resource:' not in m]
            assert not unexpected, unexpected
            print('PASS: Web WindowCatalog input, model pager/viewport, child Close, one USER file, two cold restarts, fresh Debug')
            return 0
        except Exception as error:
            print('FAIL:', error)
            print('PAGEERROR:', errors)
            print('\n'.join(messages[-40:]))
            print('DOM:', page.locator('body').inner_text(timeout=3000))
            Path(args.screenshot).parent.mkdir(parents=True, exist_ok=True)
            page.screenshot(path=str(Path(args.screenshot).with_suffix('.failed.png')), full_page=True)
            return 1
        finally:
            context.close()
            browser.close()


if __name__ == '__main__':
    raise SystemExit(main())
