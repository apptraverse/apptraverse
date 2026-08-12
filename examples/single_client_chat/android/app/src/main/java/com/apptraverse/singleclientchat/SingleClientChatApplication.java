package com.apptraverse.singleclientchat;

import android.app.Application;
import android.util.Log;

/**
 * Owns the native runtime handle and the background thread that runs the core
 * Update/WaitUntil loop. The runtime outlives Activity recreation, so a
 * rotation never restarts the model.
 */
public final class SingleClientChatApplication extends Application {

  private static final String TAG = "AppTraverseChat";
  private static final String LIBRARY_NAME = "apptraverse_single_client_chat";
  private static final long JOIN_TIMEOUT_MS = 5000L;

  private final NativeUiBridge uiBridge = new NativeUiBridge();

  private long runtimeHandle;
  private Thread coreThread;

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

    coreThread = new Thread(new CoreLoop(runtimeHandle), "apptraverse-core");
    coreThread.start();
  }

  @Override
  public void onTerminate() {
    shutdown();
    super.onTerminate();
  }

  NativeUiBridge uiBridge() {
    return uiBridge;
  }

  /** Queues the text for the core thread. */
  void send(String text) {
    if (runtimeHandle != 0L) {
      NativeRuntime.nativeQueueSend(runtimeHandle, text);
    }
  }

  /** Queues an AddPeer command for the core thread. */
  void addPeer(String uid) {
    if (runtimeHandle != 0L) {
      NativeRuntime.nativeQueueAddPeer(runtimeHandle, uid);
    }
  }

  /** Queues the current Activity content viewport for the core thread. */
  void queueWindowChanged(int width, int height, int densityDpi) {
    if (runtimeHandle != 0L) {
      NativeRuntime.nativeQueueWindowChanged(
          runtimeHandle, width, height, densityDpi);
    }
  }

  private void shutdown() {
    if (runtimeHandle == 0L) {
      return;
    }

    NativeRuntime.nativeStop(runtimeHandle);
    if (coreThread != null) {
      try {
        coreThread.join(JOIN_TIMEOUT_MS);
      } catch (InterruptedException interrupted) {
        Thread.currentThread().interrupt();
      }
      coreThread = null;
    }
    NativeRuntime.nativeDestroy(runtimeHandle);
    runtimeHandle = 0L;
  }

  private static final class CoreLoop implements Runnable {
    private final long handle;

    CoreLoop(long runtimeHandle) {
      handle = runtimeHandle;
    }

    @Override
    public void run() {
      NativeRuntime.nativeRun(handle);
    }
  }
}
