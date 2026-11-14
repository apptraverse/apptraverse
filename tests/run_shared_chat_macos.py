#!/usr/bin/env python3
"""Explicit three-process native SwiftUI/cloud acceptance; uses private profiles.

Not registered with ordinary CTest: this creates real SDK test identities.
Commands press actual SwiftUI accessibility controls; publications report visible text values.
"""
import argparse
import asyncio
import json
from pathlib import Path
import time
import uuid

async def main(args):
    args.out.mkdir(parents=True, exist_ok=False)
    processes, states, readers, logs = {}, {}, [], []
    checks = []
    started = time.monotonic()
    token = uuid.uuid4().hex[:10]

    def record(label):
        checks.append(dict(check=label, seconds=round(time.monotonic()-started, 3)))
        print('PASS '+label, flush=True)

    async def start(name):
        states.pop(name, None)
        profile = args.out/'profiles'/name
        profile.mkdir(mode=0o700, parents=True, exist_ok=True)
        proc = await asyncio.create_subprocess_exec(str(args.exe), '--host' if name == 'Host' else '--client', '--load-only', '--profile', str(profile), '--name', name,
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
        processes[name] = proc
        log = (args.out/f'{name}-{proc.pid}.log').open('w', encoding='utf-8')
        logs.append(log)
        async def read():
            async for raw in proc.stdout:
                line = raw.decode('utf-8', errors='replace')
                log.write(line); log.flush()
                if line.startswith('CHATUI ') and processes.get(name) is proc:
                    states[name] = json.loads(line[7:])
        readers.append(asyncio.create_task(read()))

    async def command(name, action, **values):
        p = processes[name]
        p.stdin.write((json.dumps(dict(action=action, **values), ensure_ascii=False)+'\n').encode())
        await p.stdin.drain()

    async def wait(label, ready, timeout=180, diagnostics=False):
        next_snapshot = 0.0
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            for name,p in processes.items():
                if p.returncode is not None: raise RuntimeError(f'{name} unexpectedly exited {p.returncode}')
            if ready(): record(label); return
            if diagnostics and time.monotonic() >= next_snapshot:
                # Explicit test-only queries keep runtime counters independent
                # of whether the serialized UI model changed.
                for name in processes: await command(name, 'snapshot')
                next_snapshot = time.monotonic() + 0.5
            await asyncio.sleep(0.1)
        raise RuntimeError(f'{label}: timeout; states={states}')

    def received(text, names=('Host','Alice','Bob')):
        return all(n in states and sum(text in m for m in states[n]['messages']) == 1
                   and states[n]['rendered'] == '\n'.join(states[n]['messages'])
                   and any(text in s for s in states[n]['visible_strings']) for n in names)

    async def stop(name, force=False):
        p = processes.pop(name)
        if force: p.kill()
        else:
            p.stdin.write(b'{"action":"quit"}\n'); await p.stdin.drain()
        code = await asyncio.wait_for(p.wait(), 45)
        if not force and code != 0: raise RuntimeError(f'{name} shutdown exit{code}')
        states.pop(name, None)

    result = 'FAIL'
    try:
        for name in ('Host','Alice','Bob'): await start(name)
        await wait('three native clients ready', lambda: all(states.get(n,{}).get('ready') and states[n]['role_ready'] for n in processes), 300)
        duplicate = await asyncio.create_subprocess_exec(str(args.exe), '--profile',
            str(args.out/'profiles'/'Host'), '--name', 'Duplicate',
            stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        try:
            assert await asyncio.wait_for(duplicate.wait(), 20) == 2
        finally:
            if duplicate.returncode is None: duplicate.kill(); await duplicate.wait()
        record('concurrent profile use rejected')
        uids = {n:states[n]['uid'] for n in processes}
        assert len(set(uids.values())) == 3
        await wait('host role automatically creates room', lambda: states['Host']['in_room'] and states['Host']['invite'])
        invite = states['Host']['invite']
        assert len(invite) == 36 and '/' not in invite
        for name in ('Alice','Bob'): await command(name,'join',text=invite)
        await wait('native Join admits all peers', lambda: all(s['in_room'] and len(s['participants']) == 3 for s in states.values()))
        for n in processes:
            assert not states[n]['join_enabled'] and states[n]['send_enabled']
            assert states[n]['messages_width'] > 100 and states[n]['messages_height'] > 100
        sent=[]
        for name in ('Host','Alice','Bob'):
            text=f'{token} {name}: Привет 🌍 你好'
            sent.append(text); await command(name,'send',text=text)
            await wait(f'{name} Unicode presented exactly once on all peers', lambda t=text: received(t))
        await wait('direct TCP path verified', lambda: all(s['tcp'] for s in states.values()), diagnostics=True)
        for name in processes:
            await command(name,'screenshot',path=str(args.out/f'{name}.png'))
        await wait('native screenshots written', lambda: all((args.out/f'{n}.png').exists() for n in processes))
        # Periodic progress reports remain in flight during idle. Only their
        # count is excluded; Events, bootstrap and coverage still require ACKs.
        await wait('initial delivery acknowledged', lambda: all(s['pending'] == s['pending_progress'] for s in states.values()), diagnostics=True)
        await stop('Bob', force=True)
        offline=f'{token} offline delivery after force restart'
        await command('Alice','send',text=offline)
        await wait('accepted message while Bob stopped', lambda: received(offline,('Host','Alice')))
        # This unacknowledged Event must remain distinguishable from progress
        # traffic until Bob returns, including across the sender's cold restart.
        await wait('offline recipient leaves a durable delivery obligation',
                   lambda: states['Alice']['pending'] > states['Alice']['pending_progress'], diagnostics=True)
        await stop('Alice', force=True)
        await start('Alice')
        await wait('sender cold restores UID and room',lambda: states.get('Alice',{}).get('ready') and states['Alice']['in_room'] and states['Alice']['uid'] == uids['Alice'])
        await start('Bob')
        await wait('recipient cold restores UID and offline delivery',lambda: states.get('Bob',{}).get('ready') and states['Bob']['uid'] == uids['Bob'] and received(offline),300)
        for text in sent: assert received(text)
        restored=f'{token} message from restored Bob'
        await command('Bob','send',text=restored)
        await wait('restored peer sends to all participants',lambda: received(restored))
        await wait('durable retry acknowledged after restart',lambda: all(s['pending'] == s['pending_progress'] for s in states.values()), diagnostics=True)
        if args.idle_seconds:
            idle_until = time.monotonic() + args.idle_seconds
            while time.monotonic() < idle_until:
                for name,p in processes.items():
                    if p.returncode is not None: raise RuntimeError(f'{name} exited during idle: {p.returncode}')
                await asyncio.sleep(0.5)
            record(f'clients survive {args.idle_seconds}s idle after recovery')
            after_idle=f'{token} delivery after idle'
            await command('Alice','send',text=after_idle)
            await wait('delivery after idle',lambda: received(after_idle))
        for name in list(processes): await stop(name)
        for name in ('Host','Alice','Bob'):
            profile=args.out/'profiles'/name
            assert (profile/'user.bin').stat().st_size > 0
            assert (profile/'transport'/'user.bin').stat().st_size > 0
            for location in (profile, profile/'transport'):
                assert not (location/'native').exists() and not (location/'root.id').exists()
        record('orderly shutdown with native USER profiles')
        result='PASS'
    finally:
        for p in processes.values():
            if p.returncode is None: p.kill()
        await asyncio.gather(*(p.wait() for p in processes.values()), return_exceptions=True)
        await asyncio.gather(*readers, return_exceptions=True)
        for log in logs: log.close()
        (args.out/'result.json').write_text(json.dumps(dict(status=result,checks=checks),indent=2)+'\n')

if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--idle-seconds',type=int,default=0)
    args=p.parse_args();args.exe=args.exe.resolve();args.out=args.out.resolve()
    asyncio.run(main(args))
