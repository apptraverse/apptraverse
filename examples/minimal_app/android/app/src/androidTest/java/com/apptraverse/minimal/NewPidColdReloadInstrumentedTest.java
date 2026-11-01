package com.apptraverse.minimal;

import android.content.Context;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.UiObject2;
import androidx.test.uiautomator.Until;

import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;


import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

/**
 * Cold reload E2E with a new demo process PID ({@code :demo} ≠ instrumentation).
 * In-process {@code clearRuntimeForColdReloadTest} is out of scope for this class.
 */
@RunWith(AndroidJUnit4.class)
public final class NewPidColdReloadInstrumentedTest {

  private static final String TAG = "NewPidColdReload";
  private static final String PROFILE = "minimal_app_new_pid_cold_reload_e2e";

  private UiDevice device;
  private Context context;

  @Before
  public void setUp() {
    InstrumentedTestNative.ensureLoaded();
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    context = InstrumentationRegistry.getInstrumentation().getTargetContext();
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

  @Test
  public void backgroundSaveColdStartWithNewDemoPid() throws Exception {
    int instrumentationPid = Process.myPid();
    MinimalAppTestLaunch.launchFreshProfile(context, device, PROFILE, false);

    int pidBefore = -1;
    try {
      pidBefore = MinimalAppTestLaunch.waitForStrictDemoProcessPid(
          context, device, instrumentationPid, 30000);
    } catch (AssertionError notFound) {
      String diag = MinimalAppTestLaunch.demoProcessShellDiagnostics(device);
      if (notFound.getMessage() != null
          && notFound.getMessage().contains("co-located")) {
        Assume.assumeTrue("BLOCKED: " + notFound.getMessage() + " " + diag, false);
      }
      Assume.assumeTrue(
          "BLOCKED: :demo process not visible (" + notFound.getMessage() + ") " + diag,
          false);
    }
    Assume.assumeTrue(
        "BLOCKED: demo PID equals instrumentation; :demo process not in use",
        pidBefore != instrumentationPid);
    reportPidEvidence("pidBefore", pidBefore, instrumentationPid);

    UiObject2 rowB = device.wait(Until.findObject(By.desc("Row B")), 15000);
    UiObject2 apply = device.wait(Until.findObject(By.desc("Apply")), 15000);
    assertNotNull(rowB);
    assertNotNull(apply);
    rowB.click();
    waitForSelected(rowB, true, 10000);
    apply.click();
    assertTrue(device.wait(Until.findObject(By.text("Applied")), 15000) != null);
    apply = device.findObject(By.desc("Apply"));
    assertNotNull(apply);
    assertFalse(apply.isEnabled());

    device.pressHome();
    MinimalAppTestLaunch.waitForApplyCheckpointOnDisk(context, PROFILE, 60000);

    MinimalAppTestLaunch.killDemoProcessAndWait(context, device, pidBefore, 45000);
    assertFalse("demo process must be dead", MinimalAppTestLaunch.isDemoProcessAlive(context, pidBefore));

    MinimalAppTestLaunch.launchExistingProfile(context, device, PROFILE, false);
    int pidAfter = MinimalAppTestLaunch.waitForStrictDemoProcessPid(
        context, device, instrumentationPid, 20000);
    assertNotEquals("cold start must use a new demo PID", pidBefore, pidAfter);
    assertNotEquals("demo PID must differ from instrumentation", instrumentationPid, pidAfter);
    reportPidEvidence("pidAfter", pidAfter, instrumentationPid);

    MinimalAppTestLaunch.assertColdLoadFromProfile(context, device, PROFILE, 30000);

    apply = device.wait(Until.findObject(By.desc("Apply")), 15000);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    UiObject2 rowBAfter = device.wait(Until.findObject(By.desc("Row B")), 15000);
    assertNotNull(apply);
    assertNotNull(rowA);
    assertNotNull(rowBAfter);
    assertTrue(device.hasObject(By.text("Applied")));
    assertFalse("Apply disabled before new input", apply.isEnabled());
    assertFalse("Row A unselected before new input", rowA.isSelected());
    assertFalse("Row B unselected before new input", rowBAfter.isSelected());

    rowA.click();
    waitForSelected(rowA, true, 15000);
    assertTrue("title unchanged after post-reload input", device.hasObject(By.text("Applied")));
  }

  private static void reportPidEvidence(String label, int demoPid, int instrumentationPid) {
    Log.i(TAG, label + "=" + demoPid + " instrumentationPid=" + instrumentationPid);
    Bundle status = new Bundle();
    status.putInt(label, demoPid);
    status.putInt("instrumentationPid", instrumentationPid);
    InstrumentationRegistry.getInstrumentation().sendStatus(0, status);
  }

}
