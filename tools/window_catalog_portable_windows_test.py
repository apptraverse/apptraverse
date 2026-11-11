"""Load foreign USER in the real Windows consumer, reopen a child and restart."""
from __future__ import annotations

import argparse
import ctypes as ct
from ctypes import wintypes as wt
import json
from pathlib import Path
import shutil
import subprocess
import time

u = ct.WinDLL("user32", use_last_error=True)
u.GetDlgItem.argtypes = [wt.HWND, ct.c_int]
u.GetDlgItem.restype = wt.HWND
u.GetWindowThreadProcessId.argtypes = [wt.HWND, ct.POINTER(wt.DWORD)]
u.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ct.c_int]
u.IsWindowVisible.argtypes = [wt.HWND]
u.SendMessageW.argtypes = [wt.HWND, wt.UINT, ct.c_size_t, ct.c_ssize_t]
u.SendMessageW.restype = ct.c_ssize_t
u.PostMessageW.argtypes = [wt.HWND, wt.UINT, ct.c_size_t, ct.c_ssize_t]
ENUM = ct.WINFUNCTYPE(wt.BOOL, wt.HWND, ct.c_ssize_t)
u.EnumWindows.argtypes = [ENUM, ct.c_ssize_t]


def windows(pid: int) -> list[int]:
    result = []
    @ENUM
    def visit(hwnd, _):
        owner = wt.DWORD()
        u.GetWindowThreadProcessId(hwnd, ct.byref(owner))
        if owner.value == pid and u.IsWindowVisible(hwnd):
            result.append(hwnd)
        return True
    u.EnumWindows(visit, 0)
    return result


def text(hwnd) -> str:
    buffer = ct.create_unicode_buffer(4096)
    u.GetWindowTextW(hwnd, buffer, len(buffer))
    return buffer.value


def rows(main) -> list[str]:
    control = u.GetDlgItem(main, 5001)
    count = u.SendMessageW(control, 0x18B, 0, 0)  # LB_GETCOUNT
    result = []
    for index in range(max(count, 0)):
        buffer = ct.create_unicode_buffer(4096)
        u.SendMessageW(control, 0x189, index, ct.addressof(buffer))
        result.append(buffer.value)
    return result


def wait(process: subprocess.Popen, predicate):
    until = time.monotonic() + 30
    while time.monotonic() < until:
        assert process.poll() is None, f"Consumer exited {process.returncode}"
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError("Timed out waiting for native presentation")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    assert not args.output.exists(), "Use a new isolated output directory"
    args.output.mkdir(parents=True)
    shutil.copyfile(args.profile / "user.bin", args.output / "user.bin")
    manifest = dict(line.split("=", 1) for line in
                    (args.profile / "manifest.txt").read_text(encoding="utf-8").splitlines())
    expected = [manifest[f"row.{i}.localized_row"] for i in range(3)]
    assert manifest["language"] == "1" and manifest["row.1.open"] == "0"
    evidence = []
    for run in range(2):
        with (args.output / f"native-{run}.log").open("wb") as log:
            process = subprocess.Popen([str(args.exe.resolve()), "--state-dir", str(args.output.resolve())],
                                       stdout=log, stderr=log)
            try:
                main_hwnd = wait(process, lambda: next(
                    (hwnd for hwnd in windows(process.pid) if u.GetDlgItem(hwnd, 5001)), None))
                wait(process, lambda: rows(main_hwnd) == expected)
                wait(process, lambda: len(windows(process.pid)) == (3 if run == 0 else 4))
                assert text(u.GetDlgItem(main_hwnd, 5002)) == "Добавить"
                if run == 0:
                    # Imported activity is the same order[0] on every backend.
                    active_id = manifest['active_id']
                    active_title = text(main_hwnd)
                    for index in range(3):
                        if manifest[f'row.{index}.window'] == active_id:
                            active_title = expected[index]
                            break
                    wait(process, lambda: text(windows(process.pid)[0]) == active_title)
                    list_hwnd = u.GetDlgItem(main_hwnd, 5001)
                    u.SendMessageW(list_hwnd, 0x186, 1, 0)  # LB_SETCURSEL
                    u.SendMessageW(main_hwnd, 0x111, 5001 | (1 << 16), list_hwnd)
                    button = u.GetDlgItem(main_hwnd, 5003)
                    u.SendMessageW(main_hwnd, 0x111, 5003, button)
                    expected[1] = expected[1].split(" (")[0]
                    wait(process, lambda: rows(main_hwnd) == expected)
                    wait(process, lambda: len(windows(process.pid)) == 4)
                    child = next(hwnd for hwnd in windows(process.pid)
                                 if hwnd != main_hwnd and text(hwnd) == expected[1])
                    assert not u.GetDlgItem(child, 5006), "Desktop has an in-content Close button"
                    u.PostMessageW(child, 0x112, 0xF060, 0)  # SC_CLOSE: caption cross
                    wait(process, lambda: len(windows(process.pid)) == 3)
                    wait(process, lambda: rows(main_hwnd)[1] == manifest['row.1.localized_row'])
                    u.SendMessageW(main_hwnd, 0x111, 5003, button)
                    wait(process, lambda: len(windows(process.pid)) == 4)
                    wait(process, lambda: rows(main_hwnd) == expected)
                for hwnd in windows(process.pid):
                    if hwnd != main_hwnd:
                        assert not u.GetDlgItem(hwnd, 5006)
                evidence.append({"run": run, "pid": process.pid, "rows": rows(main_hwnd),
                                 "titles": [text(hwnd) for hwnd in windows(process.pid)]})
                u.PostMessageW(main_hwnd, 0x10, 0, 0)
                assert process.wait(timeout=30) == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
    (args.output / "native-report.json").write_text(
        json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("PORTABLE_WINDOWS_PASS: unchanged foreign USER, Russian rows, native child reopen, save/restart")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
