package com.apptraverse.minimal;

import android.app.Application;
import android.util.Log;

public final class MinimalAppApplication extends Application
    implements NativeUiBridge.Host {

  private static final String TAG = "AppTraverseMinimal";
  private static final String LIBRARY_NAME = "apptraverse_minimal_app";

  private final NativeUiBridge uiBridge = new NativeUiBridge();
  private long runtimeHandle;

  @Override
  public void onCreate() {
    super.onCreate();
    System.loadLibrary(LIBRARY_NAME);
    runtimeHandle =
        NativeRuntime.nativeCreate(getFilesDir().getAbsolutePath(), uiBridge);
    if (runtimeHandle == 0L) {
      Log.e(TAG, "Native runtime create failed");
      return;
    }
    uiBridge.attachHost(this);
  }

  NativeUiBridge uiBridge() {
    return uiBridge;
  }

  long runtimeHandle() {
    return runtimeHandle;
  }

  void startModelOnce() {
    if (runtimeHandle == 0L) {
      return;
    }
    NativeRuntime.nativeStartModel(runtimeHandle);
  }

  @Override
  public void consumePublication(int kind) {
    if (runtimeHandle == 0L) {
      return;
    }
    NativeRuntime.nativeConsumePublication(runtimeHandle, kind);
  }
}
