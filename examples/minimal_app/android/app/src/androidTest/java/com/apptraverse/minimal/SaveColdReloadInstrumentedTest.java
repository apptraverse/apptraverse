package com.apptraverse.minimal;

import android.content.Context;

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
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

/** P3.T3.A: onStop checkpoint save, cold reload, and post-reload UI input. */
@RunWith(AndroidJUnit4.class)
public final class SaveColdReloadInstrumentedTest {

  private static final String PROFILE = "minimal_app_save_cold_reload_e2e";
  private static final String PKG = "com.apptraverse.minimal";

  private UiDevice device;
  private Context context;
  private MinimalAppApplication app;

  @Before
  public void setUp() {
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    app = (MinimalAppApplication) context.getApplicationContext();
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

  @Test
  public void backgroundSaveSurvivesColdReloadAndAcceptsInput()
      throws Exception {
    UiObject2 apply = device.wait(Until.findObject(By.desc("Apply")), 15000);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    UiObject2 rowB = device.wait(Until.findObject(By.desc("Row B")), 15000);
    assertNotNull(apply);
    assertNotNull(rowA);
    assertNotNull(rowB);

    rowB.click();
    waitForSelected(rowB, true, 10000);
    apply.click();
    assertTrue(device.wait(Until.findObject(By.text("Applied")), 15000) != null);
    apply = device.findObject(By.desc("Apply"));
    assertNotNull(apply);
    assertFalse(apply.isEnabled());

    app.clearBackgroundSaveLatch();
    app.setBackgroundSaveObserver(null);
    CountDownLatch saveLatch = new CountDownLatch(1);
    AtomicInteger saveResult = new AtomicInteger(1);
    app.setBackgroundSaveObserver(saveResult::set);
    app.setBackgroundSaveLatch(saveLatch);

    Thread.sleep(500);
    device.pressHome();

    assertTrue("background save confirmation", saveLatch.await(30, TimeUnit.SECONDS));
    assertEquals(0, saveResult.get());

    app.clearBackgroundSaveLatch();
    app.setBackgroundSaveObserver(null);

    // Kill only the demo app process; force-stop also tears down instrumentation.
    device.executeShellCommand("am kill " + PKG);
    Thread.sleep(2000);
    app = (MinimalAppApplication) context.getApplicationContext();

    MinimalAppTestLaunch.launchExistingProfile(context, device, PROFILE, false);

    apply = device.wait(Until.findObject(By.desc("Apply")), 15000);
    rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    rowB = device.wait(Until.findObject(By.desc("Row B")), 15000);
    assertNotNull(apply);
    assertNotNull(rowA);
    assertNotNull(rowB);
    assertTrue(device.wait(Until.findObject(By.text("Applied")), 15000) != null);
    apply = device.findObject(By.desc("Apply"));
    assertNotNull(apply);
    assertFalse(apply.isEnabled());
    rowA = device.findObject(By.desc("Row A"));
    rowB = device.findObject(By.desc("Row B"));
    assertNotNull(rowA);
    assertNotNull(rowB);
    rowB.click();
    waitForSelected(rowB, true, 15000);
    assertTrue(device.hasObject(By.text("Applied")));
  }
}
