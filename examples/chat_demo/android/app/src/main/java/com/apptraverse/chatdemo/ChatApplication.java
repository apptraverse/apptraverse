package com.apptraverse.chatdemo;

import android.app.Application;
import android.util.Log;

/**
 * Owns the native ChatSession handle and model thread. The runtime outlives
 * Activity recreation so the session is not restarted on rotation.
 */
public final class ChatApplication extends Application implements NativeUiBridge.Host {

  private static final String TAG = "AppTraverseChat";
  private static final String LIBRARY_NAME = "apptraverse_chat";
  private static final long JOIN_TIMEOUT_MS = 10000L;

  private final NativeUiBridge uiBridge = new NativeUiBridge();
  private final ChatUiRetention uiRetention = new ChatUiRetention();

  private long runtimeHandle;
  private Thread modelThread;

  @Override
  public void onCreate() {
    super.onCreate();
    System.loadLibrary(LIBRARY_NAME);

    runtimeHandle = NativeRuntime.nativeCreate(getFilesDir().getAbsolutePath(), uiBridge);
    if (runtimeHandle == 0L) {
      Log.e(TAG, "Native runtime could not be created");
      return;
    }
    uiBridge.attachHost(this);

    final long handle = runtimeHandle;
    modelThread = new Thread(new Runnable() {
      @Override
      public void run() {
        NativeRuntime.nativeStart(handle);
      }
    }, "apptraverse-chat-model");
    modelThread.start();
  }

  NativeUiBridge uiBridge() {
    return uiBridge;
  }

  ChatUiRetention uiRetention() {
    return uiRetention;
  }

  long runtimeHandle() {
    return runtimeHandle;
  }

  void joinHost(byte[] hostUid) {
    NativeRuntime.nativeJoinHost(runtimeHandle, hostUid);
  }

  void selectChat(long entryId) {
    NativeRuntime.nativeSelectChat(runtimeHandle, entryId);
  }

  void editDraft(long entryId, byte[] text, long editRevision) {
    NativeRuntime.nativeEditDraft(runtimeHandle, entryId, text, editRevision);
  }

  void sendDraft(long entryId, byte[] text, long editRevision) {
    NativeRuntime.nativeSendDraft(runtimeHandle, entryId, text, editRevision);
  }

  void saveScroll(long entryId, boolean followTail, byte[] msgUid, long msgSeq,
                  double offset) {
    NativeRuntime.nativeSaveScroll(runtimeHandle, entryId, followTail, msgUid, msgSeq, offset);
  }

  void checkpoint() {
    if (runtimeHandle == 0L) {
      return;
    }
    NativeRuntime.nativeCheckpoint(runtimeHandle);
  }

  void setWideLayout(boolean wide) {
    NativeRuntime.nativeSetWideLayout(runtimeHandle, wide);
  }

  void requestStop() {
    NativeRuntime.nativeRequestStop(runtimeHandle);
  }

  @Override
  public void consumeUiUpdate() {
    NativeRuntime.nativeConsumeUiUpdate(runtimeHandle);
  }

  @Override
  public void onModelStopped() {
    try {
      if (modelThread != null) {
        modelThread.join(JOIN_TIMEOUT_MS);
      }
    } catch (InterruptedException interrupted) {
      Thread.currentThread().interrupt();
    }
    modelThread = null;
    NativeRuntime.nativeDestroy(runtimeHandle);
    runtimeHandle = 0L;
    Log.i(TAG, "CHAT_APP_STOPPED");
    uiBridge.notifyStopped();
  }
}
