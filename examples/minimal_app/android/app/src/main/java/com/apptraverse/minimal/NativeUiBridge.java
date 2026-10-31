package com.apptraverse.minimal;

import android.os.Handler;
import android.os.Looper;

/** Delivers model-thread publication notifications to the Android main looper. */
public final class NativeUiBridge {

  interface Host {
    void consumePublication(int kind);

    void onBackgroundSaveFinished(int persistResult);
  }

  private final Handler mainHandler = new Handler(Looper.getMainLooper());
  private Host host;

  void attachHost(Host newHost) {
    host = newHost;
  }

  /** Called from the model thread. */
  void onNativeBackgroundSaveFinished(final int persistResult) {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (host != null) {
          host.onBackgroundSaveFinished(persistResult);
        }
      }
    });
  }

  /** Called from the model thread. */
  void onNativePublication(final int kind) {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (host != null) {
          host.consumePublication(kind);
        }
      }
    });
  }
}
