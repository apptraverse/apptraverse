"""Real DOM input, generic layout and IDBFS cold state for the Surface adapter."""
import argparse
import json
import time
from pathlib import Path
from urllib.parse import urlencode
from playwright.sync_api import sync_playwright, expect

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--url', required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    errors, logs = [], []
    def control(page, number):
        return page.locator(f'.apptraverse-desktop-window.active [data-apptraverse-control-id="{number}"]')
    with sync_playwright() as pw:
        browser = pw.chromium.launch(channel='chrome', headless=True)
        context = browser.new_context(viewport={'width': 900, 'height': 500})
        page = context.new_page()
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.on('console', lambda message: logs.append(message.type + ': ' + message.text))
        try:
            base = '/persistent/surface-test-' + str(time.time_ns())
            page.goto(args.url + '?' + urlencode({'state-dir': base}))
            expect(control(page, 1001)).to_be_visible(timeout=60000)
            assert page.evaluate('crossOriginIsolated')
            page.locator('.apptraverse-desktop-window').evaluate('(el)=>el.__identity=123')
            control(page, 1001).click()
            expect(page.locator('#tabs button')).to_have_count(2)
            expect(page.locator('#tabs button[aria-selected=true]')).to_have_text('Surface 2')
            control(page, 1001).click()
            expect(page.locator('#tabs button')).to_have_count(3)
            page.locator('#tabs button', has_text='Surface 2').click()
            expect(page.locator('#tabs button[aria-selected=true]')).to_have_text('Surface 2')
            control(page, 1002).click()
            expect(page.locator('#tabs button')).to_have_count(2)
            assert page.locator('.apptraverse-desktop-window').first.evaluate('(el)=>el.__identity') == 123
            control(page, 1003).click()
            expect(page.locator('#tabs button').first).to_have_text('Поверхность 1')
            expect(control(page, 1001)).to_have_text('Добавить')
            expect(control(page, 1002)).to_have_text('Закрыть это окно')
            page.set_viewport_size({'width': 360, 'height': 760})
            page.wait_for_function('''() => {
              const nodes=[...document.querySelectorAll('.active .apptraverse-push-button')];
              return nodes.length===3 && nodes.every(n=>n.getBoundingClientRect().width>0) &&
                nodes[1].getBoundingClientRect().top > nodes[0].getBoundingClientRect().top;
            }''')
            accepted = page.evaluate('Module._ApptraverseSurfaceUiHidden()')
            assert accepted == 1
            page.evaluate('''() => new Promise((resolve,reject)=>Module.FS.syncfs(false,e=>e?reject(e):resolve()))''')
            profiles = page.evaluate('''base => Module.FS.readdir(base).filter(x=>x.startsWith('debug-run-')).map(x=>base+'/'+x+'/user')''', base)
            assert len(profiles) == 1
            profile = profiles[0]
            assert page.evaluate('path=>Module.FS.analyzePath(path+"/user.bin").exists && Module.FS.analyzePath(path+"/local/user.bin").exists', profile)
            page.goto(args.url + '?' + urlencode({'state-dir': profile, 'load': '1'}))
            expect(control(page, 1001)).to_be_visible(timeout=60000)
            expect(page.locator('#tabs button')).to_have_count(2)
            expect(page.locator('#tabs button').first).to_have_text('Поверхность 1')
            expect(page.locator('#tabs button').last).to_have_text('Поверхность 3')
            expect(page.locator('#tabs button[aria-selected=true]')).to_have_text('Поверхность 3')
            expect(control(page, 1001)).to_have_text('Добавить')
            control(page, 1002).click()
            expect(page.locator('#tabs button')).to_have_count(1)
            page.screenshot(path=str(args.artifacts / 'surface-web.png'))
            page.locator('#shutdown').click()
            expect(page.locator('#status')).to_have_text('Saved and closed', timeout=30000)
            expect(page.locator('.apptraverse-desktop-window')).to_have_count(0)
            page.wait_for_function('Module.count_emval_handles() === 0')
            assert not errors, errors
            print('PASS Surface Chrome DOM Add/Close/current/identity, RU, orientation/layout, hidden USER+LOCAL, cold state and shutdown')
        finally:
            (args.artifacts / 'surface-web-errors.json').write_text(json.dumps({'errors':errors, 'logs':logs}, ensure_ascii=False), encoding='utf-8')
            context.close()
            browser.close()
if __name__ == '__main__':
    main()
