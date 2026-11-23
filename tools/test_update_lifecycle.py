"""Exercise the Windows lifecycle example through Win32 controls and a provider."""
import argparse
import ctypes
from ctypes import wintypes
import json
import pathlib
import subprocess
import time

from test_update_lab import read_snapshot


USER32 = ctypes.WinDLL("user32", use_last_error=True)
KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
ENUM_WINDOWS = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
USER32.EnumWindows.argtypes = [ENUM_WINDOWS, wintypes.LPARAM]
USER32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
USER32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
USER32.GetDlgItem.restype = wintypes.HWND
USER32.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
USER32.FindWindowExW.restype = wintypes.HWND
USER32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
USER32.SendMessageW.restype = wintypes.LPARAM
USER32.PostMessageW.argtypes = USER32.SendMessageW.argtypes
USER32.IsWindowEnabled.argtypes = [wintypes.HWND]
USER32.IsWindowVisible.argtypes = [wintypes.HWND]
USER32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
KERNEL32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
KERNEL32.OpenProcess.restype = wintypes.HANDLE
KERNEL32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                ctypes.POINTER(wintypes.DWORD)]
KERNEL32.CloseHandle.argtypes = [wintypes.HANDLE]

LANGUAGE_LIST, RELEASE_LIST, DATA_BUTTON, EXE_BUTTON = 101, 102, 111, 112
WM_COMMAND, WM_CLOSE, BM_CLICK = 0x0111, 0x0010, 0x00F5
LB_GETCOUNT, LB_SETCURSEL, PBM_GETPOS = 0x018B, 0x0186, 0x0408


def own_window(installation):
    found = None
    window_class = ctypes.create_unicode_buffer(64)
    exe = ctypes.create_unicode_buffer(32768)

    @ENUM_WINDOWS
    def visit(hwnd, _):
        nonlocal found
        if not USER32.IsWindowVisible(hwnd):
            return True
        USER32.GetClassNameW(hwnd, window_class, len(window_class))
        if window_class.value != "AppTraverseUpdateLab":
            return True
        pid = wintypes.DWORD()
        USER32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        handle = KERNEL32.OpenProcess(0x1000, False, pid.value)
        if handle:
            try:
                size = wintypes.DWORD(len(exe))
                if KERNEL32.QueryFullProcessImageNameW(handle, 0, exe, ctypes.byref(size)):
                    path = pathlib.Path(exe.value)
                    if path.parent.parent == installation / "slots":
                        found = hwnd
                        return False
            finally:
                KERNEL32.CloseHandle(handle)
        return True

    USER32.EnumWindows(visit, 0)
    return found


def choose(hwnd, control_id, item):
    control = USER32.GetDlgItem(hwnd, control_id)
    assert control and USER32.IsWindowEnabled(control), control_id
    assert USER32.SendMessageW(control, LB_SETCURSEL, item, 0) != -1
    USER32.SendMessageW(hwnd, WM_COMMAND, (1 << 16) | control_id, control)


def click(hwnd, control_id):
    control = USER32.GetDlgItem(hwnd, control_id)
    assert control and USER32.IsWindowEnabled(control), control_id
    USER32.SendMessageW(control, BM_CLICK, 0, 0)


class Device:
    def __init__(self, args, app):
        self.args = args
        self.app = app
        self.root = args.run / app
        self.root.mkdir()
        self.installation = self.root / "installation"
        self.snapshot = self.root / "snapshot.json"
        self.process = None

    def command(self):
        source = ["--provider", self.args.provider] if self.args.provider else ["--store", self.args.store]
        image = self.args.seeds / "apps" / f"app-{self.app}" / "images" / "R0"
        return [
            self.args.bin / "update_lab_launcher.exe", "--app", self.app,
            "--profile", self.root / "profile", "--install", self.installation,
            "--image", image, "--snapshot-file", self.snapshot, *source,
        ]

    def preview(self, suffix="preview"):
        self.snapshot = self.root / f"{suffix}.json"
        with (self.root / f"{suffix}.log").open("w", encoding="utf-8") as log:
            subprocess.run([*self.command(), "--headless"], check=True, stdout=log, stderr=log, timeout=180)
        return json.loads(read_snapshot(self.snapshot))

    def start(self, suffix):
        self.snapshot = self.root / f"{suffix}.json"
        with (self.root / f"{suffix}.log").open("w", encoding="utf-8") as log:
            self.process = subprocess.Popen([*self.command(), "--auto-close-ms", "1200000"],
                                            stdout=log, stderr=log)

    def wait(self, label, condition, timeout=150):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            assert self.process.poll() is None, (label, self.process.returncode, last)
            if self.snapshot.is_file():
                try:
                    last = json.loads(read_snapshot(self.snapshot))
                except (OSError, json.JSONDecodeError):
                    continue
                assert last["phase"] != "error", (label, last)
                if condition(last):
                    hwnd = own_window(self.installation)
                    if hwnd:
                        return last, hwnd
            time.sleep(0.1)
        raise AssertionError((label, last))

    def close(self):
        if not self.process or self.process.poll() is not None:
            return
        hwnd = own_window(self.installation)
        if hwnd:
            USER32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        try:
            assert self.process.wait(timeout=15) == 0
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(self.process.pid)],
                           capture_output=True, check=False)
            self.process.wait(timeout=10)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", required=True, type=pathlib.Path)
    parser.add_argument("--seeds", required=True, type=pathlib.Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--store", type=pathlib.Path)
    source.add_argument("--provider")
    parser.add_argument("--run", required=True, type=pathlib.Path)
    parser.add_argument("--app", default="lifecycle-demo")
    parser.add_argument("--other-app", default="lifecycle-demo-alt")
    parser.add_argument("--offers-only", type=int, metavar="LATEST")
    args = parser.parse_args()
    args.bin, args.seeds, args.run = args.bin.resolve(), args.seeds.resolve(), args.run.resolve()
    if args.store:
        args.store = args.store.resolve()
    args.run.mkdir(parents=True)

    device, other = Device(args, args.app), Device(args, args.other_app)
    preview = device.preview()
    alternate = other.preview()
    expected = 3 if args.offers_only is None else args.offers_only
    assert preview["ready"] and preview["language"] == "en" and preview["update"]["release"] == expected, preview
    assert alternate["update"]["release"] == 1 and alternate["update"]["data_only"], alternate
    print(f"PASS one provider serves independently versioned {args.app} R{expected} and {args.other_app} R1", flush=True)
    if args.offers_only is not None:
        return

    try:
        device.start("native-before")
        before, hwnd = device.wait("two update options for the same R3", lambda state:
                                  state["ready"] and state["update"]["data_only"] and state["update"]["replace_executable"])
        assert before["update"]["data_bytes"] < before["update"]["download_bytes"], before
        assert USER32.SendMessageW(USER32.GetDlgItem(hwnd, LANGUAGE_LIST), LB_GETCOUNT, 0, 0) == 2
        started = time.monotonic()
        choose(hwnd, LANGUAGE_LIST, 1)
        partial, hwnd = device.wait("partial Russian with the previous English greeting", lambda state:
            state["phase"] == "progress" and state["language"] == "en" and
            any(0 < f["received"] < f["size"] and f["id"].startswith("language/") for f in state["files"]))
        assert partial["greeting"] == before["greeting"] and time.monotonic() - started >= 0.95
        bar = USER32.FindWindowExW(hwnd, None, "msctls_progress32", None)
        assert bar and 0 < USER32.SendMessageW(bar, PBM_GETPOS, 0, 0) < 1000
        print("PASS actual Windows progress bar advances while English stays active", flush=True)

        device.close()
        device.start("native-resumed")
        russian, hwnd = device.wait("restart resumes and activates Russian", lambda state:
            state["phase"] == "activated" and state["language"] == "ru" and state["update"]["data_only"])
        assert "resumed=0;" not in russian["message"] and russian["greeting"] != before["greeting"], russian
        print("PASS closing and reopening resumes the transfer, then switches to Russian", flush=True)
        click(hwnd, DATA_BUTTON)
        data, hwnd = device.wait("data-only R3 on E0", lambda state:
            state["release"] == 3 and state["executable"] == 0 and state["ready"] and state["update"]["replace_executable"])
        assert data["requested"] and all(not file.startswith("executable/") for file in data["requested"]), data
        assert data["greeting"] != russian["greeting"] and not data["update"]["data_only"], data
        print("PASS data-only button updates R0 to R3 on E0 and offers the E1 upgrade separately", flush=True)

        click(hwnd, EXE_BUTTON)
        full, hwnd = device.wait("update restarts with E1", lambda state:
            state["release"] == 3 and state["executable"] == 1 and state["ready"] and state["update"]["release"] == 3, 600)
        assert full["requested"] == ["executable/windows-x64/1"] and full["greeting"] == data["greeting"], full
        assert full["clone_ids"] == data["clone_ids"], full
        print("PASS EXE button downloads E1 and restarts with the same R3 data and USER", flush=True)

        choose(hwnd, RELEASE_LIST, 1)
        back, hwnd = device.wait("version list returns to R1/E0", lambda state:
            state["release"] == 1 and state["executable"] == 0 and state["ready"] and state["update"]["release"] == 3, 600)
        assert back["language"] == "ru" and back["greeting"] != full["greeting"], back
        assert back["clone_ids"] == full["clone_ids"] and back["update"]["replace_executable"], back
        print("PASS version list switches back to R1/E0 and the newer version is offered again", flush=True)

        click(hwnd, EXE_BUTTON)
        combined, _ = device.wait("single action applies data and EXE", lambda state:
            state["release"] == 3 and state["executable"] == 1 and state["ready"] and state["update"]["release"] == 3, 600)
        assert "delta/1/2" in combined["requested"] and "delta/2/3" in combined["requested"], combined
        assert "executable/windows-x64/1" in combined["requested"], combined
        print("PASS from R1, one update action applies the bin chain and E1 together", flush=True)

        assert other.preview("independent")["update"]["release"] == 1
        (args.run / "results.json").write_text(json.dumps({
            "before": before, "partial": partial, "russian": russian, "data": data, "full": full, "back": back,
            "combined": combined, "alternate": alternate}, ensure_ascii=False, indent=2), encoding="utf-8")
    finally:
        device.close()


if __name__ == "__main__":
    main()
