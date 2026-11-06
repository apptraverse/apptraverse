package com.apptraverse.windowcatalog;

/** JNI entry points for the WindowCatalog Android host. */
public final class NativeRuntime {
  static {
    System.loadLibrary("apptraverse_window_catalog");
  }

  private NativeRuntime() {}

  public static native long nativeCreate(String filesDir, NativeUiBridge uiBridge);

  public static native void nativeSetPresentationRoot(
      long handle, android.app.Activity activity, android.view.ViewGroup contentRoot);

  public static native void nativeStartModel(long handle);

  public static native void nativeConsumePublication(long handle, int kind);

  public static native void nativeRequestBackgroundSave(long handle);

  public static native void nativeMobilePageShown(long handle, long windowObjId);

  public static native void nativeMobileSwipe(long handle, int delta);

  public static native void nativeDestroy(long handle);
}
