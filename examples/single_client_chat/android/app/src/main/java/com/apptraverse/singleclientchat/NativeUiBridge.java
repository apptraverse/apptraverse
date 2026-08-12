package com.apptraverse.singleclientchat;

import android.os.Handler;
import android.os.Looper;

/**
 * The only object the native runtime keeps a JNI global reference to.
 * It marshals native transcript and Aether UID callbacks to the main thread,
 * caches the last values and forwards them to the Activity while one is attached.
 */
public final class NativeUiBridge {

  /** Implemented by the Activity between onStart and onStop. */
  interface Listener {
    void onTranscript(String transcript);

    void onAetherUid(String uid);
  }

  private final Handler mainHandler = new Handler(Looper.getMainLooper());

  // Main thread only.
  private Listener listener;
  private String transcript = "";
  private String aetherUid = "";

  void attach(Listener newListener) {
    listener = newListener;
    newListener.onTranscript(transcript);
    if (!aetherUid.isEmpty()) {
      newListener.onAetherUid(aetherUid);
    }
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

  /** Called from the native core thread. */
  void onNativeAetherUid(final String value) {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        aetherUid = value == null ? "" : value;
        if (listener != null) {
          listener.onAetherUid(aetherUid);
        }
      }
    });
  }
}
