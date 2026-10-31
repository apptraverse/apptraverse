package com.apptraverse.minimal;

import android.content.Context;
import android.content.Intent;

import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.Until;

import java.io.File;

import static org.junit.Assert.assertTrue;

/** Launches Minimal App with an isolated on-disk profile for instrumented tests. */
final class MinimalAppTestLaunch {

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
    Intent intent = context.getPackageManager()
        .getLaunchIntentForPackage("com.apptraverse.minimal");
    intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
    intent.putExtra(MinimalAppStateDirs.INTENT_EXTRA_STATE_SUBDIR, stateSubdir);
    if (skipCheckpointSaveOnStop) {
      intent.putExtra(MinimalAppStateDirs.INTENT_EXTRA_SKIP_CHECKPOINT_SAVE, true);
    }
    context.startActivity(intent);
    assertTrue(device.wait(Until.hasObject(By.pkg("com.apptraverse.minimal").depth(0)),
        15000));
  }

  private static void stopTargetApp(UiDevice device) {
    try {
      device.executeShellCommand("am kill com.apptraverse.minimal");
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
