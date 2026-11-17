"""Run a real browser through separately distilled supply defaults and USER override."""
from pathlib import Path
import argparse
import subprocess
from playwright.sync_api import sync_playwright
from wasm_window_catalog_browser_smoke import load, main_page, shutdown


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--url', required=True)
    p.add_argument('--update-url', required=True)
    p.add_argument('--fixture', required=True)
    p.add_argument('--node', required=True)
    p.add_argument('--artifacts', type=Path, required=True)
    a = p.parse_args(); a.artifacts.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as pw:
        browser = pw.chromium.launch(channel='chrome', headless=True)
        context = browser.new_context(viewport={'width': 430, 'height': 860})
        page = context.new_page(); errors = []
        page.on('pageerror', lambda e: errors.append(str(e)))
        def expect_padding(url, expected):
            load(page, url); main_page(page)
            page.wait_for_function('(x) => parseFloat(document.querySelector(".catalog-page-main [data-apptraverse-control-id=\\"5001\\"]").style.left) === x', arg=expected)
        def export_profile(directory):
            directory.mkdir(parents=True, exist_ok=True)
            files = shutdown(page)
            for f in files:
                dest = directory / ('local/user.bin' if '/local/' in f['path'] else 'user.bin')
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(bytes(page.evaluate('p => Array.from(Module.FS.readFile(p))', f['path'])))
            return next(f['path'] for f in files if '/local/' not in f['path'])
        def inject_user(path, profile):
            page.evaluate('''async ({path, bytes}) => {
              Module.FS.writeFile(path, new Uint8Array(bytes));
              await new Promise((resolve,reject) => Module.FS.syncfs(false, e => e ? reject(e) : resolve()));
            }''', {'path': path, 'bytes': list((profile / 'user.bin').read_bytes())})
        expect_padding(a.url, 12)
        user_path = export_profile(a.artifacts / 'supply-a-user')
        expect_padding(a.update_url, 24)
        current = a.artifacts / 'supply-b-user'; export_profile(current)
        override = a.artifacts / 'override-user'
        subprocess.run([a.node, a.fixture, 'set-layout-override', str(current.resolve()), str(override.resolve())], check=True)
        inject_user(user_path, override)
        expect_padding(a.update_url, 44)
        overridden = a.artifacts / 'override-export'; export_profile(overridden)
        cleared = a.artifacts / 'cleared-user'
        subprocess.run([a.node, a.fixture, 'clear-layout-override', str(overridden.resolve()), str(cleared.resolve())], check=True)
        inject_user(user_path, cleared)
        expect_padding(a.update_url, 24); shutdown(page)
        expect_padding(a.update_url, 24); shutdown(page)
        assert not errors, errors
        context.close(); browser.close()
    print('PASS: actual Web supply A(default12) -> independently distilled B(default24); USER override44 survives B, ClearOverride and cold reload select B(default24)')

if __name__ == '__main__': main()
