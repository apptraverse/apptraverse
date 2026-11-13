"""Import an unchanged desktop USER into an isolated browser, edit and restart."""
from __future__ import annotations

import argparse
from pathlib import Path

from playwright.sync_api import expect, sync_playwright
from wasm_window_catalog_browser_smoke import (
    ROWS, control, load, main_page, shutdown,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--profile', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    manifest = dict(line.split('=', 1) for line in
                    (args.profile / 'manifest.txt').read_text().splitlines())
    assert manifest['row_count'] == '3' and manifest['language'] == '1'
    assert manifest['row.1.open'] == '0'
    errors = []
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(channel='chrome', headless=True)
        context = browser.new_context(viewport={'width': 430, 'height': 860})
        page = context.new_page()
        page.on('pageerror', lambda error: errors.append(str(error)))
        # Use the application's own filesystem mount and sync, in a new browser
        # context. No manual IndexedDB envelope or normal user profile is used.
        load(page, args.url)
        files = shutdown(page)
        profile_file = files[0]['path']
        page.evaluate('''async ({path, bytes}) => {
          Module.FS.writeFile(path, new Uint8Array(bytes));
          await new Promise((resolve, reject) => Module.FS.syncfs(false,
            error => error ? reject(error) : resolve()));
        }''', {'path': profile_file,
               'bytes': list((args.profile / 'user.bin').read_bytes())})
        load(page, args.url)
        main_page(page)
        expect(page.locator(ROWS)).to_have_count(3)
        expect(control(page, 5002)).to_have_text('Добавить')
        expect(page.locator('.catalog-page')).to_have_count(3)
        for index in (0, 2):
            expect(page.locator(
                '[data-window-id="' + manifest[f'row.{index}.window'] + '"]'
            )).to_have_count(1)
        # Open a cold closed child through the real browser presenter/model.
        page.locator(ROWS).nth(1).dblclick()
        expect(page.locator('.catalog-page')).to_have_count(4)
        expect(page.locator('#page-title')).to_have_text('Окно 2')
        shutdown(page)
        load(page, args.url)
        expect(page.locator('.catalog-page')).to_have_count(4)
        expect(page.locator('#page-title')).to_have_text('Окно 2')
        shutdown(page)
        data = page.evaluate('path => Array.from(Module.FS.readFile(path))', profile_file)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(bytes(data))
        assert not errors, errors
        context.close()
        browser.close()
    print('PORTABLE_BROWSER_PASS: unchanged USER import, native page identities, '
          'Russian locale, cold child reopen, save/restart')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
