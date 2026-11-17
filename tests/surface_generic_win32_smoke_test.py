"""Exercise the migrated Surface executables through real native controls."""
import ctypes as c
from ctypes import wintypes as w
import pathlib
import subprocess
import sys
import tempfile
import time

u = c.WinDLL('user32', use_last_error=True)
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
u.IsWindow.argtypes = [w.HWND]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [callback, w.LPARAM]

def text(hwnd):
    value = c.create_unicode_buffer(512)
    u.GetWindowTextW(hwnd, value, len(value))
    return value.value

def windows(process):
    result = []
    @callback
    def found(hwnd, _):
        pid = w.DWORD()
        u.GetWindowThreadProcessId(hwnd, c.byref(pid))
        title = text(hwnd)
        if pid.value == process.pid and (title.startswith('Surface ') or title.startswith('Поверхность ')):
            result.append((hwnd, title))
        return True
    u.EnumWindows(found, 0)
    return result

def wait(test, message):
    end = time.monotonic() + 20
    while time.monotonic() < end:
        result = test()
        if result:
            return result
        time.sleep(.025)
    raise AssertionError(message)

def named(process, title):
    return next((hwnd for hwnd, name in windows(process) if name == title), None)

def rect(hwnd):
    value = w.RECT()
    obtained = u.GetWindowRect(hwnd, c.byref(value))
    assert obtained
    return value.left, value.top, value.right - value.left, value.bottom - value.top

def click(hwnd, control):
    button = wait(lambda: u.GetDlgItem(hwnd, control), f"standard control {control} mounted")
    assert button, f'missing standard control {control}'
    assert min(rect(button)[2:]) > 0
    u.SendMessageW(button, 0xF5, 0, 0)

def near(a, b):
    return all(abs(x-y) <= 3 for x, y in zip(a,b))

def run(fresh, cold, v1):
    with tempfile.TemporaryDirectory(prefix='apptraverse_surface_generic_') as directory:
        base = pathlib.Path(directory)
        p = subprocess.Popen([fresh, '--state-dir', str(base / 'normal')])
        try:
            first = wait(lambda: named(p, 'Surface 1'), 'fresh Surface 1')
            click(first, 1001)
            second = wait(lambda: named(p, 'Surface 2'), 'Add Surface 2')
            click(second, 1001)
            third = wait(lambda: named(p, 'Surface 3'), 'Add Surface 3')
            click(second, 1002)
            wait(lambda: not u.IsWindow(second), 'Close removes Surface 2')
            assert named(p, 'Surface 1') == first and named(p, 'Surface 3') == third
            click(third, 1001)
            fourth = wait(lambda: named(p, 'Surface 4'), 'stable next Surface number')
            click(first, 1003)
            wait(lambda: named(p, 'Поверхность 4') == fourth, 'language publication')
            assert text(u.GetDlgItem(first, 1001)) == 'Добавить'
            assert text(u.GetDlgItem(third, 1002)) == 'Закрыть это окно'
            saved = {}
            for hwnd, title in windows(p):
                number = int(title.split()[-1])
                u.SendMessageW(hwnd, 0x231, 0, 0)
                moved = u.SetWindowPos(hwnd, None, 80 + number * 40, 90 + number * 30,
                                       480 + number * 10, 320 + number * 10, 0x14)
                assert moved
                u.SendMessageW(hwnd, 0x232, 0, 0)
                saved[title] = rect(hwnd)
            # Allow queued native observations to publish before shutdown.
            time.sleep(.2)
            for hwnd in [first, fourth, third]:
                ordered = u.SetWindowPos(hwnd, None, 0, 0, 0, 0, 0x13)
                assert ordered
            expected_order = [title for _, title in windows(p)]
            u.PostMessageW(third, 0x10, 0, 0)
            status = p.wait(timeout=20)
            assert status == 0
            profiles = list((base / 'normal').glob('debug-run-*/user'))
            assert len(profiles) == 1
            profile = profiles[0]
            assert (profile / 'user.bin').is_file() and (profile / 'local/user.bin').is_file()
            p = subprocess.Popen([cold, '--state-dir', str(profile)])
            wait(lambda: len(windows(p)) == 3, 'cold Surface count')
            wait(lambda: {name for _, name in windows(p)} == set(saved), 'RU cold titles/topology')
            for hwnd, title in windows(p):
                wait(lambda hwnd=hwnd, title=title: near(rect(hwnd), saved[title]), 'LOCAL cold placement')
            wait(lambda: [title for _, title in windows(p)] == expected_order, 'full cold z-order')
            assert not named(p, 'Поверхность 2')
            u.PostMessageW(windows(p)[0][0], 0x10, 0, 0)
            status = p.wait(timeout=20)
            assert status == 0
            p = subprocess.Popen([v1, '--state-dir', str(base / 'v1')])
            first = wait(lambda: named(p, 'Surface 1'), 'v1 initial Surface')
            click(first, 1001)
            second = wait(lambda: named(p, 'Surface 2 [s2]'), 'v1 exposed suffix default')
            u.PostMessageW(second, 0x10, 0, 0)
            status = p.wait(timeout=20)
            assert status == 0
            v1_profiles = list((base / 'v1').glob('debug-run-*/user'))
            assert len(v1_profiles) == 1
            p = subprocess.Popen([v1, '--load-only', '--state-dir', str(v1_profiles[0])])
            second = wait(lambda: named(p, 'Surface 2 [s2]'), 'v1 matching-supply cold suffix')
            click(second, 1001)
            third = wait(lambda: named(p, 'Surface 3 [s3]'), 'v1 cold newly added suffix')
            u.PostMessageW(third, 0x10, 0, 0)
            status = p.wait(timeout=20)
            assert status == 0
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()
    print('PASS: Surface native controls, topology/HWND identity, language, LOCAL placement, full z-order, native X and v1 defaults')

if __name__ == '__main__':
    run(*sys.argv[1:])
