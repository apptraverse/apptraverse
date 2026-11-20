"""Real DOM acceptance for model-published page fullscreen and layout commands."""
from pathlib import Path
import argparse
from urllib.parse import urlencode
from playwright.sync_api import expect, sync_playwright
from wasm_window_catalog_browser_smoke import ACTIVE, ROWS, control, load, main_page, shutdown, snapshot


def fullscreen(page, value):
    expect(page.locator('#fullscreen-button')).to_have_attribute('aria-pressed', str(value).lower())
    if value:
        page.wait_for_function('''() => {
          const r = document.querySelector('.page-active').getBoundingClientRect();
          const header = document.querySelector('#pager-bar').getBoundingClientRect();
          return Math.abs(r.left) < 1 && Math.abs(r.right-innerWidth) < 1 &&
              Math.abs(r.bottom-innerHeight) < 1 && r.top >= header.bottom-1;
        }''')
        for selector in ['#application-menu', '#layout-actions', '#page-dots', '#status']:
            expect(page.locator(selector)).to_be_hidden()
        assert page.evaluate('document.fullscreenElement === null')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--url', required=True)
    parser.add_argument('--load-url', required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as pw:
        browser = pw.chromium.launch(channel='chrome', headless=True)
        context = browser.new_context(viewport={'width': 1200, 'height': 860})
        page = context.new_page()
        errors = []
        page.on('pageerror', lambda error: errors.append(str(error)))
        load(page, args.url)
        expect(page.locator('#layout-actions button')).to_have_count(4)
        page.locator('#layout-actions button').nth(1).click()
        expect(page.locator('#layout-actions button').nth(1)).to_have_attribute('aria-pressed', 'true')
        page.locator('#layout-actions button').nth(3).click()
        expect(page.locator('#layout-actions button').first).to_have_attribute('aria-pressed', 'true')
        control(page, 5002).click()
        expect(page.locator('#layout-actions')).to_be_hidden()
        child_id = page.locator(ACTIVE).get_attribute('data-window-id')
        base = shutdown(page)
        profile = next(f['path'].rsplit('/', 1)[0] for f in base if '/local/' not in f['path'])
        cold = args.load_url + '?' + urlencode({'state-dir': profile})
        load(page, cold)
        expect(page.locator(ACTIVE)).to_have_attribute('data-window-id', child_id)
        page.locator('#fullscreen-button').click()
        fullscreen(page, True)
        page.locator('#prev-page').click()
        fullscreen(page, False)
        page.locator('#catalog-menu-1').select_option('1')
        page.locator('#next-page').click()
        fullscreen(page, True)
        expect(page.locator('#fullscreen-button')).to_have_text('Выйти из полноэкранного режима')
        page.set_viewport_size({'width': 430, 'height': 420})
        fullscreen(page, True)
        page.screenshot(path=str(args.artifacts / 'fullscreen-ru-narrow.png'))
        # Escape still exits with a native button focused.
        page.locator('#fullscreen-button').focus()
        page.keyboard.press('Escape')
        fullscreen(page, False)
        page.locator('#fullscreen-button').click()
        fullscreen(page, True)
        assert snapshot(page) == base, 'ordinary display/locale actions wrote disk'
        saved = shutdown(page)
        user_sha = lambda files: next(f['sha'] for f in files if '/local/' not in f['path'])
        assert user_sha(saved) == user_sha(base), 'fullscreen/locale changed portable USER'
        load(page, cold)
        fullscreen(page, True)
        expect(page.locator(ACTIVE)).to_have_attribute('data-window-id', child_id)
        # Close preserves the child's LOCAL preference; reopening its original
        # identity must restore fullscreen after the child publication arrives.
        page.locator(ACTIVE).get_by_role('button', name='Закрыть', exact=True).click()
        fullscreen(page, False)
        expect(page.locator(ROWS)).to_have_text(['Окно 1 (закрыто)'])
        page.locator(ROWS).first.dblclick()
        expect(page.locator(ACTIVE)).to_have_attribute('data-window-id', child_id)
        fullscreen(page, True)
        page.locator('#prev-page').click()
        expect(page.locator('.catalog-page-main.page-active')).to_have_count(1)
        fullscreen(page, False)
        page.locator('#fullscreen-button').click()
        fullscreen(page, True)
        page.keyboard.press('Escape')
        fullscreen(page, False)
        page.locator('#catalog-menu-1').focus()
        page.locator('#fullscreen-button').click()
        fullscreen(page, True)
        # A synthetic select-targeted Escape represents a popup owning the key.
        page.locator('#catalog-menu-1').dispatch_event('keydown', {'key': 'Escape', 'bubbles': True})
        fullscreen(page, True)
        page.locator('#fullscreen-button').focus()
        page.keyboard.press('Escape')
        fullscreen(page, False)
        page.locator(ROWS).first.click()
        control(page, 5004).click()
        expect(page.locator(ROWS)).to_have_count(0)
        expect(page.locator('.catalog-page')).to_have_count(1)
        shutdown(page)
        assert not errors, errors
        context.close()
        browser.close()
    print('PASS: Web localized layout, published viewport fullscreen, safe paging/Escape, USER unchanged, LOCAL cold restore, Close/reopen preserves fullscreen, Delete removes child')


if __name__ == '__main__':
    main()
