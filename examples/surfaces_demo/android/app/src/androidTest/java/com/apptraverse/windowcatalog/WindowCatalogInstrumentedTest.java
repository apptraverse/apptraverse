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
import androidx.test.uiautomator.Direction;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.UiObject2;
import androidx.test.uiautomator.Until;
import java.io.File;
import java.io.FileInputStream;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
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

  @Test public void backThenRecentTaskDismissColdRestartHonorsBuildPolicy() throws Exception {
    closeTaskAndRelaunch(true);
  }

  @Test public void homeThenRecentTaskDismissColdRestartHonorsBuildPolicy() throws Exception {
    closeTaskAndRelaunch(false);
  }

  private void closeTaskAndRelaunch(boolean useBack) throws Exception {
    launch();
    waitCatalog();
    tap("Add");
    waitChild();
    swipe(1);
    waitCatalog();
    tap("RU");
    waitText("Каталог окон");
    swipe(-1);
    waitText("Окно 1");
    waitText("Закрыть");
    String oldPid = device.executeShellCommand("pidof " + PACKAGE + ":demo").trim();
    assertFalse(oldPid.isEmpty());
    assertTrue(userFiles().isEmpty());

    if (useBack) assertTrue(device.pressBack());
    else assertTrue(device.pressHome());
    long deadline = SystemClock.uptimeMillis() + 30_000;
    while (userFiles().isEmpty() && SystemClock.uptimeMillis() < deadline) {
      SystemClock.sleep(25);
    }
    assertEquals(1, userFiles().size());
    File savedFile = userFiles().get(0);
    assertTrue(savedFile.length() > 0);
    byte[] savedBytes = readBytes(savedFile);
    long savedModified = savedFile.lastModified();

    // Let the launcher finish the Back/Home transition before opening Recents.
    // Resolve through the shell: the instrumented app's package-visibility
    // filter can hide HOME from UiDevice.getLauncherPackageName().
    String resolvedHome = device.executeShellCommand("cmd package resolve-activity --brief"
        + " -a android.intent.action.MAIN -c android.intent.category.HOME").trim();
    String component = resolvedHome.substring(resolvedHome.lastIndexOf('\n') + 1);
    assertTrue(resolvedHome, component.contains("/"));
    String launcher = component.substring(0, component.indexOf('/'));
    assertTrue("Expected home from " + launcher + ", current=" + device.getCurrentPackageName(),
        device.wait(Until.hasObject(By.pkg(launcher)), 10_000));
    device.waitForIdle();
    String taskId = recentCatalogTaskId();
    // Real Pixel/API34 Recents gesture, not force-stop or an explicit process kill.
    assertTrue(device.pressRecentApps());
    device.waitForIdle();
    String appLabel = targetContext().getApplicationInfo()
        .loadLabel(targetContext().getPackageManager()).toString();
    Rect bounds = settledRecentCardBounds(launcher, appLabel);
    device.dumpWindowHierarchy(new File(targetContext().getCacheDir(), "recents-before-swipe.xml"));
    device.takeScreenshot(new File(targetContext().getCacheDir(), "recents-before-swipe.png"));
    UiObject2 taskSnapshot = device.findObject(By.desc(appLabel))
        .findObject(By.res(launcher, "snapshot"));
    assertNotNull(taskSnapshot);
    taskSnapshot.swipe(Direction.UP, 1.0f);
    deadline = SystemClock.uptimeMillis() + 10_000;
    while (hasRecentTask(taskId) && SystemClock.uptimeMillis() < deadline) {
      SystemClock.sleep(25);
    }
    if (hasRecentTask(taskId)) {
      device.dumpWindowHierarchy(new File(targetContext().getCacheDir(), "recents-after-swipe.xml"));
      device.takeScreenshot(new File(targetContext().getCacheDir(), "recents-after-swipe.png"));
    }
    assertFalse("Catalog task " + taskId + " survived the Recents swipe from " + bounds
        + ": " + device.executeShellCommand("dumpsys activity recents"), hasRecentTask(taskId));
    // Task removal and process lifetime are different Android contracts. The OS
    // may cache a taskless process; terminate only that retained demo process to
    // additionally prove disk restoration, after verifying the actual UI close.
    killDemoProcessOnly();

    launch();
    boolean debugFresh = Boolean.parseBoolean(
        InstrumentationRegistry.getArguments().getString("debugFresh", "true"));
    if (debugFresh) {
      waitCatalog();
      assertFalse(device.hasObject(By.text("Window 1")));
      assertArrayEquals(savedBytes, readBytes(savedFile));
      assertEquals(savedModified, savedFile.lastModified());
    } else {
      waitText("Окно 1");
      waitText("Закрыть");
      swipe(1);
      waitText("Каталог окон");
      waitText("Добавить");
      waitText("Окно 1");
    }
    String newPid = device.executeShellCommand("pidof " + PACKAGE + ":demo").trim();
    assertFalse(newPid.isEmpty());
    assertFalse("The restored graph must be loaded in a new process", oldPid.equals(newPid));
  }

  private Rect settledRecentCardBounds(String launcher, String label) {
    long deadline = SystemClock.uptimeMillis() + 10_000;
    Rect previous = null;
    int stable = 0;
    while (SystemClock.uptimeMillis() < deadline) {
      UiObject2 card = device.findObject(By.desc(label));
      UiObject2 snapshot = card == null ? null : card.findObject(By.res(launcher, "snapshot"));
      if (snapshot != null) {
        Rect bounds = snapshot.getVisibleBounds();
        if (!bounds.isEmpty() && bounds.equals(previous)) ++stable;
        else stable = 0;
        previous = bounds;
        if (stable >= 3) return bounds;
      }
      SystemClock.sleep(100);
    }
    throw new AssertionError("Catalog Recents card did not settle: " + label + " " + previous);
  }

  private String recentCatalogTaskId() throws Exception {
    String recent = device.executeShellCommand("dumpsys activity recents");
    Matcher match = Pattern.compile("(?m)^\\s*\\* Recent #\\d+: Task\\{[^\\n]* #(\\d+) "
        + "[^\\n]*A=\\d+:" + Pattern.quote(PACKAGE) + "\\}").matcher(recent);
    assertTrue("Catalog task not found before dismissal: " + recent, match.find());
    return match.group(1);
  }

  private boolean hasRecentTask(String taskId) throws Exception {
    return Pattern.compile("(?m)^\\s*\\* Recent #\\d+: Task\\{[^\\n]* #"
        + Pattern.quote(taskId) + " ").matcher(
            device.executeShellCommand("dumpsys activity recents")).find();
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
