package com.apptraverse.chatdemo;

/**
 * Process-wide UI private state retained across Activity recreation (ViewModel
 * equivalent without AndroidX). ChatSession remains in {@link ChatApplication}.
 */
public final class ChatUiRetention {
  public long selectedEntryId;
  public long draftEditRevision;
  public boolean followTail = true;
  public byte[] scrollMsgUid = new byte[0];
  public long scrollMsgSeq;
  public double scrollOffset;
  public boolean conversationVisibleInPortrait;
}
