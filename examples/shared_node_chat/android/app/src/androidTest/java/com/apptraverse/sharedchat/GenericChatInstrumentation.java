package com.apptraverse.sharedchat;

import android.app.Activity;
import android.app.Application;
import android.app.Instrumentation;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.widget.EditText;
import android.widget.TextView;
import java.util.concurrent.Callable;

/** Explicit real-cloud native widget/lifecycle acceptance; only own profiles. */
public final class GenericChatInstrumentation extends Instrumentation {
  private Bundle arguments;
  private Activity activity;
  private final LifecycleProbe lifecycle = new LifecycleProbe();
  private static final class LifecycleProbe implements Application.ActivityLifecycleCallbacks {
    volatile Activity created;
    public void onActivityCreated(Activity a, Bundle state) {
      if (a instanceof MainActivity)
        created = a;
    }
    public void onActivityStarted(Activity a) {}
    public void onActivityResumed(Activity a) {}
    public void onActivityPaused(Activity a) {}
    public void onActivityStopped(Activity a) {}
    public void onActivitySaveInstanceState(Activity a, Bundle b) {}
    public void onActivityDestroyed(Activity a) {}
  }
  private final String marker = "android-native \u041f\u0440\u0438\u0432\u0435\u0442 \ud83c\udf0d";
  @Override
  public void onCreate(Bundle args) {
    arguments = args;
    start();
  }
  @Override
  public void onStart() {
    Bundle result = new Bundle();
    Application application = (Application) getTargetContext().getApplicationContext();
    application.registerActivityLifecycleCallbacks(lifecycle);
    try {
      String profile = arguments.getString("profile", "native_acceptance");
      String mode = arguments.getString("mode", "full");
      activity = launch(profile, mode.startsWith("local_"));
      await(() -> value(108) != null && value(108).isEnabled(), "connected room");
      if ("local_prepare".equals(mode) || "local_verify".equals(mode)) {
        verifyLocalPreferences(profile, "local_prepare".equals(mode));
        result.putString("stream", "PASS Android native LOCAL " + mode + "\n");
        result.putInt("nativePid", android.os.Process.myPid());
        finish(Activity.RESULT_OK, result);
        return;
      }
      if ("long_text".equals(arguments.getString("mode"))) {
        verifyLongNativeInput();
        result.putString("stream",
            "PASS Android native IME commitText40 and fresh-timestamp40 keys, normal publications "
                + "and Enter\n");
        finish(Activity.RESULT_OK, result);
        return;
      }
      check(onUi(() -> currentActivity().getResources().getConfiguration().fontScale) == 1.0f,
          "normalized font-scale baseline");
      status("FONT_SCALE baseline=1.0 target=1.15");
      status("HOST_UID " + text(101));
      check("Message".contentEquals(value(107).getContentDescription()),
          "composer accessible name");
      check("Messages".contentEquals(value(106).getContentDescription()),
          "transcript accessible name");
      check(((EditText) value(106)).getKeyListener() == null, "transcript read-only");
      check(value(105) != null, "native participant list");
      setText("abcdef");
      onUi(() -> {
        EditText e = input();
        e.requestFocus();
        e.setSelection(3);
        return null;
      });
      copyAndCheck();
      await(() -> text(107).equals("abcdef"), "unrelated publication retains draft");
      check(onUi(() -> input().getSelectionStart()) == 3, "caret retained");
      sendStringSync("Z");
      await(() -> text(107).equals("abcZdef"), "native insertion at caret");
      setText(marker);
      onUi(() -> {
        input().onEditorAction(EditorInfo.IME_ACTION_DONE);
        return null;
      });
      await(() -> text(106).contains(marker), "Unicode outbound accepted");
      await(() -> text(107).isEmpty(), "Enter clears composer");
      check(occurrences(text(106), marker) == 1, "Enter once");
      status("WAIT_PEER_UNICODE");
      await(
          ()
              -> text(106).contains("peer-reply \u041f\u0440\u0438\u0432\u0435\u0442 \ud83c\udf0d"),
          "Unicode peer reply");
      setText("revision-before-remount");
      onUi(() -> {
        input().setSelection(7);
        return null;
      });
      status("REQUEST_ROTATION");
      await(()
                -> onUi(()
                            -> currentActivity().getResources().getConfiguration().orientation
                        == Configuration.ORIENTATION_LANDSCAPE),
          "native rotation");
      check(text(107).equals("revision-before-remount"), "draft survives rotation");
      setText("rotated-revision-before-remount");
      float oldSize = onUi(() -> input().getTextSize());
      Activity old = activity;
      status("REQUEST_FONT_SCALE");
      await(()
                -> onUi(() -> currentActivity().getResources().getConfiguration().fontScale) > 1.0f,
          "system font scale changed");
      Thread.sleep(2000);
      if (lifecycle.created == old)
        onUi(() -> {
          old.recreate();
          return null;
        });
      await(()
                -> lifecycle.created != null && lifecycle.created != old,
          "Activity recreation callback");
      Activity remounted = lifecycle.created;
      activity = remounted;
      status("REMOUNT candidate=" + System.identityHashCode(activity));
      await(() -> {
        if (lifecycle.created != activity) {
          activity = lifecycle.created;
          status("REMOUNT latest=" + System.identityHashCode(activity));
        }
        return value(107) != null;
      }, "remounted native composer");
      await(() -> text(107).equals("rotated-revision-before-remount"), "draft survives remount");
      check(onUi(() -> input().getTextSize()) > oldSize, "native SP font scale");
      setText("after-remount-send");
      click(108);
      await(()
                -> text(106).contains("after-remount-send"),
          "native edit/send after nonzero revision remount");
      await(() -> text(107).isEmpty(), "button clears composer");
      check(onUi(() -> input().hasFocus()), "Send returns composer focus");
      setText("replacement-draft");
      await(() -> text(107).equals("replacement-draft"), "new draft survives Send acknowledgement");
      // Finish and relaunch in this process, preserving SDK identity but no business RAM.
      Activity previousOwner = currentActivity();
      onUi(() -> {
        previousOwner.finish();
        return null;
      });
      activity = launch(profile, true);
      await(() -> value(102) != null && value(102).isEnabled(), "fresh session Copy enabled");
      check(text(106).isEmpty() && text(107).isEmpty(), "business reset after finish");
      // Exercise a delayed native teardown callback from the obsolete owner.
      onUi(() -> {
        java.lang.reflect.Method stop = MainActivity.class.getDeclaredMethod("stopNative");
        stop.setAccessible(true);
        stop.invoke(previousOwner);
        return null;
      });
      check(value(102) != null && value(102).isEnabled(), "stale owner teardown ignored");
      copyAndCheck();
      result.putString("stream",
          "PASS Android native Unicode peer exchange, a11y/read-only/list, caret, Enter once, "
              + "rotation/font-scale Activity remount/edit/Send/focus, same-process first Copy "
              + "reset\n");
      status("NATIVE_ACCEPTANCE_PASS");
      finish(Activity.RESULT_OK, result);
    } catch (Throwable failure) {
      result.putString("stream", "FAIL " + android.util.Log.getStackTraceString(failure));
      finish(Activity.RESULT_CANCELED, result);
    } finally {
      application.unregisterActivityLifecycleCallbacks(lifecycle);
    }
  }
  private boolean nativeDarkChecked() throws Exception {
    return onUi(() -> {
      android.widget.PopupMenu observation =
          new android.widget.PopupMenu(currentActivity(), input());
      currentActivity().onPrepareOptionsMenu(observation.getMenu());
      android.view.MenuItem dark = observation.getMenu().findItem(102);
      return dark != null && dark.isChecked();
    });
  }
  private void verifyLocalPreferences(String profile, boolean prepare) throws Exception {
    String uid = text(101);
    check(uid.matches("[0-9a-f-]{36}"), "nonempty SDK identity");
    if (prepare) {
      setText("local-business-message");
      onUi(() -> {
        input().onEditorAction(EditorInfo.IME_ACTION_DONE);
        return null;
      });
      await(()
                -> text(106).contains("local-business-message") && text(107).isEmpty(),
          "business before cold stop");
      setText("local-unsaved-draft");
      onUi(() -> {
        currentActivity().openOptionsMenu();
        return null;
      });
      boolean invoked = invokeMenuActionSync(currentActivity(), 102, 0);
      check(invoked, "real native Dark menu action");
      await(this::nativeDarkChecked, "published native Dark state");
      status("LOCAL_PREPARED_UID " + uid);
      Activity owner = currentActivity();
      onUi(() -> {
        owner.finish();
        return null;
      });
      await(() -> onUi(owner::isDestroyed), "onStop/final LOCAL completed");
    } else {
      check(uid.equals(arguments.getString("expectedUid")), "SDK identity cold restore");
      check(!Integer.toString(android.os.Process.myPid()).equals(arguments.getString("oldPid")),
          "new cold process");
      check(text(106).isEmpty() && text(107).isEmpty(), "business/draft absent after cold process");
      await(this::nativeDarkChecked, "native LOCAL Dark cold restore");
    }
    java.io.File root = new java.io.File(getTargetContext().getFilesDir(), profile);
    check(new java.io.File(root, "local/user.bin").isFile(), "ordinary native LOCAL file");
    check(!new java.io.File(root, "user.bin").exists()
            && !new java.io.File(root, "transport/user.bin").exists(),
        "no business files");
  }
  private void verifyLongNativeInput() throws Exception {
    String ime = "imeabcdefghijklmnopqrstuvwxyz0123456789A";
    String keys = "abcdefghijklmnopqrstuvwxyz0123456789ABCD";
    check(ime.length() == 40 && keys.length() == 40, "forty-character payloads");
    setText("");
    onUi(() -> {
      InputConnection connection = input().onCreateInputConnection(new EditorInfo());
      check(connection != null, "native input connection");
      check(connection.commitText(ime, 1), "native IME commit");
      return null;
    });
    await(() -> text(107).equals(ime), "native IME forty");
    onUi(() -> {
      input().onEditorAction(EditorInfo.IME_ACTION_DONE);
      return null;
    });
    await(() -> text(106).contains(ime) && text(107).isEmpty(), "IME forty model/send");
    check(occurrences(text(106), ime) == 1, "IME forty Enter once");
    status("IME40_PASS");
    setText("");
    // Instrumentation refreshes each event timestamp, without sleeps or pacing.
    sendStringSync(keys);
    await(() -> text(107).equals(keys), "fresh timestamp forty");
    check(onUi(() -> input().hasFocus()), "forty-character focus");
    sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER);
    await(() -> text(106).contains(keys) && text(107).isEmpty(), "key forty model/send");
    check(occurrences(text(106), keys) == 1, "key forty Enter once");
    status("FRESH_KEYS40_PASS");
  }
  private Activity launch(String profile, boolean load) {
    Intent intent = new Intent(getTargetContext(), MainActivity.class);
    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    intent.putExtra("profile", profile)
        .putExtra("name", "AndroidNative")
        .putExtra("host", true)
        .putExtra("invite", arguments.getString("hostUid", ""))
        .putExtra("loadOnly", load);
    return startActivitySync(intent);
  }
  private Activity currentActivity() {
    return lifecycle.created;
  }
  private View value(int id) throws Exception {
    return onUi(() -> currentActivity().getWindow().getDecorView().findViewWithTag(id));
  }
  private EditText input() {
    return currentActivity().getWindow().getDecorView().findViewWithTag(107);
  }
  private String text(int id) throws Exception {
    return onUi(()
                    -> ((TextView) currentActivity().getWindow().getDecorView().findViewWithTag(id))
                        .getText()
                        .toString());
  }
  private void setText(String text) throws Exception {
    onUi(() -> {
      input().setText(text);
      input().setSelection(text.length());
      input().requestFocus();
      return null;
    });
  }
  private void click(int id) throws Exception {
    onUi(() -> {
      View v = currentActivity().getWindow().getDecorView().findViewWithTag(id);
      check(v != null && v.isEnabled(), "click enabled " + id);
      v.performClick();
      return null;
    });
  }
  private void copyAndCheck() throws Exception {
    String uid = text(101);
    onUi(() -> {
      clipboard().setPrimaryClip(ClipData.newPlainText("test", "sentinel"));
      return null;
    });
    click(102);
    await(
        ()
            -> onUi(()
                        -> clipboard().hasPrimaryClip()
                    && clipboard().getPrimaryClip().getItemAt(0).getText().toString().equals(uid)),
        "native Copy clipboard");
  }
  private ClipboardManager clipboard() {
    return (ClipboardManager) currentActivity().getSystemService(Activity.CLIPBOARD_SERVICE);
  }
  private interface Condition {
    boolean get() throws Exception;
  }
  private void await(Condition condition, String what) throws Exception {
    long end = System.currentTimeMillis() + 120000;
    do {
      if (condition.get())
        return;
      Thread.sleep(200);
    } while (System.currentTimeMillis() < end);
    throw new AssertionError("Timeout: " + what);
  }
  private <T> T onUi(Callable<T> task) throws Exception {
    final Object[] result = new Object[1];
    final Throwable[] error = new Throwable[1];
    runOnMainSync(() -> {
      try {
        result[0] = task.call();
      } catch (Throwable t) {
        error[0] = t;
      }
    });
    if (error[0] != null)
      throw new RuntimeException(error[0]);
    return (T) result[0];
  }
  private void status(String text) {
    Bundle b = new Bundle();
    b.putString("stream", text + "\n");
    sendStatus(0, b);
  }
  private static void check(boolean condition, String what) {
    if (!condition)
      throw new AssertionError(what);
  }
  private static int occurrences(String value, String needle) {
    int n = 0, p = 0;
    while ((p = value.indexOf(needle, p)) >= 0) {
      ++n;
      p += needle.length();
    }
    return n;
  }
}
