package com.apptraverse.singleclientchat;

import android.os.Handler;
import android.os.Looper;

/**
 * The only object the native runtime keeps a JNI global reference to.
 * It marshals native transcript callbacks to the main thread, caches the last
 * transcript and forwards it to the Activity while one is attached.
 */
public final class NativeUiBridge {

  /** Implemented by the Activity between onStart and onStop. */
  interface Listener {
    void onTranscript(String transcript);
  }

  private final Handler mainHandler = new Handler(Looper.getMainLooper());

  // Main thread only.
  private Listener listener;
  private String transcript = "";

  void attach(Listener newListener) {
    listener = newListener;
    newListener.onTranscript(transcript);
  }

  void detach(Listener oldListener) {
    if (listener == oldListener) {
      listener = null;
    }
  }

  /** Called from the native core thread. */
  void onNativeTranscript(final String value) {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        transcript = value;
        if (listener != null) {
          listener.onTranscript(value);
        }
      }
    });
  }
}
