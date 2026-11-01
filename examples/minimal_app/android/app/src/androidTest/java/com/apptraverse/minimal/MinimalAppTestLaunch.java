package com.apptraverse.minimal;

import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Process;

import java.util.List;

import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.Until;

import java.io.File;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

/** Launches Minimal App with an isolated on-disk profile for instrumented tests. */
final class MinimalAppTestLaunch {

  static {
    InstrumentedTestNative.ensureLoaded();
  }

  static final String PKG = "com.apptraverse.minimal";
  static final String LOG_LOADED_FROM_PROFILE = "MINIMAL_APP_LOADED_FROM_PROFILE";
  static final String LOG_PROFILE_BOOTSTRAP = "MINIMAL_APP_PROFILE_BOOTSTRAP";

  private MinimalAppTestLaunch() {}

  static void deleteProfileTree(Context context, String stateSubdir) {
    File root = new File(context.getFilesDir(), stateSubdir);
    deleteRecursively(root);
  }

  static void launchFreshProfile(Context context, UiDevice device, String stateSubdir) {
    launchFreshProfile(context, device, stateSubdir, true);
  }

  static void launchFreshProfile(Context context, UiDevice device, String stateSubdir,
      boolean skipCheckpointSaveOnStop) {
    deleteProfileTree(context, stateSubdir);
    launchExistingProfile(context, device, stateSubdir, skipCheckpointSaveOnStop);
  }

  static void launchExistingProfile(Context context, UiDevice device, String stateSubdir) {
    launchExistingProfile(context, device, stateSubdir, true);
  }

  static void launchExistingProfile(Context context, UiDevice device, String stateSubdir,
      boolean skipCheckpointSaveOnStop) {
    device.pressHome();
    stopTargetApp(device);
    startMainActivity(context, stateSubdir, skipCheckpointSaveOnStop);
    assertTrue(device.wait(Until.hasObject(By.pkg(PKG).depth(0)), 15000));
  }

  /** Brings the demo Activity to foreground without killing the process. */
  static void bringAppToForeground(Context context, UiDevice device, String stateSubdir,
      boolean skipCheckpointSaveOnStop) {
    startMainActivity(context, stateSubdir, skipCheckpointSaveOnStop);
    assertTrue(device.wait(Until.hasObject(By.pkg(PKG).depth(0)), 15000));
  }

  static int readDemoProcessPid(Context context, UiDevice device, int excludePid)
      throws Exception {
    return readDemoProcessPid(context, device, excludePid, false);
  }

  /** Requires {@code com.apptraverse.minimal:demo} (not default package process). */
  static int readDemoProcessPidStrict(Context context, UiDevice device, int excludePid)
      throws Exception {
    return readDemoProcessPid(context, device, excludePid, true);
  }

  private static int readDemoProcessPid(Context context, UiDevice device, int excludePid,
      boolean strictDemoSuffix) throws Exception {
    ActivityManager am =
        (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
    if (am != null) {
      List<ActivityManager.RunningAppProcessInfo> processes = am.getRunningAppProcesses();
      if (processes != null) {
        int fallback = -1;
        for (ActivityManager.RunningAppProcessInfo info : processes) {
          if (strictDemoSuffix) {
            if (!isStrictDemoProcessName(info.processName)) {
              continue;
            }
          } else if (!isDemoProcessName(info.processName)) {
            continue;
          }
          if (info.pid == excludePid) {
            continue;
          }
          if (info.processName.equals(PKG + ":demo")) {
            return info.pid;
          }
          if (!strictDemoSuffix && fallback < 0) {
            fallback = info.pid;
          }
        }
        if (fallback > 0) {
          return fallback;
        }
      }
    }
    return readDemoProcessPidFromShell(device, excludePid, strictDemoSuffix);
  }

  private static boolean isStrictDemoProcessName(String processName) {
    return (PKG + ":demo").equals(processName);
  }

  private static int readDemoProcessPidFromShell(UiDevice device, int excludePid,
      boolean strictDemoSuffix) throws Exception {
    int fromDumpsys = readDemoProcessPidFromDumpsys(device, excludePid, strictDemoSuffix);
    if (fromDumpsys > 0) {
      return fromDumpsys;
    }
    String out = device.executeShellCommand("ps -A -o PID,NAME").trim();
    for (String line : out.split("\n")) {
      String trimmed = line.trim();
      if (trimmed.isEmpty() || trimmed.startsWith("PID")) {
        continue;
      }
      if (trimmed.contains(".test") || trimmed.contains("instrumentedtests")) {
        continue;
      }
      if (strictDemoSuffix && !trimmed.contains(PKG + ":demo")) {
        continue;
      }
      if (!strictDemoSuffix && !trimmed.contains(PKG)) {
        continue;
      }
      String[] parts = trimmed.split("\\s+");
      if (parts.length < 2) {
        continue;
      }
      int pid;
      try {
        pid = Integer.parseInt(parts[0]);
      } catch (NumberFormatException ignored) {
        continue;
      }
      if (pid == excludePid) {
        continue;
      }
      return pid;
    }
    return -1;
  }

  private static int readDemoProcessPidFromDumpsys(UiDevice device, int excludePid,
      boolean strictDemoSuffix) throws Exception {
    String needle = strictDemoSuffix ? (PKG + ":demo") : PKG;
    Pattern pattern = Pattern.compile("(\\d+):" + Pattern.quote(needle) + "/");
    Matcher matcher = pattern.matcher(device.executeShellCommand("dumpsys activity processes"));
    int fallback = -1;
    while (matcher.find()) {
      int pid = Integer.parseInt(matcher.group(1));
      if (pid == excludePid) {
        continue;
      }
      if (strictDemoSuffix) {
        return pid;
      }
      if (fallback < 0) {
        fallback = pid;
      }
    }
    return fallback;
  }

  static boolean isDemoProcessName(String processName) {
    if (processName == null) {
      return false;
    }
    if (processName.contains(".test") || processName.contains("instrumentedtests")) {
      return false;
    }
    return processName.equals(PKG) || processName.equals(PKG + ":demo");
  }

  static void killDemoProcessAndWait(Context context, UiDevice device, int pid,
      long timeoutMs) throws Exception {
    int instrumentationPid = Process.myPid();
    if (pid == instrumentationPid) {
      throw new AssertionError(
          "demo pid matches instrumentation pid; cannot kill in-process");
    }
    device.executeShellCommand("am kill " + PKG);
    long deadline = System.currentTimeMillis() + timeoutMs;
    while (System.currentTimeMillis() < deadline) {
      if (!isDemoProcessAlive(context, pid)) {
        return;
      }
      Thread.sleep(250);
    }
    device.executeShellCommand("kill -9 " + pid);
    deadline = System.currentTimeMillis() + 10000;
    while (System.currentTimeMillis() < deadline) {
      if (!isDemoProcessAlive(context, pid)) {
        return;
      }
      Thread.sleep(250);
    }
    throw new AssertionError("demo pid still alive after kill: " + pid);
  }

  static boolean isDemoProcessAlive(Context context, int pid) {
    ActivityManager am =
        (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
    if (am == null) {
      return false;
    }
    List<ActivityManager.RunningAppProcessInfo> processes = am.getRunningAppProcesses();
    if (processes == null) {
      return false;
    }
    for (ActivityManager.RunningAppProcessInfo info : processes) {
      if (info.pid == pid && isDemoProcessName(info.processName)) {
        return true;
      }
    }
    return false;
  }

  static int waitForDemoProcessPid(Context context, UiDevice device, long timeoutMs)
      throws Exception {
    return waitForDemoProcessPid(context, device, -1, timeoutMs);
  }

  static int waitForDemoProcessPid(Context context, UiDevice device, int excludePid,
      long timeoutMs) throws Exception {
    return waitForDemoProcessPid(context, device, excludePid, timeoutMs, false);
  }

  static int waitForStrictDemoProcessPid(Context context, UiDevice device, int excludePid,
      long timeoutMs) throws Exception {
    return waitForDemoProcessPid(context, device, excludePid, timeoutMs, true);
  }

  private static int waitForDemoProcessPid(Context context, UiDevice device, int excludePid,
      long timeoutMs, boolean strictDemoSuffix) throws Exception {
    long deadline = System.currentTimeMillis() + timeoutMs;
    while (System.currentTimeMillis() < deadline) {
      int pid = readDemoProcessPid(context, device, excludePid, strictDemoSuffix);
      if (pid > 0) {
        return pid;
      }
      if (strictDemoSuffix) {
        int demoOnly = readDemoProcessPidFromDumpsys(device, -1, true);
        if (demoOnly > 0 && demoOnly == excludePid) {
          throw new AssertionError(
              "instrumentation co-located in :demo pid=" + demoOnly);
        }
      }
      Thread.sleep(200);
    }
    throw new AssertionError("demo process pid not found");
  }

  static void waitForApplyCheckpointOnDisk(Context context, String profileSubdir,
      long timeoutMs) throws Exception {
    String profilePath = new File(context.getFilesDir(), profileSubdir).getAbsolutePath();
    long deadline = System.currentTimeMillis() + timeoutMs;
    String last = "";
    while (System.currentTimeMillis() < deadline) {
      last = NativeRuntime.nativeRunColdReloadApplyStateProbe(profilePath);
      if (last.startsWith("OK:")) {
        return;
      }
      Thread.sleep(400);
    }
    throw new AssertionError("checkpoint apply probe: " + last);
  }

  static boolean isApplyCheckpointOnDisk(Context context, String profileSubdir) {
    String profilePath = new File(context.getFilesDir(), profileSubdir).getAbsolutePath();
    return NativeRuntime.nativeRunColdReloadApplyStateProbe(profilePath).startsWith("OK:");
  }

  static void assertApplyCheckpointNotOnDiskWithin(Context context, String profileSubdir,
      long windowMs) throws Exception {
    long deadline = System.currentTimeMillis() + windowMs;
    while (System.currentTimeMillis() < deadline) {
      assertFalse("checkpoint must not be complete yet: " + profileSubdir,
          isApplyCheckpointOnDisk(context, profileSubdir));
      Thread.sleep(200);
    }
  }

  static void waitForLoadedFromProfileLog(UiDevice device, long timeoutMs) throws Exception {
    long deadline = System.currentTimeMillis() + timeoutMs;
    while (System.currentTimeMillis() < deadline) {
      String log = device.executeShellCommand("logcat -d -T 120");
      if (log.contains(LOG_LOADED_FROM_PROFILE)) {
        return;
      }
      Thread.sleep(400);
    }
    throw new AssertionError(LOG_LOADED_FROM_PROFILE + " not observed in log tail");
  }

  static void assertColdLoadFromProfile(Context context, UiDevice device, String profileSubdir,
      long timeoutMs) throws Exception {
    String profilePath = new File(context.getFilesDir(), profileSubdir).getAbsolutePath();
    String probe = NativeRuntime.nativeRunColdReloadApplyStateProbe(profilePath);
    assertTrue("cold load disk probe: " + probe, probe.startsWith("OK:"));
    waitForLoadedFromProfileLog(device, Math.min(timeoutMs, 15000));
  }

  static String demoProcessShellDiagnostics(UiDevice device) throws Exception {
    String dumpsys = device.executeShellCommand("dumpsys activity processes");
    StringBuilder hits = new StringBuilder();
    for (String line : dumpsys.split("\n")) {
      if (line.contains(PKG)) {
        if (hits.length() > 0) {
          hits.append(" | ");
        }
        hits.append(line.trim());
      }
    }
    return hits.toString();
  }

  private static void startMainActivity(Context context, String stateSubdir,
      boolean skipCheckpointSaveOnStop) {
    Intent intent = new Intent(Intent.ACTION_MAIN);
    intent.setComponent(new ComponentName(PKG, PKG + ".MainActivity"));
    intent.addCategory(Intent.CATEGORY_LAUNCHER);
    intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
    intent.putExtra(MinimalAppStateDirs.INTENT_EXTRA_STATE_SUBDIR, stateSubdir);
    if (skipCheckpointSaveOnStop) {
      intent.putExtra(MinimalAppStateDirs.INTENT_EXTRA_SKIP_CHECKPOINT_SAVE, true);
    }
    context.startActivity(intent);
  }

  private static void stopTargetApp(UiDevice device) {
    try {
      device.executeShellCommand("am kill " + PKG);
      Thread.sleep(500);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private static void deleteRecursively(File file) {
    if (file == null || !file.exists()) {
      return;
    }
    if (file.isDirectory()) {
      File[] children = file.listFiles();
      if (children != null) {
        for (File child : children) {
          deleteRecursively(child);
        }
      }
    }
    file.delete();
  }
}
