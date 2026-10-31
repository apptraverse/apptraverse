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

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

/** P3.T3.A2: list selection + Apply through real Android input (presenter path). */
@RunWith(AndroidJUnit4.class)
public final class ListApplyInputInstrumentedTest {

  private UiDevice device;

  @Before
  public void setUp() {
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    device.pressHome();
    context.startActivity(context.getPackageManager()
        .getLaunchIntentForPackage("com.apptraverse.minimal")
        .addFlags(android.content.Intent.FLAG_ACTIVITY_CLEAR_TASK));
    assertTrue(device.wait(Until.hasObject(By.pkg("com.apptraverse.minimal").depth(0)),
        15000));
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
  public void rowBSelectionThenApplyUpdatesTitleAndMirror() throws InterruptedException {
    UiObject2 apply = device.wait(Until.findObject(By.desc("Apply")), 15000);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    UiObject2 rowB = device.wait(Until.findObject(By.desc("Row B")), 15000);

    assertNotNull(apply);
    assertNotNull(rowA);
    assertNotNull(rowB);
    assertTrue(apply.isEnabled());
    assertFalse(rowA.isSelected());
    assertFalse(rowB.isSelected());
    assertTrue(device.wait(Until.findObject(By.text("Minimal App")), 15000) != null);

    rowB.click();
    waitForSelected(rowB, true, 10000);
    assertFalse(rowA.isSelected());
    assertTrue(apply.isEnabled());

    apply.click();
    assertTrue(device.wait(Until.findObject(By.text("Applied")), 15000) != null);
    apply = device.findObject(By.desc("Apply"));
    assertNotNull(apply);
    assertFalse(apply.isEnabled());
    rowA = device.findObject(By.desc("Row A"));
    rowB = device.findObject(By.desc("Row B"));
    assertNotNull(rowA);
    assertNotNull(rowB);
    assertFalse(rowA.isSelected());
    assertFalse(rowB.isSelected());
  }
}
