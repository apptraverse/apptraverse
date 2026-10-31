package com.apptraverse.minimal;

/** JNI entry points for the Minimal App Android shell. */
public final class NativeRuntime {

  private NativeRuntime() {}

  static native long nativeCreate(String filesDir, NativeUiBridge uiBridge);

  static native void nativeSetPresentationRoot(
      long handle, android.app.Activity activity, android.view.ViewGroup contentRoot);

  static native void nativeStartModel(long handle);

  static native void nativeConsumePublication(long handle, int kind);

  static native void nativeDestroy(long handle);

  /** A1R1: distill → cold LoadApplication label probe (no UI presenters). */
  static native String nativeRunColdLoadLabelProbe(String profileDir);

  static native String nativeRunInitialPublicationLabelProbe(String profileDir);
}
