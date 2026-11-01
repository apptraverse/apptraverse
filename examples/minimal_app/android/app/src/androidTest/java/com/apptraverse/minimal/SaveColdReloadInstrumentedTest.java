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

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

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
  private MinimalAppApplication app;

  @Before
  public void setUp() {
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    app = (MinimalAppApplication) context.getApplicationContext();
    NativeRuntime.nativeDisableCheckpointPersistTestGate();
    if (app.hasNativeRuntime()) {
      InstrumentationRegistry.getInstrumentation().runOnMainSync(
          app::clearRuntimeForColdReloadTest);
    }
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

  private void applyRowBAndWaitPublication() throws InterruptedException {
    UiObject2 apply = device.wait(Until.findObject(By.desc("Apply")), 15000);
    UiObject2 rowB = device.wait(Until.findObject(By.desc("Row B")), 15000);
    assertNotNull(apply);
    assertNotNull(rowB);
    rowB.click();
    waitForSelected(rowB, true, 10000);
    apply.click();
    assertTrue(device.wait(Until.findObject(By.text("Applied")), 15000) != null);
    apply = device.findObject(By.desc("Apply"));
    assertNotNull(apply);
    assertFalse(apply.isEnabled());
  }

  private SaveLatch armSaveConfirmation(int expectedCount) {
    app.clearBackgroundSaveLatch();
    app.setBackgroundSaveObserver(null);
    SaveLatch latch = new SaveLatch(expectedCount);
    app.setBackgroundSaveObserver(latch.result::set);
    app.setBackgroundSaveLatch(latch.countDown);
    return latch;
  }

  private void disarmSaveConfirmation() {
    app.clearBackgroundSaveLatch();
    app.setBackgroundSaveObserver(null);
  }

  @Test
  public void sameProcessResumeAfterBackgroundSaveAcceptsInput() throws Exception {
    MinimalAppTestLaunch.launchFreshProfile(context, device, PROFILE, false);
    applyRowBAndWaitPublication();
    SaveLatch save = armSaveConfirmation(1);
    device.pressHome();
    assertTrue("background save confirmation", save.await(30, TimeUnit.SECONDS));
    assertEquals(0, save.result.get());
    disarmSaveConfirmation();

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

    NativeRuntime.nativeEnableCheckpointPersistTestGate();
    SaveLatch save = armSaveConfirmation(1);
    device.pressHome();

    assertFalse("save must not finish while gate closed",
        save.await(2, TimeUnit.SECONDS));

    MinimalAppTestLaunch.bringAppToForeground(context, device, PROFILE, false);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    assertNotNull(rowA);
    assertFalse(rowA.isSelected());
    rowA.click();

    NativeRuntime.nativeOpenCheckpointPersistTestGate();
    waitForSelected(rowA, true, 15000);
    assertTrue("background save confirmation", save.await(30, TimeUnit.SECONDS));
    assertEquals(0, save.result.get());
    disarmSaveConfirmation();
    NativeRuntime.nativeDisableCheckpointPersistTestGate();
    assertTrue(device.hasObject(By.text("Applied")));
    assertTrue(rowA.isSelected());
  }

  @Test
  public void overlappingBackgroundSavePersistsLastChangeAfterColdLoad() throws Exception {
    MinimalAppTestLaunch.launchFreshProfile(context, device, OVERLAP_PROFILE, false);
    applyRowBAndWaitPublication();

    NativeRuntime.nativeEnableCheckpointPersistTestGate();
    SaveLatch saves = armSaveConfirmation(2);
    device.pressHome();
    assertFalse("first save still in gate", saves.await(2, TimeUnit.SECONDS));
    assertTrue(
        "first persist must enter gate before overlap input",
        NativeRuntime.nativeWaitForCheckpointPersistGateEntries(1, 5000));

    MinimalAppTestLaunch.bringAppToForeground(context, device, OVERLAP_PROFILE, false);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    assertNotNull(rowA);
    rowA.click();

    device.pressHome();

    NativeRuntime.nativeOpenCheckpointPersistTestGate();
    assertTrue("both checkpoint saves confirmed", saves.await(45, TimeUnit.SECONDS));
    assertEquals(
        "last checkpoint confirmed by final onBackgroundSaveFinished(0)",
        0,
        saves.result.get());
    disarmSaveConfirmation();
    NativeRuntime.nativeDisableCheckpointPersistTestGate();

    String overlapProfilePath =
        new java.io.File(context.getFilesDir(), OVERLAP_PROFILE).getAbsolutePath();
    String diskAfterSaves =
        NativeRuntime.nativeRunColdReloadProfileStateProbe(
            overlapProfilePath, 0, false, "Applied");
    assertTrue("disk after overlapping saves: " + diskAfterSaves,
        diskAfterSaves.startsWith("OK:"));

    int pidBefore = MinimalAppTestLaunch.waitForDemoProcessPid(context, device, -1, 15000);
    ColdReloadPath pathUsed =
        pidBefore == Process.myPid()
            ? ColdReloadPath.IN_PROCESS_RUNTIME_RESET
            : ColdReloadPath.NEW_PID_DEMO_PROCESS;

    InstrumentationRegistry.getInstrumentation().runOnMainSync(
        app::clearRuntimeForColdReloadTest);
    MinimalAppTestLaunch.bringAppToForeground(context, device, OVERLAP_PROFILE, false);

    assertTrue(device.wait(Until.findObject(By.text("Applied")), 30000) != null);
    String diskAfterCold =
        NativeRuntime.nativeRunColdReloadProfileStateProbe(
            overlapProfilePath, 0, false, "Applied");
    assertTrue("disk after cold reload: " + diskAfterCold, diskAfterCold.startsWith("OK:"));
    MinimalAppTestLaunch.waitForLoadedFromProfileLog(device, 15000);

    rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    assertNotNull(rowA);
    waitForSelected(rowA, true, 30000);
    assertEquals("cold reload path", ColdReloadPath.IN_PROCESS_RUNTIME_RESET, pathUsed);
  }

  @Test
  public void backgroundSaveSurvivesColdReloadAndAcceptsInput() throws Exception {
    MinimalAppTestLaunch.launchFreshProfile(context, device, PROFILE, false);
    applyRowBAndWaitPublication();
    int pidBefore = MinimalAppTestLaunch.waitForDemoProcessPid(context, device, -1, 15000);
    boolean inProcessDemo = pidBefore == Process.myPid();
    ColdReloadPath pathUsed = inProcessDemo
        ? ColdReloadPath.IN_PROCESS_RUNTIME_RESET
        : ColdReloadPath.NEW_PID_DEMO_PROCESS;

    SaveLatch save = armSaveConfirmation(1);
    device.pressHome();
    assertTrue("background save confirmation", save.await(30, TimeUnit.SECONDS));
    assertEquals(0, save.result.get());
    disarmSaveConfirmation();

    if (pathUsed == ColdReloadPath.IN_PROCESS_RUNTIME_RESET) {
      InstrumentationRegistry.getInstrumentation().runOnMainSync(
          app::clearRuntimeForColdReloadTest);
      MinimalAppTestLaunch.bringAppToForeground(context, device, PROFILE, false);
    } else {
      MinimalAppTestLaunch.killDemoProcessAndWait(context, device, pidBefore, 45000);
      MinimalAppTestLaunch.launchExistingProfile(context, device, PROFILE, false);
      int pidAfter =
          MinimalAppTestLaunch.waitForDemoProcessPid(context, device, Process.myPid(), 15000);
      assertNotEquals("cold start must be a new demo PID", pidBefore, pidAfter);
    }

    UiObject2 apply = device.wait(Until.findObject(By.desc("Apply")), 15000);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    UiObject2 rowB = device.wait(Until.findObject(By.desc("Row B")), 15000);
    assertNotNull(apply);
    assertNotNull(rowA);
    assertNotNull(rowB);
    assertTrue(device.wait(Until.findObject(By.text("Applied")), 30000) != null);
    MinimalAppTestLaunch.assertColdLoadFromProfile(context, device, PROFILE, 30000);
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

  private static final class SaveLatch {
    final CountDownLatch countDown;
    final AtomicInteger result = new AtomicInteger(1);

    SaveLatch(int expectedSaveCallbacks) {
      countDown = new CountDownLatch(expectedSaveCallbacks);
    }

    boolean await(long timeout, TimeUnit unit) throws InterruptedException {
      return countDown.await(timeout, unit);
    }
  }
}
