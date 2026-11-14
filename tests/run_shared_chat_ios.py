#!/usr/bin/env python3
"""Explicit Mac/iPhone SwiftUI cloud test; product must be installed on simulator.

Build tests/ios_chat_ui/SharedChatUi.xcodeproj with build-for-testing first.
Private profiles and XCTest artifacts stay in --out; no Windows builds.
"""
import argparse
import asyncio
import json
from pathlib import Path
import plistlib
import time
import uuid

async def main(args):
    args.out.mkdir(parents=True, exist_ok=False)
    token = uuid.uuid4().hex[:10]
    state = {}
    seen = set()
    pids = {}
    tasks = []
    host_log = (args.out/'host.log').open('w')
    test_log = (args.out/'xctest.log').open('w')
    host = await asyncio.create_subprocess_exec(str(args.host), '--host', '--load-only', '--name', 'MacHost',
        '--profile', str(args.out/'host-profile'), stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
    async def send(text, delay=0):
        if delay: await asyncio.sleep(delay)
        host.stdin.write((json.dumps(dict(action='send',text=token+' '+text))+'\n').encode())
        await host.stdin.drain()
    async def read():
        nonlocal state
        async for raw in host.stdout:
            line=raw.decode(errors='replace'); host_log.write(line); host_log.flush()
            if not line.startswith('CHATUI '): continue
            state=json.loads(line[7:])
            for source, reply, delay in [('phone-first','Mac reply: Привет 🌍 你好',0),
                    ('before-background','sent while phone backgrounded',3),
                    ('phone-after-background','Mac confirms background',0),
                    ('phone-after-restart','Mac confirms restart',0)]:
                text=token+' '+source
                if text not in seen and any(text in m for m in state['messages']):
                    if not any(text in m for m in state['visible_strings']): continue
                    seen.add(text); print('PASS Mac renders '+source,flush=True)
                    rows=(await subprocess_output(['xcrun','simctl','spawn',args.device,'launchctl','list'])).splitlines()
                    matches=[row.split()[0] for row in rows if 'UIKitApplication:org.apptraverse.SharedNodeChat.ios[' in row]
                    assert len(matches)==1 and matches[0].isdigit(),matches
                    pids[source]=int(matches[0])
                    tasks.append(asyncio.create_task(send(reply,delay)))
    reader=asyncio.create_task(read())
    status='FAIL'
    test=None
    run_file=None
    try:
        deadline=time.monotonic()+180
        while not (state.get('ready') and state.get('in_room') and state.get('invite')):
            if host.returncode is not None or time.monotonic()>deadline: raise RuntimeError('Mac host failed to initialize')
            await asyncio.sleep(.2)
        container=await subprocess_output(['xcrun','simctl','get_app_container',args.device,'org.apptraverse.SharedNodeChat.ios','data'])
        profile=Path(container)/'Library/Application Support/AppTraverse/SharedNodeChat'/('acceptance-'+token)
        config=plistlib.loads(args.xctestrun.read_bytes())
        entry=config['SharedChatUi']
        entry.setdefault('EnvironmentVariables',{}).update(CHAT_HOST_UID=state['invite'],CHAT_PROFILE=str(profile),CHAT_TOKEN=token)
        run_file=args.xctestrun.with_name('SharedChatUi-'+token+'.xctestrun')
        run_file.write_bytes(plistlib.dumps(config))
        test=await asyncio.create_subprocess_exec('xcodebuild','test-without-building','-xctestrun',str(run_file),
            '-destination','platform=iOS Simulator,id='+args.device,'-destination-timeout','60',
            '-parallel-testing-enabled','NO','-maximum-concurrent-test-simulator-destinations','1',
            '-resultBundlePath',str(args.out/'ui.xcresult'),stdout=test_log,stderr=asyncio.subprocess.STDOUT)
        code=await asyncio.wait_for(test.wait(),780)
        if code: raise RuntimeError('XCTest failed: '+str(code))
        assert len(seen)==4,seen
        assert pids['phone-first']==pids['phone-after-background'],pids
        assert pids['phone-first']!=pids['phone-after-restart'],pids
        assert (profile/'user.bin').stat().st_size>0
        assert (profile/'transport'/'user.bin').stat().st_size>0
        for location in (profile,profile/'transport'):
            assert not (location/'native').exists() and not (location/'root.id').exists()
        await asyncio.gather(*tasks)
        host.stdin.write(b'{"action":"quit"}\n');await host.stdin.drain()
        assert await asyncio.wait_for(host.wait(),45)==0
        assert (args.out/'host-profile'/'user.bin').stat().st_size>0
        assert (args.out/'host-profile'/'transport'/'user.bin').stat().st_size>0
        status='PASS'
        print('PASS native iPhone Join/Send, Mac Unicode reply, background and cold restore',flush=True)
    finally:
        if test and test.returncode is None: test.kill();await test.wait()
        if host.returncode is None: host.kill();await host.wait()
        reader_result=(await asyncio.gather(reader,return_exceptions=True))[0]
        task_results=await asyncio.gather(*tasks,return_exceptions=True)
        errors=[str(error) for error in [reader_result,*task_results] if isinstance(error,BaseException)]
        if errors: status='FAIL'
        host_log.close();test_log.close()
        if run_file: run_file.unlink(missing_ok=True)
        (args.out/'result.json').write_text(json.dumps(dict(status=status,checks=sorted(seen),process_ids=pids,errors=errors),indent=2)+'\n')
        if errors: raise RuntimeError('Host acceptance driver failed: '+'; '.join(errors))

async def subprocess_output(args):
    proc=await asyncio.create_subprocess_exec(*args,stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
    try:
        stdout,stderr=await asyncio.wait_for(proc.communicate(),60)
    except BaseException:
        proc.kill();await proc.wait();raise
    if proc.returncode: raise RuntimeError(stderr.decode(errors='replace'))
    return stdout.decode().strip()

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ['host','xctestrun','out']:p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--device',required=True)
    args=p.parse_args()
    for name in ['host','xctestrun','out']:setattr(args,name,getattr(args,name).resolve())
    asyncio.run(main(args))
