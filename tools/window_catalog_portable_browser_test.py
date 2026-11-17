"""Real browser USER import with optional LOCAL seed or existing-device precedence."""
from __future__ import annotations
import argparse
from pathlib import Path
from playwright.sync_api import expect, sync_playwright
from wasm_window_catalog_browser_smoke import ROWS, control, load, main_page, shutdown


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--profile', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--local-mode', choices=['existing', 'seed'], default='existing')
    args = parser.parse_args()
    manifest = dict(line.split('=', 1) for line in (args.profile / 'manifest.txt').read_text().splitlines() if '=' in line)
    count = int(manifest['row_count'])
    closed = next(i for i in range(count) if manifest[f'row.{i}.open'] == '0')
    errors = []
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(channel='chrome', headless=True)
        context = browser.new_context(viewport={'width': 430, 'height': 860})
        page = context.new_page()
        page.on('pageerror', lambda error: errors.append(str(error)))
        load(page, args.url)
        page.locator('#appearance-theme').select_option('1')
        page.wait_for_function("() => document.querySelector('.catalog-page').dataset.apptraverseTheme === 'light'")
        files = shutdown(page)
        profile_file = next(f['path'] for f in files if '/local/' not in f['path'])
        local_file = profile_file.rsplit('/', 1)[0] + '/local/user.bin'
        source_local = (args.profile / 'local' / 'user.bin').read_bytes()
        page.evaluate('''async ({path, bytes, localPath, seed, mode}) => {
          const fs = Module.FS;
          fs.writeFile(path, new Uint8Array(bytes));
          fs.mkdirTree('/persistent/local-seed');
          // Existing LOCAL must bypass even an invalid optional seed.
          fs.writeFile('/persistent/local-seed/user.bin', new Uint8Array(mode === 'seed' ? seed : [1,2,3]));
          if (mode === 'seed') fs.unlink(localPath);
          await new Promise((resolve, reject) => fs.syncfs(false, e => e ? reject(e) : resolve()));
        }''', {'path': profile_file, 'bytes': list((args.profile / 'user.bin').read_bytes()),
               'localPath': local_file, 'seed': list(source_local), 'mode': args.local_mode})
        load(page, args.url)
        main_page(page)
        expect(page.locator(ROWS)).to_have_count(count)
        expect(control(page, 5002)).to_have_text('Добавить' if manifest['language'] == '1' else 'Add')
        expected_theme = '2' if args.local_mode == 'seed' else '1'
        expect(page.locator('#appearance-theme')).to_have_value(expected_theme)
        if args.local_mode == 'seed': expect(page.locator('#appearance-soft')).to_be_checked()
        for i in range(count):
            if manifest[f'row.{i}.open'] == '1':
                expect(page.locator('[data-window-id="' + manifest[f'row.{i}.window'] + '"]')).to_have_count(1)
        page.locator(ROWS).nth(closed).dblclick()
        expect(page.locator('[data-window-id="' + manifest[f'row.{closed}.window'] + '"]')).to_have_count(1)
        title = ('Окно ' if manifest['language'] == '1' else 'Window ') + manifest[f'row.{closed}.number']
        expect(page.locator('#page-title')).to_have_text(title)
        shutdown(page)
        load(page, args.url)
        expect(page.locator('#page-title')).to_have_text(title)
        expect(page.locator('#appearance-theme')).to_have_value(expected_theme)
        shutdown(page)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        for source, destination in [(profile_file, args.output), (local_file, args.output.parent / 'local' / 'user.bin')]:
            data = page.evaluate('path => Array.from(Module.FS.readFile(path))', source)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(bytes(data))
        assert not errors, errors
        context.close(); browser.close()
    print('PORTABLE_BROWSER_PASS: unchanged USER import, LOCAL=' + args.local_mode +
          ', native identity/locale, closed child reopen, save/restart')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
