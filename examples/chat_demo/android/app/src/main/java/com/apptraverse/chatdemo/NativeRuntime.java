package com.apptraverse.chatdemo;

/** JNI entry points for the single ChatSession runtime. */
public final class NativeRuntime {
  private NativeRuntime() {}

  public static native long nativeCreate(String filesDir, NativeUiBridge bridge);

  public static native void nativeStart(long handle);

  public static native void nativeJoinHost(long handle, byte[] hostUid);

  public static native void nativeSelectChat(long handle, long entryId);

  public static native void nativeEditDraft(long handle, long entryId, byte[] text,
                                            long editRevision);

  public static native void nativeSendDraft(long handle, long entryId, byte[] text,
                                            long editRevision);

  public static native void nativeSaveScroll(long handle, long entryId, boolean followTail,
                                             byte[] msgUid, long msgSeq, double offset);

  public static native void nativeCheckpoint(long handle);

  public static native void nativeRequestStop(long handle);

  public static native boolean nativeIsStopped(long handle);

  public static native boolean nativeConsumeUiUpdate(long handle);

  public static native void nativeSetWideLayout(long handle, boolean wide);

  public static native void nativeDestroy(long handle);
}
