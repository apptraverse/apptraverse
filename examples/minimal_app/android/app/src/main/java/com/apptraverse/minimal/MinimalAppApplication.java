package com.apptraverse.minimal;

import android.app.Application;
import android.util.Log;


public class MinimalAppApplication extends Application
    implements NativeUiBridge.Host {

  private static final String TAG = "AppTraverseMinimal";
  private static final String LIBRARY_NAME = "apptraverse_minimal_app";

  private final NativeUiBridge uiBridge = new NativeUiBridge();
  private long runtimeHandle;
  private boolean modelThreadStarted;
  private String activeStateSubdir = MinimalAppStateDirs.DEFAULT;

  @Override
  public void onCreate() {
    super.onCreate();
    System.loadLibrary(LIBRARY_NAME);
    uiBridge.attachHost(this);
  }

  NativeUiBridge uiBridge() {
    return uiBridge;
  }

  long runtimeHandle() {
    return runtimeHandle;
  }

  boolean hasNativeRuntime() {
    return runtimeHandle != 0L;
  }

  synchronized void ensureNativeRuntime(String stateSubdir) {
    String subdir = stateSubdir != null ? stateSubdir : MinimalAppStateDirs.DEFAULT;
    if (runtimeHandle != 0L) {
      if (subdir.equals(activeStateSubdir)) {
        return;
      }
      NativeRuntime.nativeDestroy(runtimeHandle);
      runtimeHandle = 0L;
      modelThreadStarted = false;
    }
    activeStateSubdir = subdir;
    runtimeHandle = NativeRuntime.nativeCreate(
        getFilesDir().getAbsolutePath(), subdir, uiBridge);
    modelThreadStarted = false;
    if (runtimeHandle == 0L) {
      Log.e(TAG, "Native runtime create failed");
    }
  }

  void startModelOnce() {
    if (runtimeHandle == 0L || modelThreadStarted) {
      return;
    }
    modelThreadStarted = true;
    NativeRuntime.nativeStartModel(runtimeHandle);
  }

  void requestBackgroundSave() {
    if (runtimeHandle == 0L) {
      return;
    }
    NativeRuntime.nativeRequestBackgroundSave(runtimeHandle);
  }

  @Override
  public void consumePublication(int kind) {
    if (runtimeHandle == 0L) {
      return;
    }
    NativeRuntime.nativeConsumePublication(runtimeHandle, kind);
  }

  @Override
  public void onBackgroundSaveFinished(int persistResult) {
    if (persistResult != 0) throw new IllegalStateException("background USER save failed: " + persistResult);
  }
}
