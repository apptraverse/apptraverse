package com.apptraverse.minimal;

import android.content.Context;
import android.os.Process;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.UiObject2;
import androidx.test.uiautomator.Until;

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

  private UiDevice device;
  private Context context;
  private MinimalAppApplication app;

  @Before
  public void setUp() {
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    app = (MinimalAppApplication) context.getApplicationContext();
    if (app.hasNativeRuntime()) {
      InstrumentationRegistry.getInstrumentation().runOnMainSync(
          app::clearRuntimeForColdReloadTest);
    }
    MinimalAppTestLaunch.launchFreshProfile(context, device, PROFILE, false);
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

  private SaveLatch armSaveConfirmation() {
    app.clearBackgroundSaveLatch();
    app.setBackgroundSaveObserver(null);
    SaveLatch latch = new SaveLatch();
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
    applyRowBAndWaitPublication();
    SaveLatch save = armSaveConfirmation();
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
    applyRowBAndWaitPublication();

    SaveLatch save = armSaveConfirmation();
    device.pressHome();
    MinimalAppTestLaunch.bringAppToForeground(context, device, PROFILE, false);

    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    assertNotNull(rowA);
    rowA.click();
    waitForSelected(rowA, true, 15000);

    assertTrue("background save confirmation", save.await(30, TimeUnit.SECONDS));
    assertEquals(0, save.result.get());
    disarmSaveConfirmation();
    assertTrue(device.hasObject(By.text("Applied")));
  }

  @Test
  public void backgroundSaveSurvivesColdReloadAndAcceptsInput() throws Exception {
    applyRowBAndWaitPublication();
    int pidBefore = MinimalAppTestLaunch.waitForDemoProcessPid(context, device, -1, 15000);
    boolean inProcessDemo = pidBefore == Process.myPid();

    SaveLatch save = armSaveConfirmation();
    device.pressHome();
    assertTrue("background save confirmation", save.await(30, TimeUnit.SECONDS));
    assertEquals(0, save.result.get());
    disarmSaveConfirmation();

    device.executeShellCommand("logcat -c");
    if (inProcessDemo) {
      InstrumentationRegistry.getInstrumentation().runOnMainSync(
          app::clearRuntimeForColdReloadTest);
      MinimalAppTestLaunch.bringAppToForeground(context, device, PROFILE, false);
    } else {
      MinimalAppTestLaunch.killDemoProcessAndWait(context, device, pidBefore, 45000);
      MinimalAppTestLaunch.launchExistingProfile(context, device, PROFILE, false);
      int pidAfter =
          MinimalAppTestLaunch.waitForDemoProcessPid(context, device, Process.myPid(), 15000);
      assertNotEquals("cold start must be a new process", pidBefore, pidAfter);
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
  }

  private static final class SaveLatch {
    final CountDownLatch countDown = new CountDownLatch(1);
    final AtomicInteger result = new AtomicInteger(1);

    boolean await(long timeout, TimeUnit unit) throws InterruptedException {
      return countDown.await(timeout, unit);
    }
  }
}
