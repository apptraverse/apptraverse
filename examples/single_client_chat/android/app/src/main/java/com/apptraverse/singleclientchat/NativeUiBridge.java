package com.apptraverse.singleclientchat;

import android.os.Handler;
import android.os.Looper;

/**
 * The only object the native runtime keeps a JNI global reference to.
 * It marshals native callbacks to the main thread, caches the last known UI
 * state and forwards it to the Activity while one is attached.
 */
public final class NativeUiBridge {

  /** Implemented by the Activity between onStart and onStop. */
  interface Listener {
    void onStatus(String status);

    void onTranscript(String transcript);

    void onMessageCommitted(String text);
  }

  private final Handler mainHandler = new Handler(Looper.getMainLooper());

  // Main thread only.
  private Listener listener;
  private String status = "";
  private String transcript = "";

  void attach(Listener newListener) {
    listener = newListener;
    newListener.onStatus(status);
    newListener.onTranscript(transcript);
  }

  void detach(Listener oldListener) {
    if (listener == oldListener) {
      listener = null;
    }
  }

  /** Called from the native core thread. */
  void onNativeStatus(final String value) {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        status = value;
        if (listener != null) {
          listener.onStatus(value);
        }
      }
    });
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

  /** Called from the native core thread once a message reached the model. */
  void onNativeMessageCommitted(final String text) {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (listener != null) {
          listener.onMessageCommitted(text);
        }
      }
    });
  }
}
