package com.apptraverse.sharedchat;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.FrameLayout;
import java.lang.ref.WeakReference;

public final class MainActivity extends Activity {
  static {
    System.loadLibrary("shared_chat_android");
  }
  private static final Handler UI = new Handler(Looper.getMainLooper());
  private static WeakReference<MainActivity> current = new WeakReference<>(null);
  private static boolean queued;
  private FrameLayout surface;
  private boolean menuReady;
  static native void registerPushTokenNative(String token);
  static void registerPushToken(String token) {
    UI.post(() -> {
      if (current.get() != null)
        registerPushTokenNative(token);
    });
  }
  private native void startNative(String path, String name, boolean host, String invite,
      boolean loadOnly, String pushServiceUid);
  private native void renderNative(boolean remount);
  private native boolean checkpointNative();
  private native void stopNative();

  public static void enqueuePublication() {
    UI.post(() -> {
      if (queued)
        return;
      queued = true;
      UI.post(() -> {
        queued = false;
        MainActivity activity = current.get();
        if (activity != null) {
          activity.renderNative(false);
          if (!activity.menuReady) {
            activity.menuReady = true;
            activity.invalidateOptionsMenu();
          }
        }
      });
    });
  }
  @Override
  public void onCreate(Bundle saved) {
    super.onCreate(saved);
    surface = new FrameLayout(this);
    setContentView(surface);
    current = new WeakReference<>(this);
    String profile = getIntent().getStringExtra("profile");
    if (profile == null)
      profile = "default";
    // Profiles are names inside private app storage, never arbitrary external paths.
    if (!profile.matches("[A-Za-z0-9_-]+"))
      throw new IllegalArgumentException("Invalid profile name");
    String name = getIntent().getStringExtra("name");
    if (name == null)
      name = "Android";
    String invite = getIntent().getStringExtra("invite");
    if (invite == null)
      invite = "";
    startNative(new java.io.File(getFilesDir(), profile).getAbsolutePath(), name,
        getIntent().getBooleanExtra("host", true), invite,
        getIntent().getBooleanExtra("loadOnly", false), BuildConfig.PUSH_SERVICE_UID);
    PushSetup.start(this);
    renderNative(true);
  }
  public android.view.ViewGroup contentRoot() {
    return surface;
  }
  public void copyUid(String uid) {
    ((ClipboardManager) getSystemService(CLIPBOARD_SERVICE))
        .setPrimaryClip(ClipData.newPlainText("SharedNode ID", uid));
  }
  @Override
  public boolean onCreateOptionsMenu(android.view.Menu menu) {
    com.apptraverse.ui.android.AppTraverseAndroidUi.populateAppearanceMenu(surface, menu);
    return true;
  }
  @Override
  public boolean onPrepareOptionsMenu(android.view.Menu menu) {
    menu.clear();
    com.apptraverse.ui.android.AppTraverseAndroidUi.populateAppearanceMenu(surface, menu);
    return true;
  }
  @Override
  protected void onResume() {
    super.onResume();
    PushSetup.resume(this);
  }
  @Override
  protected void onStop() {
    if (!checkpointNative())
      throw new IllegalStateException("Could not save device preferences");
    super.onStop();
  }
  @Override
  protected void onDestroy() {
    if (current.get() == this) {
      current.clear();
      if (isFinishing())
        stopNative();
    }
    super.onDestroy();
  }
}
