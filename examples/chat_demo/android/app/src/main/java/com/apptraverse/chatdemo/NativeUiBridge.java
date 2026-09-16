package com.apptraverse.chatdemo;

import android.os.Handler;
import android.os.Looper;

/**
 * Moves model-thread notifications to the main thread and caches the last UI
 * snapshot so an Activity that attaches after rotation renders immediately.
 */
public final class NativeUiBridge {

  interface Host {
    void consumeUiUpdate();
    void onModelStopped();
  }

  interface Listener {
    void onUiState(ChatUiState state);
    void onModelStopped();
  }

  public static final class ChatUiState {
    public long selectedEntryId;
    public long[] entryIds = new long[0];
    public byte[][] entryLabels = new byte[0][];
    public byte[] draft = new byte[0];
    public byte[] transcript = new byte[0];
    public byte[] status = new byte[0];
    public byte[] presence = new byte[0];
    public boolean followTail = true;
    public byte[] scrollMsgUid = new byte[0];
    public long scrollMsgSeq;
    public double scrollOffset;
    public boolean sendEnabled;
    public boolean wideLayout;
  }

  private final Handler mainHandler = new Handler(Looper.getMainLooper());

  private Host host;
  private Listener listener;
  private ChatUiState cached = new ChatUiState();
  private boolean stopped;

  void attachHost(Host newHost) {
    host = newHost;
  }

  void attach(Listener newListener) {
    listener = newListener;
    newListener.onUiState(cached);
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
  void onNativeNotify() {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (host != null) {
          host.consumeUiUpdate();
        }
      }
    });
  }

  /** Called from the main thread inside consumeUiUpdate. */
  void onNativeState(long selectedEntryId, long[] entryIds, byte[][] entryLabels,
                     byte[] draft, byte[] transcript, byte[] status, byte[] presence,
                     boolean followTail, byte[] scrollMsgUid, long scrollMsgSeq,
                     double scrollOffset, boolean sendEnabled, boolean wideLayout) {
    cached = new ChatUiState();
    cached.selectedEntryId = selectedEntryId;
    cached.entryIds = entryIds;
    cached.entryLabels = entryLabels;
    cached.draft = draft;
    cached.transcript = transcript;
    cached.status = status;
    cached.presence = presence;
    cached.followTail = followTail;
    cached.scrollMsgUid = scrollMsgUid;
    cached.scrollMsgSeq = scrollMsgSeq;
    cached.scrollOffset = scrollOffset;
    cached.sendEnabled = sendEnabled;
    cached.wideLayout = wideLayout;
    if (listener != null) {
      listener.onUiState(cached);
    }
  }

  void onNativeStopped() {
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (host != null) {
          host.onModelStopped();
        }
      }
    });
  }

  void notifyStopped() {
    stopped = true;
    if (listener != null) {
      listener.onModelStopped();
    }
  }
}
