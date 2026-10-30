#!/usr/bin/env python3
"""UI E2E for the macOS Window Catalog demo through System Events.

Clicks SwiftUI controls and the native window close button. Waits on the
published accessibility projection (catalog.revision / catalog.save.status).
"""

import os
import subprocess
import sys
import time
from pathlib import Path

PROC = "macos_window_catalog_demo"
ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "build/macos-x86_64-debug/examples/surfaces_demo/macos" / (
    "macos_window_catalog_demo.app/Contents/MacOS/macos_window_catalog_demo"
)
STATE = Path(os.environ.get("WC_STATE_DIR", "/tmp/wc-e2e-macos-state"))
SHOTS = Path(os.environ.get(
    "WC_SHOTS",
    str(ROOT / "docs/experiments/apple-swiftui/screenshots/macos"),
))
LOG = Path("/tmp/wc-e2e-macos.log")

SCRIPT = r"""
using terms from application "System Events"
on identOf(elem)
  try
    return (value of attribute "AXIdentifier" of elem) as text
  on error
    return ""
  end try
end identOf

on walk(elem, depth, acc)
  if depth > 14 then return acc
  set ident to my identOf(elem)
  set nm to ""
  try
    set nm to name of elem as text
  end try
  set val to ""
  try
    set val to value of elem as text
  end try
  set en to ""
  try
    set en to enabled of elem as text
  end try
  set rl to ""
  try
    set rl to role of elem as text
  end try
  set sr to ""
  try
    set sr to subrole of elem as text
  end try
  set acc to acc & rl & tab & ident & tab & nm & tab & val & tab & en & tab & sr & linefeed
  try
    repeat with c in UI elements of elem
      set acc to my walk(c, depth + 1, acc)
    end repeat
  end try
  return acc
end walk

on dumpAll()
  tell application "System Events"
    if not (exists process "PROC") then return "NO_PROCESS"
    tell process "PROC"
      set acc to ""
      repeat with w in windows
        set wt to ""
        try
          set wt to name of w as text
        end try
        set acc to acc & "WINDOW" & tab & wt & linefeed
        set acc to my walk(w, 0, acc)
      end repeat
      return acc
    end tell
  end tell
end dumpAll

on findId(elem, target, depth)
  if depth > 14 then return missing value
  if my identOf(elem) is target then return elem
  try
    repeat with c in UI elements of elem
      set found to my findId(c, target, depth + 1)
      if found is not missing value then return found
    end repeat
  end try
  return missing value
end findId

on clickId(target)
  tell application "System Events"
    tell process "PROC"
      set frontmost to true
      repeat with w in windows
        set found to my findId(w, target, 0)
        if found is not missing value then
          click found
          return "CLICKED"
        end if
      end repeat
    end tell
  end tell
  return "MISSING"
end clickId

on closeWindowId(target)
  tell application "System Events"
    tell process "PROC"
      set frontmost to true
      repeat with w in windows
        if my identOf(w) is target then
          click (first button of w whose subrole is "AXCloseButton")
          return "CLOSED"
        end if
      end repeat
    end tell
  end tell
  return "MISSING"
end closeWindowId

on moveWindowId(target, px, py, sw, sh)
  tell application "System Events"
    tell process "PROC"
      set frontmost to true
      repeat with w in windows
        if my identOf(w) is target then
          set size of w to {sw, sh}
          set position of w to {px, py}
          return "MOVED"
        end if
      end repeat
    end tell
  end tell
  return "MISSING"
end moveWindowId

on run argv
  if (count of argv) is 0 then return dumpAll()
  set verb to item 1 of argv
  if verb is "dump" then return dumpAll()
  if verb is "click" then return clickId(item 2 of argv)
  if verb is "close" then return closeWindowId(item 2 of argv)
  if verb is "move" then
    return moveWindowId(item 2 of argv, (item 3 of argv) as integer, (item 4 of argv) as integer, (item 5 of argv) as integer, (item 6 of argv) as integer)
  end if
  return "UNKNOWN"
end run
end using terms from
"""

COMPILED = Path("/tmp/wc-e2e-macos.scpt")


def ensure_script():
    source = Path("/tmp/wc-e2e-macos.applescript")
    program = SCRIPT.replace("PROC", PROC)
    if COMPILED.exists() and source.exists() and source.read_text() == program:
        return
    source.write_text(program)
    subprocess.run(
        ["osacompile", "-o", str(COMPILED), str(source)],
        check=True,
        capture_output=True,
        text=True,
    )


def osa(command, *args):
    ensure_script()
    result = subprocess.run(
        ["osascript", str(COMPILED), command, *[str(arg) for arg in args]],
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result.stdout


def parse(dump):
    windows = []
    current = None
    for line in dump.splitlines():
        parts = line.split("\t")
        if not parts or not parts[0]:
            continue
        if parts[0] == "WINDOW":
            current = {"title": parts[1] if len(parts) > 1 else "", "elems": []}
            windows.append(current)
            continue
        if current is None:
            continue
        while len(parts) < 6:
            parts.append("")
        current["elems"].append(
            {
                "role": parts[0],
                "id": parts[1],
                "name": parts[2],
                "value": parts[3],
                "enabled": parts[4],
                "subrole": parts[5],
            }
        )
    return windows


def dump_windows():
    text = osa("dump")
    if text.startswith("NO_PROCESS"):
        raise RuntimeError("process is not running")
    return parse(text)


def elem(windows, ident):
    for window in windows:
        for item in window["elems"]:
            if item["id"] == ident:
                return window, item
    return None, None


def text_of(item):
    if item is None:
        return ""
    if item["name"] and item["name"] != "missing value":
        return item["name"]
    if item["value"] and item["value"] != "missing value":
        return item["value"]
    return ""


def wait_until(predicate, timeout, label):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            windows = dump_windows()
        except RuntimeError as error:
            last = str(error)
            time.sleep(0.25)
            continue
        result = predicate(windows)
        if result is not None:
            return windows, result
        last = "predicate false"
        time.sleep(0.25)
    raise RuntimeError(f"timeout waiting for {label}; last={last}")


def revision_of(windows):
    _, item = elem(windows, "catalog.revision")
    raw = text_of(item)
    return int(raw) if raw.isdigit() else None


def click(ident):
    result = osa("click", ident).strip()
    if result != "CLICKED":
        raise RuntimeError(f"click {ident}: {result}")


def shot(name):
    SHOTS.mkdir(parents=True, exist_ok=True)
    path = SHOTS / name
    subprocess.run(
        ["screencapture", "-x", "-o", str(path)],
        check=False,
    )
    print(f"screenshot {path}")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def window_ids(windows):
    found = []
    for window in windows:
        for item in window["elems"]:
            if item["role"] == "AXWindow" and item["id"].startswith("catalog.window."):
                found.append(item["id"])
    return found


def catalog_window_id(windows):
    host, _ = elem(windows, "catalog.button.add")
    if host is None:
        return None
    for item in host["elems"]:
        if item["role"] == "AXWindow" and item["id"].startswith("catalog.window."):
            return item["id"]
    return None


def child_window_ids(windows):
    catalog = catalog_window_id(windows)
    return [item for item in window_ids(windows) if item != catalog]


def frame_of(windows, window_ax_id):
    numeric = window_ax_id.rsplit(".", 1)[-1]
    _, item = elem(windows, f"catalog.window.{numeric}.frame")
    return text_of(item)


def title_of_window(windows, window_ax_id):
    numeric = window_ax_id.rsplit(".", 1)[-1]
    _, item = elem(windows, f"catalog.window.{numeric}.title")
    return text_of(item)


def rows(windows):
    found = []
    index = 0
    while True:
        _, item = elem(windows, f"catalog.row.{index}")
        if item is None:
            break
        found.append({"title": text_of(item), "id": item["value"], "index": index})
        index += 1
    return found


def button_enabled(windows, ident):
    _, item = elem(windows, ident)
    require(item is not None, f"missing {ident}")
    return item["enabled"] == "true"


def button_label(windows, ident):
    _, item = elem(windows, ident)
    require(item is not None, f"missing {ident}")
    return text_of(item)


def launch(state_dir):
    log = open(LOG, "ab")
    proc = subprocess.Popen(
        [str(APP), "--state-dir", str(state_dir)],
        stdout=log,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    print(f"launched pid {proc.pid} state {state_dir}")
    return proc


def running():
    result = subprocess.run(
        ["pgrep", "-x", PROC],
        capture_output=True,
        text=True,
    )
    return result.returncode == 0


def main():
    if not APP.is_file():
        raise SystemExit(f"missing app binary {APP}")
    if STATE.exists() and os.environ.get("WC_KEEP_STATE") != "1":
        # Fresh profile for the first launch of this script only.
        # The cold restart below reuses STATE and does not delete it.
        if os.environ.get("WC_FRESH", "1") == "1":
            import shutil

            shutil.rmtree(STATE)
    STATE.mkdir(parents=True, exist_ok=True)
    SHOTS.mkdir(parents=True, exist_ok=True)

    proc = launch(STATE)
    windows, _ = wait_until(
        lambda w: revision_of(w) if elem(w, "catalog.revision")[1] else None,
        40,
        "initial publication",
    )
    rev = revision_of(windows)
    require(rev is not None and rev >= 1, f"revision {rev}")
    require(rows(windows) == [], f"list not empty: {rows(windows)}")
    require(button_label(windows, "catalog.button.add") == "Add",
            f"add label {button_label(windows, 'catalog.button.add')!r}")
    require(button_label(windows, "catalog.button.open") == "Open",
            f"open label {button_label(windows, 'catalog.button.open')!r}")
    require(button_label(windows, "catalog.button.delete") == "Delete",
            f"delete label {button_label(windows, 'catalog.button.delete')!r}")
    require(button_label(windows, "catalog.button.language") == "RU",
            f"language label {button_label(windows, 'catalog.button.language')!r}")
    require(button_label(windows, "catalog.button.save") == "Save",
            f"save label {button_label(windows, 'catalog.button.save')!r}")
    require(not button_enabled(windows, "catalog.button.open"), "open should be disabled")
    require(not button_enabled(windows, "catalog.button.delete"), "delete should be disabled")
    require(button_enabled(windows, "catalog.button.add"), "add should be enabled")
    _, selection = elem(windows, "catalog.selection")
    require(text_of(selection) == "none", f"selection {text_of(selection)}")
    main_id = catalog_window_id(windows)
    require(main_id, "main window id")
    initial_frame = frame_of(windows, main_id)
    require(initial_frame == "80,80,640,480", f"frame {initial_frame}")
    _, session = elem(windows, "catalog.session")
    require(text_of(session) == "1", f"session {text_of(session)}")
    shot("01-initial.png")
    print(f"step1 initial rev={rev} main={main_id} frame={initial_frame}")

    click("catalog.button.add")
    windows, _ = wait_until(
        lambda w: w if len(rows(w)) == 1 and len(child_window_ids(w)) == 1 else None,
        45,
        "first add",
    )
    require(rows(windows)[0]["title"] == "Window 1", rows(windows)[0]["title"])
    first_row_id = rows(windows)[0]["id"]
    first_child = child_window_ids(windows)[0]
    require(title_of_window(windows, first_child) == "Window 1", "child title")
    print(f"step2a row={first_row_id} window={first_child} rev={revision_of(windows)}")

    click("catalog.button.add")
    windows, _ = wait_until(
        lambda w: w if len(rows(w)) == 2 and len(child_window_ids(w)) == 2 else None,
        45,
        "second add",
    )
    titles = [row["title"] for row in rows(windows)]
    require(titles == ["Window 1", "Window 2"], str(titles))
    second_row_id = rows(windows)[1]["id"]
    require(first_row_id != second_row_id, "row ids differ")
    children = child_window_ids(windows)
    child_titles = sorted(title_of_window(windows, item) for item in children)
    require(child_titles == ["Window 1", "Window 2"], str(child_titles))
    print(f"step2b rows={[first_row_id, second_row_id]} windows={children}")
    shot("02-two-windows.png")

    before_lang = revision_of(windows)
    click("catalog.button.language")
    windows, _ = wait_until(
        lambda w: w
        if revision_of(w) is not None
        and revision_of(w) > before_lang
        and title_of_window(w, main_id) == "Каталог окон"
        else None,
        45,
        "language",
    )
    require([row["title"] for row in rows(windows)] == ["Окно 1", "Окно 2"], str(rows(windows)))
    require(button_label(windows, "catalog.button.add") == "Добавить", "ru add")
    require(button_label(windows, "catalog.button.open") == "Открыть", "ru open")
    require(button_label(windows, "catalog.button.delete") == "Удалить", "ru delete")
    require(button_label(windows, "catalog.button.language") == "EN", "ru language")
    child_titles = sorted(title_of_window(windows, item) for item in child_window_ids(windows))
    require(child_titles == ["Окно 1", "Окно 2"], str(child_titles))
    print(f"step3 language rev={revision_of(windows)}")
    shot("03-language.png")

    click("catalog.row.0")
    windows, _ = wait_until(
        lambda w: w if text_of(elem(w, "catalog.selection")[1]) == "0" else None,
        45,
        "selection",
    )
    require(button_enabled(windows, "catalog.button.open"), "open enabled")
    require(button_enabled(windows, "catalog.button.delete"), "delete enabled")
    window_for_row = None
    for item in child_window_ids(windows):
        if title_of_window(windows, item) == "Окно 1":
            window_for_row = item
    other = [item for item in child_window_ids(windows) if item != window_for_row]
    require(window_for_row and len(other) == 1, "two children before close")
    other_id = other[0]
    other_frame_before = frame_of(windows, other_id)
    print(f"step4 selected row0 window={window_for_row} other={other_id}")

    before_close = revision_of(windows)
    result = osa("close", window_for_row).strip()
    require(result == "CLOSED", result)
    windows, _ = wait_until(
        lambda w: w
        if revision_of(w) is not None
        and revision_of(w) > before_close
        and window_for_row not in child_window_ids(w)
        and other_id in child_window_ids(w)
        and len(rows(w)) == 2
        else None,
        45,
        "close child",
    )
    require(rows(windows)[0]["id"] == first_row_id, "row identity after close")
    print(f"step5 closed {window_for_row} row remains rev={revision_of(windows)}")

    result = osa("move", other_id, 140, 160, 520, 360).strip()
    require(result == "MOVED", result)
    windows, _ = wait_until(
        lambda w: w
        if other_id in child_window_ids(w) and frame_of(w, other_id) != other_frame_before
        else None,
        45,
        "other window still accepts geometry",
    )
    print(f"step5b other frame {frame_of(windows, other_id)}")

    before_open = revision_of(windows)
    click("catalog.button.open")
    windows, _ = wait_until(
        lambda w: w
        if revision_of(w) is not None
        and revision_of(w) > before_open
        and window_for_row in child_window_ids(w)
        else None,
        45,
        "reopen",
    )
    require(title_of_window(windows, window_for_row) == "Окно 1", "reopened title")
    require(rows(windows)[0]["id"] == first_row_id, "row id after open")
    print(f"step6 reopened {window_for_row}")
    shot("04-reopened.png")

    before_delete = revision_of(windows)
    click("catalog.button.delete")
    windows, _ = wait_until(
        lambda w: w
        if revision_of(w) is not None
        and revision_of(w) > before_delete
        and len(rows(w)) == 1
        and window_for_row not in window_ids(w)
        else None,
        45,
        "delete",
    )
    require(rows(windows)[0]["title"] == "Окно 2", rows(windows)[0]["title"])
    require(rows(windows)[0]["id"] == second_row_id, "remaining row id")
    require(other_id in child_window_ids(windows), "remaining window")
    print(f"step7 deleted {first_row_id} kept {second_row_id}")

    main_frame_before = frame_of(windows, main_id)
    result = osa("move", main_id, 96, 64, 700, 520).strip()
    require(result == "MOVED", result)
    windows, _ = wait_until(
        lambda w: w if frame_of(w, main_id) not in ("", main_frame_before) else None,
        45,
        "main geometry",
    )
    saved_frame = frame_of(windows, main_id)
    saved_other_frame = frame_of(windows, other_id)
    print(f"step geometry main={saved_frame} other={saved_other_frame}")
    shot("05-geometry.png")

    click("catalog.button.save")
    windows, _ = wait_until(
        lambda w: w if text_of(elem(w, "catalog.save.status")[1]) == "confirmed" else None,
        30,
        "save confirmed",
    )
    print("step8 save confirmed")
    shot("06-saved.png")

    result = osa("close", main_id).strip()
    require(result == "CLOSED", result)
    deadline = time.time() + 30
    while time.time() < deadline and running():
        time.sleep(0.25)
    require(not running(), "process did not exit after main window close")
    proc.wait(timeout=5)
    print(f"step9 exited status={proc.returncode}")

    proc = launch(STATE)
    windows, _ = wait_until(
        lambda w: revision_of(w) if elem(w, "catalog.revision")[1] else None,
        40,
        "cold restart",
    )
    require(title_of_window(windows, catalog_window_id(windows)) == "Каталог окон", "language persisted")
    require(button_label(windows, "catalog.button.add") == "Добавить", "chrome persisted")
    reloaded_rows = rows(windows)
    require(len(reloaded_rows) == 1, str(reloaded_rows))
    require(reloaded_rows[0]["title"] == "Окно 2", reloaded_rows[0]["title"])
    require(reloaded_rows[0]["id"] == second_row_id, f"identity {reloaded_rows[0]['id']} != {second_row_id}")
    restarted_main = catalog_window_id(windows)
    require(frame_of(windows, restarted_main) == saved_frame, frame_of(windows, restarted_main))
    restarted_children = child_window_ids(windows)
    if other_id not in restarted_children:
        click("catalog.row.0")
        wait_until(
            lambda w: w if text_of(elem(w, "catalog.selection")[1]) == "0" else None,
            45,
            "select remaining after restart",
        )
        click("catalog.button.open")
        windows, _ = wait_until(
            lambda w: w if other_id in child_window_ids(w) else None,
            45,
            "open remaining after restart",
        )
    else:
        windows = dump_windows()
    require(title_of_window(windows, other_id) == "Окно 2", "reloaded window title")
    require(frame_of(windows, other_id) == saved_other_frame, frame_of(windows, other_id))
    _, session = elem(windows, "catalog.session")
    require(text_of(session) == "1", "session still one runtime")
    print(f"step10 reload row={second_row_id} frame={frame_of(windows, restarted_main)}")
    shot("07-reloaded.png")

    before_add = revision_of(windows)
    click("catalog.button.add")
    windows, _ = wait_until(
        lambda w: w
        if revision_of(w) is not None
        and revision_of(w) > before_add
        and len(rows(w)) == 2
        else None,
        45,
        "add after reload",
    )
    require([row["title"] for row in rows(windows)] == ["Окно 2", "Окно 3"], str(rows(windows)))
    require(rows(windows)[0]["id"] == second_row_id, "old identity")
    require(rows(windows)[1]["id"] != second_row_id, "new identity")
    new_children = [item for item in child_window_ids(windows) if item != other_id]
    require(len(new_children) == 1, str(child_window_ids(windows)))
    require(title_of_window(windows, new_children[0]) == "Окно 3", "new window title")
    print(f"step11 new window={new_children[0]} row={rows(windows)[1]['id']}")
    shot("08-add-after-reload.png")

    osa("close", catalog_window_id(windows))
    deadline = time.time() + 20
    while time.time() < deadline and running():
        time.sleep(0.25)
    print("PASS macos ui e2e")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL {error}", file=sys.stderr)
        try:
            shot("failure.png")
        except Exception:
            pass
        sys.exit(1)
