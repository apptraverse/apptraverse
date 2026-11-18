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
    home();
    killDemoProcessOnly();
    stateSubdir = "acceptance-" + System.nanoTime();
    stateDir = new File(targetContext().getFilesDir(), stateSubdir);
  }

  @After public void tearDown() throws Exception {
    device.setOrientationNatural();
    device.unfreezeRotation();
    home();
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

  private void tap(String text) { waitText(text).click(); device.waitForIdle(); }
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
    waitChild();
    tap("Close");
    waitCatalog();
    tap("Open");
    waitChild();
    swipe(1);
    waitCatalog();
    tap("Window 1");
    waitChild();
    swipe(1);
    waitCatalog();
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
    home();
    long deadline = SystemClock.uptimeMillis() + 30_000;
    while (userFiles().isEmpty() && SystemClock.uptimeMillis() < deadline) {
      SystemClock.sleep(25);
    }
    assertEquals("One USER file at background lifecycle boundary", 1, userFiles().size());
    File firstFile = userFiles().get(0);
    assertTrue("LOCAL uses native object storage at the same lifecycle boundary",
        new File(firstFile.getParentFile(), "local/user.bin").length() > 0);
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
    languageMenu(false);
    tap("Русский");
    waitText("Каталог окон");
    waitText("Добавить");
    waitText("Окно 1");
    swipe(-1);
    waitText("Окно 1");
    waitText("Закрыть");
    swipe(1);
    waitText("Каталог окон");
    languageMenu(true);
    tap("English");
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
    waitForCatalogInputFocus();
    swipe(1);
    waitCatalog();
    waitText("Window 1");
  }

  private void keyEvent(int code) {
    try { device.executeShellCommand("input keyevent " + code); }
    catch (Exception error) { throw new AssertionError(error); }
  }
  private void home() {
    keyEvent(3);
    assertTrue("HOME must leave the app before its demo process is killed",
        device.wait(Until.gone(By.pkg(PACKAGE)), 30_000));
    device.waitForIdle();
  }
  private void back() { keyEvent(4); }
  private boolean menuChecked(String text) {
    UiObject2 ancestor = waitText(text);
    for (int depth = 0; depth < 5 && ancestor != null; ++depth) {
      List<UiObject2> checks = ancestor.findObjects(By.checkable(true));
      if (checks.size() == 1) return checks.get(0).isChecked();
      if (checks.size() > 1) break; // Do not mistake another menu row for this one.
      ancestor = ancestor.getParent();
    }
    throw new AssertionError("Native menu row must expose its radio/checkbox: " + text);
  }
  private void appearanceMenu() {
    try { waitForCatalogInputFocus(); } catch (Exception error) { throw new AssertionError(error); }
    device.waitForIdle();
    keyEvent(82);
    tap("Theme");
    waitText("From system");
  }

  private void languageMenu(boolean russian) {
    try { waitForCatalogInputFocus(); } catch (Exception error) { throw new AssertionError(error); }
    device.waitForIdle();
    keyEvent(82);
    tap(russian ? "Язык" : "Language");
    waitText("English");
    waitText("Русский");
  }

  @Test public void appearanceCommandsPersistLocallyAndFollowFreshProfilePolicy() throws Exception {
    launch();
    waitCatalog();
    appearanceMenu();
    tap("Dark");
    appearanceMenu();
    assertTrue(menuChecked("Dark"));
    tap("Soft dark");
    appearanceMenu();
    assertTrue(menuChecked("Soft dark"));
    back();
    assertTrue("Appearance changes remain RAM-backed until lifecycle", userFiles().isEmpty());
    home();
    assertEquals(1, userFiles().size());
    assertTrue(new File(userFiles().get(0).getParentFile(), "local/user.bin").length() > 0);
    killDemoProcessOnly();
    launch();
    waitCatalog();
    appearanceMenu();
    boolean fresh = Boolean.parseBoolean(
        InstrumentationRegistry.getArguments().getString("debugFresh", "true"));
    assertTrue(menuChecked(fresh ? "From system" : "Soft dark"));
    assertEquals(!fresh, menuChecked("Soft dark"));
    tap("Light");
    appearanceMenu();
    assertTrue(menuChecked("Light"));
    tap("From system");
    appearanceMenu();
    assertTrue(menuChecked("From system"));
    back();
  }

  @Test public void nativeLanguageMenuPersistsLocallyAndRestoresPublishedChecks() throws Exception {
    launch();
    waitCatalog();
    assertFalse("No duplicate content language toggle", device.hasObject(By.text("RU")));
    languageMenu(false);
    assertTrue(menuChecked("English"));
    tap("Русский");
    waitText("Каталог окон");
    waitText("Добавить");
    languageMenu(true);
    assertTrue(menuChecked("Русский"));
    assertFalse(menuChecked("English"));
    back();
    keyEvent(82);
    tap("Тема");
    waitText("Из системы");
    waitText("Мягкая тёмная");
    back();
    assertTrue("Menu intent remains RAM-backed until lifecycle", userFiles().isEmpty());
    home();
    assertEquals(1, userFiles().size());
    assertTrue(new File(userFiles().get(0).getParentFile(), "local/user.bin").length() > 0);
    killDemoProcessOnly();
    launch();
    boolean fresh = Boolean.parseBoolean(
        InstrumentationRegistry.getArguments().getString("debugFresh", "true"));
    waitText(fresh ? "Window Catalog" : "Каталог окон");
    languageMenu(!fresh);
    assertTrue(menuChecked(fresh ? "English" : "Русский"));
    back();
    if (!fresh) {
      languageMenu(true);
      tap("English");
      waitCatalog();
    }
  }

  @Test public void darkSofteningPreservesNativeDisabledTextState() {
    InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
      Context context = targetContext();
      android.widget.FrameLayout parent = new android.widget.FrameLayout(context);
      android.widget.FrameLayout client =
          com.apptraverse.ui.android.AppTraverseAndroidUi.createClientMount(parent, 0);
      android.widget.Button enabled = com.apptraverse.ui.android.AppTraverseAndroidUi
          .createPushButton(client, 1, "Enabled", true, 0, 0, 100, 48, 0);
      android.widget.Button disabled = com.apptraverse.ui.android.AppTraverseAndroidUi
          .createPushButton(client, 2, "Disabled", false, 0, 48, 100, 48, 0);
      try {
        Context nativeDark = new android.view.ContextThemeWrapper(context,
            android.R.style.Theme_Material);
        android.content.res.TypedArray colors = nativeDark.obtainStyledAttributes(
            new int[] {android.R.attr.textColorPrimary});
        android.content.res.ColorStateList foreground = colors.getColorStateList(0);
        int nativeDisabled = foreground.getColorForState(
            new int[] {-android.R.attr.state_enabled}, foreground.getDefaultColor());
        colors.recycle();
        com.apptraverse.ui.android.AppTraverseAndroidUi.applyAppearance(client, 2, true);
        assertEquals("Disabled text retains the native dark theme state", nativeDisabled,
            disabled.getCurrentTextColor());
        assertTrue("Enabled and disabled native controls remain visually distinct",
            enabled.getCurrentTextColor() != disabled.getCurrentTextColor());
        assertFalse(disabled.isEnabled());
      } finally {
        com.apptraverse.ui.android.AppTraverseAndroidUi.releaseClientMount(client);
      }
    });
  }

  @Test public void liveSystemTextScalingRemeasuresNativeButtons() throws Exception {
    String original = device.executeShellCommand("settings get system font_scale").trim();
    try {
      device.executeShellCommand("settings put system font_scale 1.0");
      launch();
      waitCatalog();
      int initial = waitText("Add").getVisibleBounds().height();
      device.executeShellCommand("settings put system font_scale 2.0");
      long deadline = SystemClock.uptimeMillis() + 15_000;
      int scaled = initial;
      while (scaled <= initial && SystemClock.uptimeMillis() < deadline) {
        SystemClock.sleep(100);
        scaled = waitText("Add").getVisibleBounds().height();
      }
      assertTrue("Native SP text measurement must enlarge the shared layout: "
          + initial + " -> " + scaled, scaled > initial);
      assertTrue("OS observations must not save before onStop", userFiles().isEmpty());
    } finally {
      if (original.equals("null")) device.executeShellCommand("settings delete system font_scale");
      else device.executeShellCommand("settings put system font_scale " + original);
    }
  }

  private void waitForCatalogInputFocus() throws Exception { waitForInputFocus(PACKAGE); }

  private void waitForInputFocus(String packageName) throws Exception {
    // Accessibility can expose the new Activity before InputDispatcher focuses
    // its window. Injecting then queues DOWN until it is already a long press.
    long deadline = SystemClock.uptimeMillis() + 30_000;
    String focusedWindows = "";
    do {
      String input = device.executeShellCommand("dumpsys input");
      int start = input.indexOf("  FocusedWindows:");
      int end = input.indexOf("  FocusRequests:", start);
      focusedWindows = start >= 0 && end > start ? input.substring(start, end) : "<unavailable>";
      if (focusedWindows.contains(packageName + "/")) return;
      SystemClock.sleep(25);
    } while (SystemClock.uptimeMillis() < deadline);
    assertTrue("Window did not acquire input focus for " + packageName + ": " + focusedWindows, false);
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
    languageMenu(false);
    tap("Русский");
    waitText("Каталог окон");
    swipe(-1);
    waitText("Окно 1");
    waitText("Закрыть");
    String oldPid = device.executeShellCommand("pidof " + PACKAGE + ":demo").trim();
    assertFalse(oldPid.isEmpty());
    assertTrue(userFiles().isEmpty());

    String taskId = currentCatalogTaskId();
    // Repeated acceptance runs may retain older cards with the same app label.
    // Remove only those stale own-app tasks; the current task is dismissed below
    // by the real native Recents gesture and is never removed through this API.
    android.app.ActivityManager manager = (android.app.ActivityManager)
        targetContext().getSystemService(Context.ACTIVITY_SERVICE);
    for (android.app.ActivityManager.AppTask task : manager.getAppTasks()) {
      if (task.getTaskInfo().taskId != Integer.parseInt(taskId)) task.finishAndRemoveTask();
    }
    if (useBack) back();
    else home();
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
    assertTrue("Current task must remain in Recents before dismissal", hasRecentTask(taskId));
    // Native Pixel/API34 Recents dismissal, not removal of the current task by API.
    keyEvent(187);
    device.waitForIdle();
    String appLabel = targetContext().getApplicationInfo()
        .loadLabel(targetContext().getPackageManager()).toString();
    Rect bounds = settledRecentCardBounds(launcher, appLabel);
    waitForInputFocus(launcher);
    device.dumpWindowHierarchy(new File(targetContext().getCacheDir(), "recents-before-swipe.xml"));
    device.takeScreenshot(new File(targetContext().getCacheDir(), "recents-before-swipe.png"));
    UiObject2 taskSnapshot = device.findObject(By.desc(appLabel))
        .findObject(By.res(launcher, "snapshot"));
    assertNotNull(taskSnapshot);
    if (!dismissCurrentCardThroughAccessibility(appLabel)) {
      device.executeShellCommand("input swipe " + bounds.centerX() + " " + bounds.centerY()
          + " " + bounds.centerX() + " 0 220");
    }
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

  private boolean dismissCurrentCardThroughAccessibility(String appLabel) {
    android.view.accessibility.AccessibilityNodeInfo root =
        InstrumentationRegistry.getInstrumentation().getUiAutomation().getRootInActiveWindow();
    return dismissCardNode(root, appLabel);
  }
  private boolean dismissCardNode(android.view.accessibility.AccessibilityNodeInfo node,
                                  String appLabel) {
    if (node == null) return false;
    if (appLabel.contentEquals(String.valueOf(node.getContentDescription()))) {
      for (android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction action : node.getActionList()) {
        String label = String.valueOf(action.getLabel());
        if (action.getId() == android.view.accessibility.AccessibilityNodeInfo.ACTION_DISMISS
            || label.equalsIgnoreCase("Close app")) {
          System.out.println("NATIVE_RECENTS_DISMISS_ACTION=" + action.getId() + " label=" + label);
          return node.performAction(action.getId());
        }
      }
    }
    for (int i = 0; i < node.getChildCount(); ++i) {
      if (dismissCardNode(node.getChild(i), appLabel)) return true;
    }
    return false;
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

  private String currentCatalogTaskId() throws Exception {
    String activities = device.executeShellCommand("dumpsys activity activities");
    Matcher match = Pattern.compile("topResumedActivity=ActivityRecord\\{[^\\n]* "
        + Pattern.quote(PACKAGE) + "/[^ ]+ t(\\d+)").matcher(activities);
    assertTrue("Foreground catalog task not found: " + activities, match.find());
    return match.group(1);
  }

  private boolean hasRecentTask(String taskId) throws Exception {
    return Pattern.compile("(?m)^\\s*\\* Recent #\\d+: Task\\{[^\\n]* #"
        + Pattern.quote(taskId) + " ").matcher(
            device.executeShellCommand("dumpsys activity recents")).find();
  }

  private void assertChildCloseUsesViewport() {
    // The common UI-runtime solver arranges native views after viewport measurement.
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
      if (entry.isDirectory()) {
        if (!entry.getName().equals("local")) collectUserFiles(entry, result);
      }
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
    try { waitForCatalogInputFocus(); } catch (Exception error) { throw new AssertionError(error); }
    device.waitForIdle();
    try {
      // Shell injection retains event timing under this AVD's instrumentation
      // load; UiDevice.swipe can deliver a queued DOWN as a long press.
      device.executeShellCommand("input swipe " + start + " " + y + " " + end + " " + y + " 180");
    } catch (Exception error) { throw new AssertionError(error); }
    device.waitForIdle();
  }
}
