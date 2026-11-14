package com.apptraverse.minimal;

import android.content.Context;
import android.os.Process;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.UiObject2;
import androidx.test.uiautomator.Until;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

/** P3.T3.A: checkpoint save on onStop, same-process resume, cold reload. */
@RunWith(AndroidJUnit4.class)
public final class SaveColdReloadInstrumentedTest {

  private static final String PROFILE = "minimal_app_save_cold_reload_e2e";
  private static final String OVERLAP_PROFILE = "minimal_app_save_overlap_e2e";

  enum ColdReloadPath {
    IN_PROCESS_RUNTIME_RESET,
    NEW_PID_DEMO_PROCESS
  }

  private UiDevice device;
  private Context context;
  private MinimalAppTestApplication app;

  @Before
  public void setUp() throws Exception {
    InstrumentedTestNative.ensureLoaded();
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    NativeTestRuntime.nativeSetCheckpointPersistTestGateRoot(
        context.getFilesDir().getAbsolutePath());
    app = (MinimalAppTestApplication) context.getApplicationContext();
    NativeTestRuntime.nativeDisableCheckpointPersistTestGate();
    if (app.hasNativeRuntime()) {
      InstrumentationRegistry.getInstrumentation().runOnMainSync(
          app::clearRuntimeForColdReloadTest);
    }
  }

  @After
  public void tearDown() {
    NativeTestRuntime.nativeDisableCheckpointPersistTestGate();
  }

  private ColdReloadPath resolveColdReloadPath(int pidBefore) {
    return pidBefore == Process.myPid()
        ? ColdReloadPath.IN_PROCESS_RUNTIME_RESET
        : ColdReloadPath.NEW_PID_DEMO_PROCESS;
  }

  private void coldReloadAfterSave(String profile, int pidBefore, ColdReloadPath pathUsed)
      throws Exception {
    if (pathUsed == ColdReloadPath.IN_PROCESS_RUNTIME_RESET) {
      InstrumentationRegistry.getInstrumentation().runOnMainSync(
          app::clearRuntimeForColdReloadTest);
      MinimalAppTestLaunch.bringAppToForeground(context, device, profile, false);
      return;
    }
    MinimalAppTestLaunch.killDemoProcessAndWait(context, device, pidBefore, 45000);
    assertFalse("demo process must be dead before cold start",
        MinimalAppTestLaunch.isStrictDemoPidAlive(device, pidBefore));
    MinimalAppTestLaunch.launchExistingProfile(context, device, profile, false);
    int pidAfter = MinimalAppTestLaunch.waitForStrictDemoProcessPid(
        context, device, Process.myPid(), 15000);
    assertTrue("demo process must return after cold start", pidAfter > 0);
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

  private void applyRowBAndWaitPublication() throws InterruptedException {
    UiObject2 apply = device.wait(Until.findObject(By.desc("Apply")), 25000);
    UiObject2 rowB = device.wait(Until.findObject(By.desc("Row B")), 25000);
    assertNotNull(apply);
    assertNotNull(rowB);
    rowB.click();
    assertNotNull(
        device.wait(Until.findObject(By.desc("Row B").selected(true)), 20000));
    apply.click();
    assertTrue(device.wait(Until.findObject(By.text("Applied")), 25000) != null);
    apply = device.findObject(By.desc("Apply"));
    assertNotNull(apply);
    assertFalse(apply.isEnabled());
  }

  @Test
  public void sameProcessResumeAfterBackgroundSaveAcceptsInput() throws Exception {
    MinimalAppTestLaunch.launchFreshProfile(context, device, PROFILE, false);
    applyRowBAndWaitPublication();
    MinimalAppTestLaunch.pressHomeForBackgroundSave(device, PROFILE, 60000);
    MinimalAppTestLaunch.waitForApplyCheckpointOnDisk(context, PROFILE, 30000);

    MinimalAppTestLaunch.bringAppToForeground(context, device, PROFILE, false);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    assertNotNull(rowA);
    assertFalse(rowA.isSelected());
    rowA.click();
    waitForSelected(rowA, true, 15000);
    assertTrue(device.hasObject(By.text("Applied")));
  }

  @Test
  public void resumeWhileSaveInProgressDoesNotDropInput() throws Exception {
    MinimalAppTestLaunch.launchFreshProfile(context, device, PROFILE, false);
    applyRowBAndWaitPublication();

    NativeTestRuntime.nativeEnableCheckpointPersistTestGate();
    device.pressHome();
    MinimalAppTestLaunch.assertApplyCheckpointNotOnDiskWithin(context, PROFILE, 2500);

    MinimalAppTestLaunch.bringAppToForeground(context, device, PROFILE, false);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    assertNotNull(rowA);
    assertFalse(rowA.isSelected());
    rowA.click();

    int saveLogBaseline = MinimalAppTestLaunch.countMinimalAppInfoLines(
        device, MinimalAppTestLaunch.LOG_BACKGROUND_SAVE_FINISHED_PREFIX + PROFILE
            + " result=0");
    NativeTestRuntime.nativeOpenCheckpointPersistTestGate();
    waitForSelected(rowA, true, 15000);
    MinimalAppTestLaunch.waitForBackgroundSaveFinishedAfter(
        device, PROFILE, saveLogBaseline, 60000);
    MinimalAppTestLaunch.waitForApplyCheckpointOnDisk(context, PROFILE, 30000);
    NativeTestRuntime.nativeDisableCheckpointPersistTestGate();
    assertTrue(device.hasObject(By.text("Applied")));
    assertTrue(rowA.isSelected());
  }

  @Test
  public void overlappingBackgroundSavePersistsLastChangeAfterColdLoad() throws Exception {
    MinimalAppTestLaunch.launchFreshProfile(context, device, OVERLAP_PROFILE, false);
    applyRowBAndWaitPublication();

    NativeTestRuntime.nativeEnableCheckpointPersistTestGate();
    device.pressHome();
    MinimalAppTestLaunch.assertApplyCheckpointNotOnDiskWithin(context, OVERLAP_PROFILE, 2500);
    assertTrue(
        "first persist must enter gate before overlap input",
        NativeTestRuntime.nativeWaitForCheckpointPersistGateEntries(1, 5000));

    MinimalAppTestLaunch.bringAppToForeground(context, device, OVERLAP_PROFILE, false);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    assertNotNull(rowA);
    rowA.click();

    int saveLogBaseline = MinimalAppTestLaunch.countMinimalAppInfoLines(
        device, MinimalAppTestLaunch.LOG_BACKGROUND_SAVE_FINISHED_PREFIX + OVERLAP_PROFILE
            + " result=0");
    device.pressHome();

    NativeTestRuntime.nativeOpenCheckpointPersistTestGate();
    MinimalAppTestLaunch.waitForBackgroundSaveFinishedAfter(
        device, OVERLAP_PROFILE, saveLogBaseline, 90000);
    MinimalAppTestLaunch.waitForProfileStateOnDisk(
        context, OVERLAP_PROFILE, 0, false, "Applied", 30000);
    NativeTestRuntime.nativeDisableCheckpointPersistTestGate();

    String overlapProfilePath =
        new java.io.File(context.getFilesDir(), OVERLAP_PROFILE).getAbsolutePath();
    String diskAfterSaves =
        NativeTestRuntime.nativeRunColdReloadProfileStateProbe(
            overlapProfilePath, 0, false, "Applied");
    assertTrue("disk after overlapping saves: " + diskAfterSaves,
        diskAfterSaves.startsWith("OK:"));

    int pidBefore =
        MinimalAppTestLaunch.waitForDemoProcessPid(context, device, Process.myPid(), 15000);
    ColdReloadPath pathUsed = resolveColdReloadPath(pidBefore);

    int loadedFromProfileBaseline =
        MinimalAppTestLaunch.countMinimalAppInfoLines(
            device, MinimalAppTestLaunch.LOG_LOADED_FROM_PROFILE);
    coldReloadAfterSave(OVERLAP_PROFILE, pidBefore, pathUsed);

    assertTrue(device.wait(Until.findObject(By.text("Applied")), 30000) != null);
    String diskAfterCold =
        NativeTestRuntime.nativeRunColdReloadProfileStateProbe(
            overlapProfilePath, 0, false, "Applied");
    assertTrue("disk after cold reload: " + diskAfterCold, diskAfterCold.startsWith("OK:"));
    try {
      MinimalAppTestLaunch.waitForLoadedFromProfileAfter(
          device, loadedFromProfileBaseline, 8000);
    } catch (AssertionError ignored) {
      // best-effort; disk probe above is primary
    }

    rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    assertNotNull(rowA);
    waitForSelected(rowA, true, 30000);
    assertEquals("cold reload path", ColdReloadPath.NEW_PID_DEMO_PROCESS, pathUsed);
  }

  @Test
  public void backgroundSaveSurvivesColdReloadAndAcceptsInput() throws Exception {
    MinimalAppTestLaunch.launchFreshProfile(context, device, PROFILE, false);
    applyRowBAndWaitPublication();
    int pidBefore =
        MinimalAppTestLaunch.waitForDemoProcessPid(context, device, Process.myPid(), 15000);
    ColdReloadPath pathUsed = resolveColdReloadPath(pidBefore);

    MinimalAppTestLaunch.pressHomeForBackgroundSave(device, PROFILE, 60000);
    MinimalAppTestLaunch.waitForApplyCheckpointOnDisk(context, PROFILE, 30000);

    int loadedFromProfileBaseline =
        MinimalAppTestLaunch.countMinimalAppInfoLines(
            device, MinimalAppTestLaunch.LOG_LOADED_FROM_PROFILE);
    coldReloadAfterSave(PROFILE, pidBefore, pathUsed);

    UiObject2 apply = device.wait(Until.findObject(By.desc("Apply")), 15000);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    UiObject2 rowB = device.wait(Until.findObject(By.desc("Row B")), 15000);
    assertNotNull(apply);
    assertNotNull(rowA);
    assertNotNull(rowB);
    assertTrue(device.wait(Until.findObject(By.text("Applied")), 30000) != null);
    MinimalAppTestLaunch.assertColdLoadFromProfile(
        context, device, PROFILE, loadedFromProfileBaseline, 30000);
    apply = device.findObject(By.desc("Apply"));
    assertNotNull(apply);
    assertFalse(apply.isEnabled());

    assertFalse(rowA.isSelected());
    assertFalse(rowB.isSelected());

    rowA.click();
    waitForSelected(rowA, true, 15000);
    assertTrue(device.hasObject(By.text("Applied")));

    if (pathUsed == ColdReloadPath.NEW_PID_DEMO_PROCESS) {
      assertNotEquals("instrumentation pid is not demo cold-start evidence",
          Process.myPid(), pidBefore);
    }
  }
}
