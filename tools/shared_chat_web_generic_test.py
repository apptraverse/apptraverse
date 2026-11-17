#!/usr/bin/env python3
"""Explicit real-cloud generic chat acceptance in headless Chrome; own profiles only."""
import argparse
import time
from pathlib import Path
from playwright.sync_api import sync_playwright

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--url',default='http://127.0.0.1:8788/')
    parser.add_argument('--profile-dir',required=True)
    args=parser.parse_args()
    errors=[]
    stamp=str(time.time_ns())
    def control(page,id): return page.locator(f'[data-apptraverse-control-id="{id}"]')
    def ready(page):
        control(page,107).wait_for(timeout=90000)
        page.wait_for_function('!document.querySelector(\'[data-apptraverse-control-id="108"]\').disabled',timeout=90000)
    with sync_playwright() as p:
        context=p.chromium.launch_persistent_context(str(Path(args.profile_dir).resolve()),headless=True,channel='chrome',viewport={'width':1000,'height':720},permissions=['clipboard-read','clipboard-write'])
        host=context.new_page();client=context.new_page()
        for page in [host,client]:page.on('pageerror',lambda e:errors.append(str(e)))
        host.goto(args.url+'?profile=host'+stamp+'&name=WebHost&loadOnly=1',timeout=120000);ready(host)
        host.bring_to_front();control(host,102).click()
        host.wait_for_function('document.querySelector(\'[data-apptraverse-control-id="109"]\').textContent.toLowerCase().includes("copied")',timeout=30000)
        uid=host.evaluate('navigator.clipboard.readText()')
        assert uid and control(host,101).input_value()==uid
        client.goto(args.url+'?profile=client'+stamp+'&role=client&name=WebClient&loadOnly=1',timeout=120000)
        control(client,103).wait_for(timeout=90000)
        control(client,103).press_sequentially(uid,delay=0)
        control(client,104).click();ready(client)
        host.wait_for_function('document.querySelector(\'[data-apptraverse-control-id="105"]\').textContent.includes("WebClient")',timeout=90000)
        assert control(client,107).get_attribute("aria-label")=="Message"
        assert control(client,106).get_attribute("aria-label")=="Messages"
        control(client,107).focus();control(client,107).press_sequentially('rapid-native-input',delay=0)
        assert control(client,107).input_value()=='rapid-native-input'
        control(client,107).press('Home');control(client,107).press('ArrowRight');control(client,107).press('ArrowRight')
        control(host,107).fill('unrelated publication');control(host,108).click()
        client.wait_for_function('document.querySelector(\'[data-apptraverse-control-id="106"]\').value.includes("unrelated publication")',timeout=60000)
        assert control(client,107).input_value()=='rapid-native-input'
        assert control(client,107).evaluate('(el)=>document.activeElement===el&&el.selectionStart===2&&el.selectionEnd===2')
        control(client,107).press('Enter')
        host.wait_for_function('document.querySelector(\'[data-apptraverse-control-id="106"]\').value.includes("rapid-native-input")',timeout=60000)
        client.wait_for_function('document.querySelector(\'[data-apptraverse-control-id="107"]\').value===""')
        client.wait_for_function('document.querySelector(\'[data-apptraverse-control-id="106"]\').value.includes("rapid-native-input")',timeout=60000)
        assert control(client,106).input_value().count('rapid-native-input')==1
        assert control(client,106).evaluate('(el)=>el.readOnly')
        control(client,107).fill('button-message Привет 🌍');control(client,108).click();control(client,107).fill('new-draft')
        host.wait_for_function('document.querySelector(\'[data-apptraverse-control-id="106"]\').value.includes("button-message")',timeout=60000)
        assert 'Привет 🌍' in control(host,106).input_value()
        assert control(client,107).input_value()=='new-draft',(control(client,107).input_value(),control(client,107).evaluate('(el)=>document.activeElement===el'))
        assert control(client,107).evaluate('(el)=>document.activeElement===el')
        client_uid=control(client,101).input_value()
        saved=client.evaluate('Module._SharedSave()')
        assert saved==1
        client.reload();control(client,103).wait_for(timeout=90000)
        assert control(client,107).input_value()=='' and control(client,106).input_value()==''
        assert control(client,103).input_value()==''
        # The native business USER path is absent; LOCAL is an ordinary native file.
        assert client.evaluate('FS.analyzePath("/shared-ui/client'+stamp+'/local/user.bin").exists')
        assert not client.evaluate('FS.analyzePath("/shared-ui/client'+stamp+'/user.bin").exists')
        assert not client.evaluate('FS.analyzePath("/shared-network/client'+stamp+'/user.bin").exists')
        client.wait_for_function('!document.querySelector(\'[data-apptraverse-control-id="104"]\').disabled',timeout=90000)
        assert control(client,101).input_value()==client_uid
        control(client,103).fill(uid);control(client,104).click();ready(client)
        client.wait_for_function('document.querySelector(\'[data-apptraverse-control-id="106"]\').value.includes("button-message")',timeout=90000)
        control(client,107).fill('after-rejoin');control(client,107).press('Enter')
        host.wait_for_function('document.querySelector(\'[data-apptraverse-control-id="106"]\').value.includes("after-rejoin")',timeout=60000)
        assert not errors,errors
        context.close()
    print('PASS Chrome native copy/join/all-peer send, rapid input/caret, Enter once, button focus/new draft, LOCAL, RAM restart and same-SDK rejoin/send')
if __name__=='__main__':main()
