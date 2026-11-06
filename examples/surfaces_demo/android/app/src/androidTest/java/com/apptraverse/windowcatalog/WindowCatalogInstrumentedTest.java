package com.apptraverse.windowcatalog;

import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.Intent;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.UiObject2;
import androidx.test.uiautomator.Until;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class WindowCatalogInstrumentedTest {

  private UiDevice device;

  @Before
  public void setUp() {
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
  }

  private void launchFresh() throws Exception {
    Context context = ApplicationProvider.getApplicationContext();
    context.deleteFile("window_catalog_state");
    java.io.File stateDir =
        new java.io.File(context.getFilesDir(), "window_catalog_state");
    deleteRecursive(stateDir);
    Intent intent = context.getPackageManager()
        .getLaunchIntentForPackage("com.apptraverse.windowcatalog");
    intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
    context.startActivity(intent);
    assertTrue(device.wait(Until.hasObject(By.textContains("/")), 30_000));
  }

  private static void deleteRecursive(java.io.File file) {
    if (file.isDirectory()) {
      java.io.File[] children = file.listFiles();
      if (children != null) {
        for (java.io.File child : children) {
          deleteRecursive(child);
        }
      }
    }
    file.delete();
  }

  @Test
  public void scenario1_catalogOnlySwipeIsNoOp() throws Exception {
    launchFresh();
    UiObject2 indicator = device.findObject(By.text("1 / 1"));
    assertTrue(indicator != null);
    swipeLeft();
    assertTrue(device.wait(Until.hasObject(By.text("1 / 1")), 5_000));
  }

  @Test
  public void scenario2_addOpensChildInRing() throws Exception {
    launchFresh();
    tapNativeButton("Add");
    assertTrue(device.wait(Until.hasObject(By.text("2 / 2")), 30_000));
    swipeLeft();
    assertTrue(device.wait(Until.hasObject(By.text("2 / 2")), 5_000));
    swipeRight();
    assertTrue(device.wait(Until.hasObject(By.text("1 / 2")), 5_000));
  }

  @Test
  public void scenario3_coldRestartKeepsActiveChild() throws Exception {
    launchFresh();
    tapNativeButton("Add");
    assertTrue(device.wait(Until.hasObject(By.text("2 / 2")), 30_000));
    device.pressHome();
    Thread.sleep(500);
    device.executeShellCommand(
        "am force-stop com.apptraverse.windowcatalog");
    relaunchKeepingState();
    assertTrue(device.wait(Until.hasObject(By.text("2 / 2")), 30_000));
  }

  private void relaunchKeepingState() {
    Context context = ApplicationProvider.getApplicationContext();
    Intent intent = context.getPackageManager()
        .getLaunchIntentForPackage("com.apptraverse.windowcatalog");
    intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
    context.startActivity(intent);
  }

  private void tapNativeButton(String label) throws Exception {
    UiObject2 button = device.wait(Until.findObject(By.text(label)), 30_000);
    assertTrue(button != null);
    button.click();
  }

  private void swipeLeft() throws Exception {
    int width = device.getDisplayWidth();
    int height = device.getDisplayHeight();
    device.swipe(width * 3 / 4, height / 2, width / 4, height / 2, 20);
  }

  private void swipeRight() throws Exception {
    int width = device.getDisplayWidth();
    int height = device.getDisplayHeight();
    device.swipe(width / 4, height / 2, width * 3 / 4, height / 2, 20);
  }
}
