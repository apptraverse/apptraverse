package com.apptraverse.surfaces;

import android.os.Handler;
import android.os.Looper;

/**
 * The only object the native runtime keeps a JNI global reference to. It moves
 * model-thread notifications to the main thread and caches the last page list
 * so an Activity that attaches later renders the current pager immediately.
 */
public final class NativeUiBridge {

  /** Implemented by {@link SurfacesApplication}, main thread only. */
  interface Host {
    void consumePublication(int kind);

    void onModelStopped();
  }

  /** Implemented by the Activity between onStart and onStop. */
  interface Listener {
    void onPages(long[] ids, int[] numbers);

    void onModelStopped();
  }

  private final Handler mainHandler = new Handler(Looper.getMainLooper());

  // Main thread only.
  private Host host;
  private Listener listener;
  private long[] pageIds = new long[0];
  private int[] pageNumbers = new int[0];
  private boolean stopped;

  void attachHost(Host newHost) {
    host = newHost;
  }

  void attach(Listener newListener) {
    listener = newListener;
    if (pageNumbers.length > 0) {
      newListener.onPages(pageIds, pageNumbers);
    }
    if (stopped) {
      newListener.onModelStopped();
    }
  }

  void detach(Listener oldListener) {
    if (listener == oldListener) {
      listener = null;
    }
  }

  /** Called from the model thread. */
  void onNativePublication(final int kind) {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        host.consumePublication(kind);
      }
    });
  }

  /** Called from the main thread, inside the publication the host consumes. */
  void onNativePages(long[] ids, int[] numbers) {
    pageIds = ids;
    pageNumbers = numbers;
    if (listener != null) {
      listener.onPages(ids, numbers);
    }
  }

  /** Called from the model thread after the state was saved. */
  void onNativeStopped() {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        host.onModelStopped();
      }
    });
  }

  /** Called by the host after the native teardown finished. */
  void notifyStopped() {
    stopped = true;
    if (listener != null) {
      listener.onModelStopped();
    }
  }
}
