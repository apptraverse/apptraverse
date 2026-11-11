package com.apptraverse.windowcatalog;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.Intent;
import android.graphics.Rect;
import android.os.SystemClock;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.UiObject2;
import androidx.test.uiautomator.Until;
import java.io.File;
import java.io.FileInputStream;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Real Activity/native presenter tests; all actions enter the shared model. */
@RunWith(AndroidJUnit4.class)
public final class WindowCatalogInstrumentedTest {
  private static final String PACKAGE = "com.apptraverse.windowcatalog";
  private UiDevice device;
  private String stateSubdir;
  private File stateDir;

  @Before public void setUp() throws Exception {
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    device.setOrientationNatural();
    device.pressHome();
    killDemoProcessOnly();
    stateSubdir = "acceptance-" + System.nanoTime();
    stateDir = new File(targetContext().getFilesDir(), stateSubdir);
  }

  @After public void tearDown() throws Exception {
    device.setOrientationNatural();
    device.unfreezeRotation();
    device.pressHome();
    killDemoProcessOnly();
  }

  private Context targetContext() {
    return InstrumentationRegistry.getInstrumentation().getTargetContext();
  }

  private void launch() {
    Intent intent = targetContext().getPackageManager().getLaunchIntentForPackage(PACKAGE);
    assertNotNull(intent);
    intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
    intent.putExtra(MainActivity.EXTRA_STATE_SUBDIR, stateSubdir);
    targetContext().startActivity(intent);
  }

  private UiObject2 waitText(String text) {
    UiObject2 found = device.wait(Until.findObject(By.text(Pattern.compile(
        Pattern.quote(text), Pattern.CASE_INSENSITIVE))), 30_000);
    assertNotNull("Missing UI text: " + text, found);
    return found;
  }

  private void tap(String text) { waitText(text).click(); }
  private void waitCatalog() { waitText("Window Catalog"); waitText("Add"); }
  private void waitChild() { waitText("Window 1"); waitText("Close"); }

  @Test public void catalogOnlySwipeIsNoOpAndDoesNotSave() {
    launch();
    waitCatalog();
    swipe(-1);
    waitCatalog();
    assertTrue(userFiles().isEmpty());
  }

  @Test public void addSwipeCloseReopenDeleteUsesModelAndDoesNotSave() {
    launch();
    waitCatalog();
    tap("Add");
    waitChild();
    swipe(1);
    waitCatalog();
    waitText("Window 1");
    swipe(-1);
    waitChild();
    tap("Close");
    waitCatalog();
    tap("Window 1 (Closed)");
    tap("Open");
    waitChild();
    swipe(1);
    waitCatalog();
    tap("Window 1");
    tap("Delete");
    assertTrue(device.wait(Until.gone(By.text("Window 1")), 30_000));
    assertTrue("Working, swiping and child close must stay in RAM", userFiles().isEmpty());
  }

  @Test public void backgroundWritesOneFileAndColdRestartHonorsBuildPolicy() throws Exception {
    launch();
    waitCatalog();
    tap("Add");
    waitChild();
    assertTrue(userFiles().isEmpty());
    device.pressHome();
    long deadline = SystemClock.uptimeMillis() + 30_000;
    while (userFiles().isEmpty() && SystemClock.uptimeMillis() < deadline) {
      SystemClock.sleep(25);
    }
    assertEquals("One USER file at background lifecycle boundary", 1, userFiles().size());
    File firstFile = userFiles().get(0);
    assertTrue(firstFile.length() > 0);
    byte[] firstBytes = readBytes(firstFile);
    long firstModified = firstFile.lastModified();
    killDemoProcessOnly();
    launch();
    boolean debugFresh = Boolean.parseBoolean(
        InstrumentationRegistry.getArguments().getString("debugFresh", "true"));
    if (debugFresh) {
      waitCatalog();
      assertFalse(device.hasObject(By.text("Window 1")));
      assertArrayEquals("Fresh run must not rewrite the old USER", firstBytes, readBytes(firstFile));
      assertEquals(firstModified, firstFile.lastModified());
    } else {
      waitChild();
      swipe(1);
      waitCatalog();
      waitText("Window 1");
    }
  }

  @Test public void localizationReachesCatalogAndExistingChild() {
    launch();
    waitCatalog();
    tap("Add");
    waitChild();
    swipe(1);
    waitCatalog();
    tap("RU");
    waitText("Каталог окон");
    waitText("Добавить");
    waitText("Окно 1");
    swipe(-1);
    waitText("Окно 1");
    waitText("Закрыть");
    swipe(1);
    waitText("Каталог окон");
    tap("EN");
    waitCatalog();
    swipe(-1);
    waitChild();
    assertTrue(userFiles().isEmpty());
  }

  @Test public void rotationAndActivityReplacementRetainLiveModel() throws Exception {
    launch();
    waitCatalog();
    tap("Add");
    waitChild();
    String originalPid = device.executeShellCommand("pidof " + PACKAGE + ":demo").trim();
    assertChildCloseUsesViewport();
    device.setOrientationLeft();
    waitChild();
    assertChildCloseUsesViewport();
    device.setOrientationNatural();
    waitChild();
    assertChildCloseUsesViewport();
    // CLEAR_TASK replaces the Activity while the Application/model stay alive.
    String result = device.executeShellCommand("am start -W -f 0x10008000"
        + " -n " + PACKAGE + "/.MainActivity --es stateSubdir " + stateSubdir);
    assertTrue(result, result.contains("Status: ok"));
    waitChild();
    assertTrue(device.wait(Until.gone(By.text("Loading")), 10_000));
    assertEquals(originalPid, device.executeShellCommand("pidof " + PACKAGE + ":demo").trim());
    swipe(1);
    waitCatalog();
    waitText("Window 1");
  }

  private void assertChildCloseUsesViewport() {
    // Native views apply pixels published by the model after the viewport event.
    Rect bounds = waitText("Close").getVisibleBounds();
    long deadline = SystemClock.uptimeMillis() + 10_000;
    while ((bounds.width() < device.getDisplayWidth() * 4 / 5
            || bounds.centerY() < device.getDisplayHeight() * 3 / 4)
           && SystemClock.uptimeMillis() < deadline) {
      SystemClock.sleep(25);
      bounds = waitText("Close").getVisibleBounds();
    }
    assertTrue("Close must use client width: " + bounds,
        bounds.width() >= device.getDisplayWidth() * 4 / 5);
    assertTrue("Close must be at client bottom: " + bounds,
        bounds.centerY() >= device.getDisplayHeight() * 3 / 4);
  }

  private static byte[] readBytes(File file) throws Exception {
    try (FileInputStream input = new FileInputStream(file);
         ByteArrayOutputStream output = new ByteArrayOutputStream()) {
      byte[] block = new byte[4096];
      int read;
      while ((read = input.read(block)) != -1) output.write(block, 0, read);
      return output.toByteArray();
    }
  }

  private List<File> userFiles() {
    List<File> result = new ArrayList<>();
    collectUserFiles(stateDir, result);
    return result;
  }

  private static void collectUserFiles(File dir, List<File> result) {
    File[] entries = dir.listFiles();
    if (entries == null) return;
    for (File entry : entries) {
      if (entry.isDirectory()) collectUserFiles(entry, result);
      else {
        assertEquals("Unexpected per-object or checkpoint file", "user.bin", entry.getName());
        result.add(entry);
      }
    }
  }

  private void killDemoProcessOnly() throws Exception {
    String pids = device.executeShellCommand("pidof " + PACKAGE + ":demo").trim();
    if (pids.isEmpty()) return;
    for (String pid : pids.split("\\s+")) {
      device.executeShellCommand("run-as " + PACKAGE + " kill " + pid);
    }
    long deadline = SystemClock.uptimeMillis() + 10_000;
    while (!device.executeShellCommand("pidof " + PACKAGE + ":demo").trim().isEmpty()
        && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(25);
    assertTrue("Old demo process survived", device.executeShellCommand(
        "pidof " + PACKAGE + ":demo").trim().isEmpty());
  }

  private void swipe(int direction) {
    int width = device.getDisplayWidth();
    int y = device.getDisplayHeight() / 2;
    int start = direction < 0 ? width * 3 / 4 : width / 4;
    int end = direction < 0 ? width / 4 : width * 3 / 4;
    assertTrue(device.swipe(start, y, end, y, 12));
    device.waitForIdle();
  }
}
