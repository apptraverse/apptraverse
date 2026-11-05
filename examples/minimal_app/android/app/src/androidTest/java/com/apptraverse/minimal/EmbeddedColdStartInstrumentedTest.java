package com.apptraverse.minimal;

import android.content.Context;
import android.os.Process;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.UiObject2;
import androidx.test.uiautomator.Until;

import java.io.File;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

/**
 * Build-time embedded BACKING + USER profile: first run from the supply with an
 * empty USER, checkpoint and re-save into USER, then a new demo process cold
 * loads the same USER.
 */
@RunWith(AndroidJUnit4.class)
public final class EmbeddedColdStartInstrumentedTest {

  private static final String PROFILE = "minimal_app_embedded_cold_start_e2e";
  private static final String EMPTY_USER = "minimal_app_embedded_backing_only";

  private UiDevice device;
  private Context context;
  private String profilePath;

  @Before
  public void setUp() {
    InstrumentedTestNative.ensureLoaded();
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    NativeRuntime.nativeSetCheckpointPersistTestGateRoot(
        context.getFilesDir().getAbsolutePath());
    NativeRuntime.nativeDisableCheckpointPersistTestGate();
    profilePath = new File(context.getFilesDir(), PROFILE).getAbsolutePath();
  }

  @After
  public void tearDown() {
    NativeRuntime.nativeDisableCheckpointPersistTestGate();
  }

  private void waitForSelected(UiObject2 row, boolean selected, long timeoutMs)
      throws InterruptedException {
    long deadline = System.currentTimeMillis() + timeoutMs;
    while (System.currentTimeMillis() < deadline) {
      if (row.isSelected() == selected) {
        return;
      }
      Thread.sleep(100);
    }
    assertTrue("row selected=" + row.isSelected() + " expected=" + selected,
        row.isSelected() == selected);
  }

  private String emptyUserPath() {
    return new File(context.getFilesDir(), EMPTY_USER).getAbsolutePath();
  }

  @Test
  public void embeddedSupplyToUserCheckpointAndNewProcessColdLoad() throws Exception {
    int instrumentationPid = Process.myPid();
    int fromBackingBaseline = MinimalAppTestLaunch.countNativeInfoLines(
        device, MinimalAppTestLaunch.LOG_LOADED_FROM_EMBEDDED_BACKING);

    MinimalAppTestLaunch.deleteProfileTree(context, PROFILE);
    String[] userBefore = new File(profilePath).list();
    assertTrue("USER must be empty before first run",
        userBefore == null || userBefore.length == 0);
    MinimalAppTestLaunch.launchExistingProfile(context, device, PROFILE, false);
    int pidBefore = MinimalAppTestLaunch.waitForStrictDemoProcessPid(
        context, device, instrumentationPid, 30000);
    assertNotEquals("demo runs outside instrumentation", instrumentationPid, pidBefore);

    // Empty USER: the initial UI comes from the embedded supply.
    UiObject2 apply = device.wait(Until.findObject(By.desc("Apply")), 30000);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 30000);
    UiObject2 rowB = device.wait(Until.findObject(By.desc("Row B")), 30000);
    assertNotNull(apply);
    assertNotNull(rowA);
    assertNotNull(rowB);
    assertNotNull(device.wait(Until.findObject(By.desc("Open Detail")), 30000));
    assertTrue("Apply enabled in embedded initial state", apply.isEnabled());
    assertFalse(rowA.isSelected());
    assertFalse(rowB.isSelected());
    assertFalse(device.hasObject(By.text("Applied")));
    MinimalAppTestLaunch.waitForNativeInfoLineAfter(device,
        MinimalAppTestLaunch.LOG_LOADED_FROM_EMBEDDED_BACKING, fromBackingBaseline, 15000);

    // Row B -> Apply: expected state after publication.
    rowB.click();
    assertNotNull(
        device.wait(Until.findObject(By.desc("Row B").selected(true)), 20000));
    apply.click();
    assertNotNull(device.wait(Until.findObject(By.text("Applied")), 30000));
    apply = device.findObject(By.desc("Apply"));
    assertNotNull(apply);
    assertFalse(apply.isEnabled());
    assertFalse(device.findObject(By.desc("Row B")).isSelected());

    // Home: confirmed checkpoint into USER; BACKING unchanged.
    MinimalAppTestLaunch.pressHomeForBackgroundSave(device, PROFILE, 60000);
    String afterCheckpoint = NativeRuntime.nativeRunColdReloadProfileStateProbe(
        profilePath, -1, false, "Applied");
    assertTrue("USER after checkpoint: " + afterCheckpoint,
        afterCheckpoint.startsWith("OK:"));
    String backing = NativeRuntime.nativeRunEmbeddedBackingProbe(profilePath, emptyUserPath());
    assertTrue("BACKING after checkpoint: " + backing, backing.startsWith("OK:"));

    // Foreground in the same process: a new command is accepted.
    MinimalAppTestLaunch.bringAppToForeground(context, device, PROFILE, false);
    rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    assertNotNull(rowA);
    assertFalse(rowA.isSelected());
    rowA.click();
    waitForSelected(rowA, true, 15000);
    assertTrue(device.hasObject(By.text("Applied")));

    // Re-save after the new command.
    MinimalAppTestLaunch.pressHomeForBackgroundSave(device, PROFILE, 60000);
    String afterResave = NativeRuntime.nativeRunColdReloadProfileStateProbe(
        profilePath, 0, false, "Applied");
    assertTrue("USER after re-save: " + afterResave, afterResave.startsWith("OK:"));
    backing = NativeRuntime.nativeRunEmbeddedBackingProbe(profilePath, emptyUserPath());
    assertTrue("BACKING after re-save: " + backing, backing.startsWith("OK:"));

    // Kill only the demo process and prove its death.
    assertTrue(MinimalAppTestLaunch.isStrictDemoPidAlive(device, pidBefore));
    MinimalAppTestLaunch.killDemoProcessAndWait(context, device, pidBefore, 45000);
    assertFalse("demo process must be dead",
        MinimalAppTestLaunch.isStrictDemoPidAlive(device, pidBefore));

    // New demo process, same USER.
    int fromProfileBaseline = MinimalAppTestLaunch.countNativeInfoLines(
        device, MinimalAppTestLaunch.LOG_LOADED_FROM_PROFILE);
    MinimalAppTestLaunch.launchExistingProfile(context, device, PROFILE, false);
    int pidAfter = MinimalAppTestLaunch.waitForStrictDemoProcessPid(
        context, device, instrumentationPid, 30000);
    assertTrue("demo process must start again", pidAfter > 0);
    assertTrue(MinimalAppTestLaunch.isStrictDemoPidAlive(device, pidAfter));
    MinimalAppTestLaunch.waitForNativeInfoLineAfter(device,
        MinimalAppTestLaunch.LOG_LOADED_FROM_PROFILE, fromProfileBaseline, 15000);

    // Restored state before any click.
    assertNotNull(device.wait(Until.findObject(By.text("Applied")), 30000));
    apply = device.wait(Until.findObject(By.desc("Apply")), 30000);
    rowA = device.wait(Until.findObject(By.desc("Row A")), 30000);
    rowB = device.wait(Until.findObject(By.desc("Row B")), 30000);
    assertNotNull(apply);
    assertNotNull(rowA);
    assertNotNull(rowB);
    assertFalse("Apply restored disabled", apply.isEnabled());
    waitForSelected(rowA, true, 15000);
    assertFalse("Row B restored unselected", rowB.isSelected());

    // New command after the cold load.
    rowB.click();
    waitForSelected(rowB, true, 15000);
    waitForSelected(rowA, false, 15000);
    assertTrue(device.hasObject(By.text("Applied")));
  }
}
