package com.apptraverse.windowcatalog;

import android.app.Application;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

/** Owns the native runtime and forwards publication callbacks to the Activity. */
public final class WindowCatalogApplication extends Application
    implements NativeUiBridge.Listener {

  private static final String TAG = "AppTraverseWindowCatalog";

  private final Handler mainHandler = new Handler(Looper.getMainLooper());
  private final NativeUiBridge uiBridge = new NativeUiBridge();

  private long runtimeHandle;
  private boolean modelStarted;
  private MainActivity activity;

  @Override
  public void onCreate() {
    super.onCreate();
    uiBridge.attach(this);
  }

  /** One native runtime per process; {@code stateSubdir} is under {@code files/}. */
  public void initRuntime(String filesDir, String stateSubdir) {
    if (runtimeHandle != 0L) {
      return;
    }
    runtimeHandle = NativeRuntime.nativeCreate(filesDir, stateSubdir, uiBridge);
    if (runtimeHandle == 0L) {
      Log.e(TAG, "Native runtime create failed");
    }
  }

  public void setActivity(MainActivity mainActivity) {
    activity = mainActivity;
  }

  public void clearActivity(MainActivity mainActivity) {
    if (activity == mainActivity) {
      activity = null;
    }
  }

  public long runtimeHandle() {
    return runtimeHandle;
  }

  public void startModelOnce() {
    if (modelStarted || runtimeHandle == 0L || activity == null) {
      return;
    }
    modelStarted = true;
    NativeRuntime.nativeStartModel(runtimeHandle);
  }

  public void requestBackgroundSave() {
    if (runtimeHandle != 0L) {
      NativeRuntime.nativeRequestBackgroundSave(runtimeHandle);
    }
  }

  public void mobilePageShown(long windowObjId) {
    if (runtimeHandle != 0L) {
      NativeRuntime.nativeMobilePageShown(runtimeHandle, windowObjId);
    }
  }

  public void mobileSwipe(int delta) {
    if (runtimeHandle != 0L) {
      NativeRuntime.nativeMobileSwipe(runtimeHandle, delta);
    }
  }

  @Override
  public void onPublication(int kind) {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (runtimeHandle != 0L) {
          NativeRuntime.nativeConsumePublication(runtimeHandle, kind);
        }
      }
    });
  }

  @Override
  public void onMobilePages(long[] windowIds, long activeWindowId) {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (activity != null) {
          activity.onMobilePages(windowIds, activeWindowId);
        }
      }
    });
  }
}
