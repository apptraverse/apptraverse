"""Process-level UpdateLab acceptance; all artifacts stay in the supplied run directory."""
import argparse
import json
import lzma
import struct
import pathlib
import subprocess
import time


def read_snapshot(path):
    # The reader must share DELETE on Windows, so it cannot obstruct atomic rename.
    import os
    if os.name != "nt":
        return path.read_text(encoding="utf-8")
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(path), 0x80000000, 7, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise OSError(ctypes.get_last_error())
    try:
        buffer = ctypes.create_string_buffer(1024 * 1024)
        received = wintypes.DWORD()
        if not kernel.ReadFile(handle, buffer, len(buffer), ctypes.byref(received), None):
            raise OSError(ctypes.get_last_error())
        return buffer.raw[:received.value].decode("utf-8")
    finally:
        kernel.CloseHandle(handle)


def wsl_path(path):
    path = str(path).replace("\\", "/")
    return "/mnt/" + path[0].lower() + path[2:] if len(path) > 2 and path[1] == ":" else path


class Host:
    def __init__(self, binaries, run, store, linux=False):
        self.binaries, self.run, self.store, self.linux = str(binaries), run, store, linux
        self.count = 0

    def command(self, name, args):
        exe = self.binaries.rstrip("/\\") + "/" + name + ("" if self.linux else ".exe")
        convert = lambda arg: wsl_path(arg) if self.linux and isinstance(arg, pathlib.Path) else str(arg)
        return (["wsl", "-d", "Ubuntu", "--", exe] if self.linux else [exe]) + [convert(a) for a in args]

    def launch(self, profile, *args, expected=0, timeout=180, suppress=False):
        self.count += 1
        snapshot = self.run / ("linux" if self.linux else "windows") / f"snapshot-{self.count}.json"
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        install = profile.parent / (profile.name + "-installation")
        command = self.command("update_lab_launcher", ["--profile", profile, "--install", install,
            "--seed", pathlib.Path(self.binaries) / ("update_lab_e0" + ("" if self.linux else ".exe")),
            "--store", self.store, "--headless", "--snapshot-file", snapshot, *args])
        if suppress:
            command += ["--suppress-candidate-health", "--health-timeout-ms", "1500"]
        result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", timeout=timeout)
        (snapshot.with_suffix(".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        assert result.returncode == expected, (command, result.returncode, result.stdout[-2000:], result.stderr[-2000:])
        return json.loads(snapshot.read_text(encoding="utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--windows-bin", required=True, type=pathlib.Path)
    parser.add_argument("--linux-bin")
    parser.add_argument("--store", required=True, type=pathlib.Path)
    parser.add_argument("--run", required=True, type=pathlib.Path)
    args = parser.parse_args()
    run = args.run.resolve()
    assert not run.exists(), "Use a new run directory; existing profiles are never removed."
    run.mkdir(parents=True)
    win = Host(args.windows_bin.resolve(), run, args.store.resolve())
    profile = run / "windows-profile"
    checks = []

    def check(name, expression):
        assert expression, name
        checks.append(name)
        print("PASS", name, flush=True)

    # Decode the actual publication using a second implementation, including EOF.
    images = args.store.resolve() / "images"
    packages = sorted(images.rglob("*.ats"))
    assert packages
    for path in packages:
        blob = path.read_bytes()
        kind, _, _, size, packed = struct.unpack_from("<IIIQI", blob)
        assert packed == len(blob) - 24 and packed >= 5, path
        pb_lp, lc = divmod(blob[24], 9)
        pb, lp = divmod(pb_lp, 5)
        dictionary, = struct.unpack_from("<I", blob, 25)
        decoder = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[{
            "id": lzma.FILTER_LZMA1, "dict_size": dictionary, "lc": lc, "lp": lp, "pb": pb}])
        raw = decoder.decompress(blob[29:])
        assert decoder.eof and not decoder.unused_data and len(raw) == size, path
        if kind == 2:
            exe = "update_lab.exe" if path.parent.name == "windows-x64" else "update_lab"
            assert raw == (path.parent / exe).read_bytes(), path
    check(f"{len(packages)} image packages decode as raw LZMA1 and executables match their originals", True)

    # Offline installation uses the binary and full state of the selected release.
    for revision in range(4):
        image = args.store.resolve() / "images" / f"R{revision}"
        device = run / f"clean-R{revision}"
        shot = run / f"clean-R{revision}.json"
        command = [image / "windows-x64" / "update_lab_launcher.exe", "--profile", device, "--install", run / f"slots-R{revision}",
                   "--headless", "--snapshot-file", shot]
        subprocess.run(command, check=True, capture_output=True, timeout=30)
        fresh = json.loads(shot.read_text(encoding="utf-8"))
        check(f"offline image boots directly at R{revision}", fresh["release"] == revision and fresh["ready"]
              and fresh["executable"] == int(revision == 3) and fresh["files"] == [] and fresh["installed"] == {})

    # Republish only the new releases of the same store while this R0 remains installed.
    live = run / "distribution"
    def publish(through):
        binaries = args.windows_bin.resolve()
        command = [binaries / "update_lab_provider.exe", "publish", "--out", live, "--through", str(through)]
        for i in range(3):
            command.extend([f"--windows-x64-e{i}", binaries / f"update_lab_e{i}.exe"])
        subprocess.run(command, check=True, capture_output=True, timeout=90)

    probe = run / "discovery-profile"
    publish(1)
    win.launch(probe, "--store", live, "--language", "ru")
    win.launch(probe, "--store", live, "--language", "en")
    early = win.launch(probe, "--store", live)
    check("startup discovers published R1 with E0 and keeps inactive Russian", early["release"] == 0
          and early["offer"].startswith("R1 / E0") and early["installed"] == {"ru": 0})
    publish(3)
    later = win.launch(probe, "--store", live)
    expected = ["delta/0/1", "delta/1/2", "delta/2/3", "delta/0/1/ru", "delta/1/3/ru", "executable/windows-x64/1"]
    paths = ["R1/deltas/from-R0/state.ats", "R2/deltas/from-R1/state.ats", "R3/deltas/from-R2/state.ats",
             "R1/deltas/from-R0/downloads/ru.ats", "R3/deltas/from-R2/downloads/ru.ats", "R3/windows-x64/update.ats"]
    amount = sum((live / "images" / name).stat().st_size for name in paths)
    check("new publication announces EXE and exact remaining bytes", later["release"] == 0
          and later["offer"] == f"R3 / E1 · EXE · {amount} B")
    updated = win.launch(probe, "--store", live, "--latest")
    check("latest traverses every base edge and only changed installed resource edges",
          updated["release"] == 3 and updated["requested"] == expected and updated["installed"] == {"ru": 3})
    check("inactive locale is ready without a new download", win.launch(probe, "--language", "ru", "--store", live)["requested"] == [])
    check("current installation reports zero bytes", win.launch(probe, "--store", live)["offer"] == "R3 / E1 · 0 B")

    # Stop at the real protocol boundary between staging and the launcher's trial.
    trial_profile = run / "offline-trial"
    win.launch(trial_profile, "--note", "offline trial", "--language", "ru")
    journal = trial_profile.parent / (trial_profile.name + "-installation")
    stage = win.command("update_lab_e0", ["--headless", "--profile", trial_profile, "--install", journal,
                                         "--store", args.store.resolve(), "--latest"])
    assert subprocess.run(stage, capture_output=True, timeout=90).returncode == 42
    transfer_file = trial_profile / "local" / "transfer" / "user.bin"
    original_user = (trial_profile / "user.bin").read_bytes()
    staged_intent = transfer_file.read_bytes()
    candidate = win.command("update_lab_e1", ["--headless", "--no-action", "--profile", trial_profile, "--install", journal])
    def first_publication():
        result = subprocess.run(candidate, capture_output=True, check=True, encoding="utf-8", timeout=30)
        return json.loads(next(line for line in result.stdout.splitlines() if line.startswith("{")))
    first = first_publication()
    check("offline candidate activates before its first UI publication", first["release"] == 3 and first["language"] == "ru"
          and first["ready"] and first["installed"] == {"ru": 3})
    # Recreate a power cut after installed.bin replacement and before USER replacement.
    (trial_profile / "user.bin").write_bytes(original_user)
    transfer_file.write_bytes(staged_intent)
    recovered_trial = first_publication()
    check("interrupted activation replays before first publication", recovered_trial["ready"]
          and recovered_trial["release"] == 3 and recovered_trial["language"] == "ru"
          and recovered_trial["note"] == "offline trial" and recovered_trial["clone_ids"] == first["clone_ids"])

    state = win.launch(profile, "--device", "windows-device", "--note", "portable user note", "--rename-first", "custom A", "--add-clone", "--rate", "123456")
    check("initial S0 and portable edits", state["release"] == 0 and state["clones"] == ["custom A", "B", "3"])
    initial_ids = state["clone_ids"]
    state = win.launch(profile)
    check("cold USER and LOCAL restore", state["note"] == "portable user note" and state["rate"] == 123456)
    state = win.launch(profile, "--release", "1", "--language", "ru", "--rate", "0")
    check("translation replacement", state["language"] == "ru" and "Обновлённый" in state["greeting"])
    check("adjacent native state delta used", "delta" in state["message"])
    check("update preserves clones and note", state["clones"][0] == "custom A" and state["note"] == "portable user note")

    snapshot = run / "partial.json"
    process = subprocess.Popen(win.command("update_lab_e0", ["--profile", profile, "--store", args.store.resolve(),
        "--headless", "--release", "2", "--language", "de", "--rate", "65536", "--snapshot-file", snapshot]),
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 30
        partial = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise AssertionError(process.stderr.read().decode(errors="replace"))
            if snapshot.exists():
                try:
                    value = json.loads(read_snapshot(snapshot))
                    if value["phase"] == "progress" and any(0 < f["received"] < f["size"] for f in value["files"]):
                        partial = value
                        break
                except (OSError, json.JSONDecodeError):
                    pass
            time.sleep(0.02)
        check("partial language keeps previous locale atomically", partial is not None and partial["language"] == "ru" and partial["release"] == 1)
    finally:
        process.kill()
        process.wait(timeout=10)
    state = win.launch(profile)
    check("DownloadNode resumes after process kill", state["language"] == "de" and state["release"] == 2 and "resumed=0;" not in state["message"])
    state = win.launch(profile, "--release", "3")
    check("real E0 to E1 executable transition", state["executable"] == 1 and state["release"] == 3)
    check("S1 reaches existing prefab clones", len(state["clones"]) == 3 and all("S1 / prefab R3" in c for c in state["clones"]))
    check("S1 preserves S0 override", state["clones"][0].startswith("custom A"))
    check("S1 preserves existing clone ObjIds", state["clone_ids"] == initial_ids)
    state = win.launch(profile, "--badge-first", "custom S1", "--add-clone")
    check("new clone inherits updated prefab", len(state["clones"]) == 4 and "S1 / prefab R3" in state["clones"][-1])
    state = win.launch(profile, "--release", "0")
    check("navigate back to old executable and supply", state["release"] == 0 and state["executable"] == 0)
    state = win.launch(profile, "--note", "edited by old executable")
    state = win.launch(profile, "--release", "3", "--language", "de")
    check("unknown S1 layer survives old executable save", "custom S1" in state["clones"][0] and state["note"] == "edited by old executable")
    before = state.copy()
    state = win.launch(profile, "--broken", expected=3)
    check("bad executable rolls back and relaunches previous", state["executable"] == 1 and state["phase"] == "rolled-back")
    check("rollback restores USER and LOCAL", state["note"] == before["note"] and state["clones"] == before["clones"] and state["device"] == "windows-device")
    state = win.launch(profile, "--release", "2", expected=3, suppress=True)
    check("health timeout rolls back executable and state", state["release"] == 3 and state["executable"] == 1 and state["phase"] == "rolled-back")
    exported = run / "portable-user.ats"
    state = win.launch(profile, "--export", exported)
    check("portable state is one compressed file", exported.exists() and exported.stat().st_size > 0)

    error_profile = run / "download-error-profile"
    failed = win.launch(error_profile, "--release", "2", "--language", "de", "--store", run / "absent-store", expected=2)
    check("download error Event preserves active language and release", failed["phase"] == "error" and failed["language"] == "en" and failed["release"] == 0 and "unavailable" in failed["message"])
    recovered = win.launch(error_profile)
    check("persisted failed download retries after restart", recovered["phase"] == "activated" and recovered["language"] == "de" and recovered["release"] == 2)

    interrupted_profile = run / "interrupted-trial-profile"
    win.launch(interrupted_profile, "--device", "trial-device", "--note", "before trial")
    interrupted_install = interrupted_profile.parent / (interrupted_profile.name + "-installation")
    interrupted_snapshot = run / "interrupted-trial.json"
    trial_command = win.command("update_lab_launcher", ["--profile", interrupted_profile,
        "--install", interrupted_install, "--store", args.store.resolve(), "--headless",
        "--release", "3", "--suppress-candidate-health", "--health-timeout-ms", "15000",
        "--hold-ms", "5000", "--snapshot-file", interrupted_snapshot])
    with (run / "interrupted-trial.log").open("w", encoding="utf-8") as log:
        trial = subprocess.Popen(trial_command, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 60
            observed = None
            while time.monotonic() < deadline:
                if interrupted_snapshot.exists():
                    try:
                        observed = json.loads(read_snapshot(interrupted_snapshot))
                        if observed["executable"] == 1 and observed["phase"] == "activated":
                            break
                    except (OSError, json.JSONDecodeError):
                        pass
                assert trial.poll() is None, "trial exited before interruption"
                time.sleep(0.02)
            check("trial candidate reached UI publication before interrupted launcher", observed and observed["executable"] == 1 and observed["phase"] == "activated")
        finally:
            trial.kill()
            trial.wait(timeout=10)
    guard = subprocess.run(win.command("update_lab_launcher", ["--profile", interrupted_profile,
        "--install", interrupted_install, "--headless", "--store", args.store.resolve()]),
        capture_output=True, text=True, timeout=10)
    check("orphan candidate profile lock prevents concurrent rollback", guard.returncode == 1 and "application is still running" in guard.stderr)
    # The deliberately orphaned child exits on its own bounded --hold-ms deadline.
    time.sleep(5.5)
    restored = win.launch(interrupted_profile, expected=3)
    check("next boot rolls back interrupted trial and restores native state", restored["release"] == 0 and restored["executable"] == 0 and restored["phase"] == "rolled-back" and restored["note"] == "before trial" and restored["device"] == "trial-device")

    if args.linux_bin:
        linux = Host(args.linux_bin, run, args.store.resolve(), linux=True)
        linux_profile = run / "linux-profile"
        linux_image = args.store.resolve() / "images" / "R3"
        linux_fresh = run / "linux-image.json"
        direct = linux.command("update_lab_launcher", ["--image", linux_image, "--profile", run / "linux-image-profile",
            "--install", run / "linux-image-install", "--headless", "--snapshot-file", linux_fresh])
        subprocess.run(direct, check=True, capture_output=True, timeout=30)
        clean = json.loads(linux_fresh.read_text(encoding="utf-8"))
        check("offline Linux image installs directly at R3/E1", clean["release"] == 3 and clean["executable"] == 1
              and clean["ready"] and not clean["files"])
        state = linux.launch(linux_profile, "--device", "linux-device", "--rate", "777777")
        check("independent Linux LOCAL initialized", state["device"] == "linux-device")
        state = linux.launch(linux_profile, "--import", exported, timeout=240)
        check("Windows to Linux USER migration with native executable", state["release"] == 3 and state["executable"] == 1 and state["language"] == "de")
        check("destination LOCAL survives migration", state["device"] == "linux-device" and state["rate"] == 777777)
        check("migrated S1 and identity overrides", state["clones"] == before["clones"])
        check("Windows to Linux clone ObjIds preserved", state["clone_ids"] == before["clone_ids"])
        returned = run / "returned-user.ats"
        state = linux.launch(linux_profile, "--note", "edited on Linux", "--export", returned)
        state = win.launch(profile, "--import", returned)
        check("Linux to Windows USER migration", state["note"] == "edited on Linux" and state["language"] == "de")
        check("Linux to Windows clone ObjIds preserved", state["clone_ids"] == before["clone_ids"])
        check("Windows LOCAL survives return migration", state["device"] == "windows-device" and state["rate"] == 0)
    (run / "results.json").write_text(json.dumps({"passed": len(checks), "checks": checks}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"PASS {len(checks)} process checks", flush=True)


if __name__ == "__main__":
    main()
