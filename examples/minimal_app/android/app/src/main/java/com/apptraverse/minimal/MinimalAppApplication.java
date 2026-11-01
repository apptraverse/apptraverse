package com.apptraverse.minimal;

import android.app.Application;
import android.util.Log;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class MinimalAppApplication extends Application
    implements NativeUiBridge.Host {

  private static final String TAG = "AppTraverseMinimal";
  private static final String LIBRARY_NAME = "apptraverse_minimal_app";

  public interface BackgroundSaveObserver {
    void onBackgroundSaveFinished(int persistResult);
  }

  private final NativeUiBridge uiBridge = new NativeUiBridge();
  private final AtomicReference<BackgroundSaveObserver> backgroundSaveObserver =
      new AtomicReference<>();
  private final AtomicReference<CountDownLatch> backgroundSaveLatch =
      new AtomicReference<>();

  private long runtimeHandle;
  private boolean modelThreadStarted;

  @Override
  public void onCreate() {
    super.onCreate();
    System.loadLibrary(LIBRARY_NAME);
    NativeRuntime.nativeSetCheckpointPersistTestGateRoot(
        getFilesDir().getAbsolutePath());
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
    if (runtimeHandle != 0L) {
      return;
    }
    String subdir = stateSubdir != null ? stateSubdir : MinimalAppStateDirs.DEFAULT;
    runtimeHandle = NativeRuntime.nativeCreate(
        getFilesDir().getAbsolutePath(), subdir, uiBridge);
    modelThreadStarted = false;
    if (runtimeHandle == 0L) {
      Log.e(TAG, "Native runtime create failed");
    }
  }

  void setBackgroundSaveLatch(CountDownLatch latch) {
    backgroundSaveLatch.set(latch);
  }

  void clearBackgroundSaveLatch() {
    backgroundSaveLatch.set(null);
  }

  void setBackgroundSaveObserver(BackgroundSaveObserver observer) {
    backgroundSaveObserver.set(observer);
  }

  void startModelOnce() {
    if (runtimeHandle == 0L || modelThreadStarted) {
      return;
    }
    modelThreadStarted = true;
    NativeRuntime.nativeStartModel(runtimeHandle);
  }

  synchronized void clearRuntimeForColdReloadTest() {
    if (runtimeHandle != 0L) {
      NativeRuntime.nativeDestroy(runtimeHandle);
      runtimeHandle = 0L;
    }
    modelThreadStarted = false;
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
    Log.i(TAG, "background save finished result=" + persistResult);
    BackgroundSaveObserver observer = backgroundSaveObserver.get();
    if (observer != null) {
      observer.onBackgroundSaveFinished(persistResult);
    }
    CountDownLatch latch = backgroundSaveLatch.get();
    if (latch != null) {
      latch.countDown();
    }
  }
}
