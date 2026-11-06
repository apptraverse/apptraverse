package com.apptraverse.windowcatalog;

/** JNI callbacks from the native model/UI thread onto the main looper. */
public final class NativeUiBridge {
  private Listener listener;

  public interface Listener {
    void onPublication(int kind);

    void onMobilePages(long[] windowIds, long activeWindowId);
  }

  public void attach(Listener newListener) {
    listener = newListener;
  }

  public void detach(Listener oldListener) {
    if (listener == oldListener) {
      listener = null;
    }
  }

  /** Called from native AndroidApplicationHost (model thread). */
  public void onNativePublication(int kind) {
    onPublication(kind);
  }

  public void onPublication(int kind) {
    if (listener != null) {
      listener.onPublication(kind);
    }
  }

  public void onMobilePages(long[] windowIds, long activeWindowId) {
    if (listener != null) {
      listener.onMobilePages(windowIds, activeWindowId);
    }
  }

  /** Called from native AndroidApplicationHost after checkpoint persist. */
  public void onNativeBackgroundSaveFinished(int persistResult) {
    onBackgroundSaveFinished(persistResult);
  }

  public void onBackgroundSaveFinished(int persistResult) {
    // Host checkpoint completion; no UI action required for catalog demo.
  }
}
