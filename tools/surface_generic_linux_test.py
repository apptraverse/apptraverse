"""Exercise actual GTK Surface executables through AT-SPI native actions/X11."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time
import gi
gi.require_version('Atspi', '2.0')
gi.require_version('Gdk', '3.0')
gi.require_version('GdkX11', '3.0')
from gi.repository import Atspi, GLib, Gdk, GdkX11

def wait(check, description):
    end = time.monotonic() + 25
    while time.monotonic() < end:
        while GLib.MainContext.default().pending():
            GLib.MainContext.default().iteration(False)
        value = check()
        if value:
            return value
        time.sleep(0.04)
    raise AssertionError(description)

def descendants(value):
    yield value
    for i in range(value.get_child_count()):
        child = value.get_child_at_index(i)
        if child:
            yield from descendants(child)

def native_window(process, title):
    result = subprocess.run(['xdotool', 'search', '--all', '--onlyvisible', '--pid', str(process.pid), '--name', '^' + title + '$'], capture_output=True, text=True)
    return result.stdout.strip().splitlines()[0] if result.returncode == 0 and result.stdout.strip() else None

def click(process, title, label):

    def find():
        for app in descendants(Atspi.get_desktop(0)):
            if app.get_role() == Atspi.Role.FRAME and app.get_process_id() == process.pid and (app.get_name() == title):
                for node in descendants(app):
                    if node.get_role() == Atspi.Role.PUSH_BUTTON and node.get_name() == label:
                        return node
        return None
    hwnd = wait(lambda: native_window(process, title), 'GTK input window')
    subprocess.run(['xdotool', 'windowactivate', '--sync', hwnd], check=True)
    button = wait(find, f'GTK {title}/{label}')
    bounds = button.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
    assert bounds.width > 0 and bounds.height > 0
    time.sleep(0.1)
    subprocess.run(['xdotool', 'mousemove', str(bounds.x + bounds.width // 2), str(bounds.y + bounds.height // 2), 'click', '1'], check=True)

def stack():
    Gdk.init([])
    return [str(window.get_xid()) for window in reversed(Gdk.Screen.get_default().get_window_stack() or [])]

def frame(hwnd):
    values = subprocess.check_output(['xdotool', 'getwindowgeometry', '--shell', hwnd], text=True)
    data = dict((line.split('=', 1) for line in values.splitlines() if '=' in line))
    return tuple((int(data[key]) for key in ['X', 'Y', 'WIDTH', 'HEIGHT']))

def close_native(hwnd):
    subprocess.run(['xdotool', 'windowactivate', '--sync', hwnd], check=True)
    subprocess.run(['xdotool', 'key', '--clearmodifiers', 'alt+F4'], check=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('fresh')
    parser.add_argument('cold')
    args = parser.parse_args()
    wm = subprocess.Popen(['openbox'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    process = None

    def wm_ready():
        result = subprocess.run(['xdotool', 'getactivewindow'], capture_output=True, text=True)
        return 'not to support _NET_ACTIVE_WINDOW' not in result.stderr
    wait(wm_ready, 'X11 window manager ready')
    time.sleep(1)
    try:
        with tempfile.TemporaryDirectory(prefix='surface-generic-linux-') as temporary:
            directory = Path(temporary)
            process = subprocess.Popen([args.fresh, '--state-dir', str(directory)])
            first = wait(lambda: native_window(process, 'Surface 1'), 'fresh GTK window')
            click(process, 'Surface 1', 'Add')
            second = wait(lambda: native_window(process, 'Surface 2'), 'GTK Add Surface 2')
            click(process, 'Surface 2', 'Add')
            third = wait(lambda: native_window(process, 'Surface 3'), 'GTK Add Surface 3')
            click(process, 'Surface 2', 'Close this window')
            wait(lambda: not native_window(process, 'Surface 2'), 'GTK remove Surface 2')
            assert native_window(process, 'Surface 1') == first and native_window(process, 'Surface 3') == third
            click(process, 'Surface 3', 'RU')
            wait(lambda: native_window(process, 'Поверхность 3') == third, 'GTK RU title')
            subprocess.run(['xdotool', 'windowmove', first, '100', '110', 'windowsize', first, '560', '340'], check=True)
            subprocess.run(['xdotool', 'windowmove', third, '210', '190', 'windowsize', third, '420', '510'], check=True)
            time.sleep(0.4)
            saved = {1: frame(first), 3: frame(third)}
            original_ids = {first: 1, third: 3}
            saved_order = [original_ids[xid] for xid in stack() if xid in original_ids]
            assert sorted(saved_order) == [1, 3]
            close_native(third)
            status = process.wait(timeout=25)
            assert status == 0
            profiles = list(directory.glob('debug-run-*/user'))
            assert len(profiles) == 1
            profile = profiles[0]
            assert (profile / 'user.bin').is_file() and (profile / 'local/user.bin').is_file()
            process = subprocess.Popen([args.cold, '--state-dir', str(profile)])
            restored_first = wait(lambda: native_window(process, 'Поверхность 1'), 'cold GTK RU 1')
            restored_third = wait(lambda: native_window(process, 'Поверхность 3'), 'cold GTK RU 3')
            assert not native_window(process, 'Поверхность 2')
            for number, hwnd in [(1, restored_first), (3, restored_third)]:
                wait(lambda: all((abs(a - b) <= 3 for a, b in zip(frame(hwnd), saved[number]))), 'cold GTK LOCAL placement')
            restored_ids = {restored_first: 1, restored_third: 3}
            wait(lambda: [restored_ids[xid] for xid in stack() if xid in restored_ids] == saved_order, 'cold GTK full desktop z-order')
            click(process, 'Поверхность 3', 'Добавить')
            wait(lambda: native_window(process, 'Поверхность 4'), 'cold GTK next number')
            close_native(restored_first)
            status = process.wait(timeout=25)
            assert status == 0
            print('PASS GTK Surface native Add/Close/RU, stable surviving XIDs, resize/orientation, native X, USER+LOCAL cold topology/placement and next number')
    finally:
        if process and process.poll() is None:
            process.kill()
            process.wait()
        wm.terminate()
        wm.wait(timeout=10)
if __name__ == '__main__':
    main()
