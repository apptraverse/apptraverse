package com.apptraverse.surfaces;

import android.app.Application;
import android.util.Log;

/**
 * Owns the native runtime handle and the model thread. The runtime outlives
 * Activity recreation, so the model is loaded once per process. Application
 * semantics stay in C++: this class only forwards taps and lifecycle edges.
 */
public final class SurfacesApplication extends Application
    implements NativeUiBridge.Host {

  private static final String TAG = "AppTraverseSurfaces";
  private static final String LIBRARY_NAME = "apptraverse_surfaces";
  private static final long JOIN_TIMEOUT_MS = 10000L;

  private final NativeUiBridge uiBridge = new NativeUiBridge();

  private long runtimeHandle;
  private Thread modelThread;

  @Override
  public void onCreate() {
    super.onCreate();

    System.loadLibrary(LIBRARY_NAME);

    runtimeHandle = NativeRuntime.nativeCreate(
        getFilesDir().getAbsolutePath(), uiBridge);
    if (runtimeHandle == 0L) {
      Log.e(TAG, "Native runtime could not be created");
      return;
    }
    uiBridge.attachHost(this);

    final long handle = runtimeHandle;
    modelThread = new Thread(new Runnable() {
      @Override
      public void run() {
        NativeRuntime.nativeRunModel(handle);
      }
    }, "apptraverse-model");
    modelThread.start();
  }

  NativeUiBridge uiBridge() {
    return uiBridge;
  }

  /** Add a Surface next to the page the user is on. */
  void addFrom(long surfaceId) {
    NativeRuntime.nativeAddFromSurface(runtimeHandle, surfaceId);
  }

  /** Remove the page the user is on; the last page stops the application. */
  void removeSurface(long surfaceId) {
    NativeRuntime.nativeRemoveSurface(runtimeHandle, surfaceId);
  }

  /** The pager settled on this page; the model records it as current. */
  void pageShown(long surfaceId) {
    NativeRuntime.nativePageShown(runtimeHandle, surfaceId);
  }

  /** Persist model Domain while running (Home / onStop may kill later). */
  void persistState() {
    if (runtimeHandle == 0L) {
      return;
    }
    NativeRuntime.nativePersistState(runtimeHandle);
  }

  /** Main thread: usable host size for every live Surface page. */
  void reportPresentationSize(int width, int height) {
    if (runtimeHandle == 0L) {
      return;
    }
    NativeRuntime.nativeReportPresentationSize(runtimeHandle, width, height);
  }

  /** Back / system close: graceful whole-application shutdown, not Remove. */
  void requestStop() {
    NativeRuntime.nativeRequestStop(runtimeHandle);
  }

  @Override
  public void consumePublication(int kind) {
    NativeRuntime.nativeConsumePublication(runtimeHandle, kind);
  }

  @Override
  public void onModelStopped() {
    try {
      modelThread.join(JOIN_TIMEOUT_MS);
    } catch (InterruptedException interrupted) {
      Thread.currentThread().interrupt();
    }
    modelThread = null;
    NativeRuntime.nativeUnloadUi(runtimeHandle);
    NativeRuntime.nativeDestroy(runtimeHandle);
    runtimeHandle = 0L;
    Log.i(TAG, "SURFACES_APP_STOPPED");
    uiBridge.notifyStopped();
  }
}
