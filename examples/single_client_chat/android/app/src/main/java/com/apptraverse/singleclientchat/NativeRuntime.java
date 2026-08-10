package com.apptraverse.singleclientchat;

/**
 * Java view of the native AppTraverse runtime. The native library binds these
 * methods with RegisterNatives from JNI_OnLoad, it exports no Java_* symbols.
 */
final class NativeRuntime {

  private NativeRuntime() {
  }

  /** Returns the runtime handle, or 0 when the runtime could not be created. */
  static native long nativeCreate(String filesDir, NativeUiBridge uiBridge);

  /** Runs the core Update/WaitUntil loop, blocks until nativeStop. */
  static native void nativeRun(long handle);

  /** Queues a message for the core thread. */
  static native void nativeQueueSend(long handle, String text);

  /** Queues a WindowChangedEvent for the Activity content viewport. */
  static native void nativeQueueWindowChanged(
      long handle, int width, int height, int densityDpi);

  /** Asks the core loop to finish. */
  static native void nativeStop(long handle);

  /** Destroys the runtime, the core thread must be joined first. */
  static native void nativeDestroy(long handle);
}
