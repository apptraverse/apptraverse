package com.apptraverse.minimal;

import android.view.ViewGroup;

/** JNI entry points of the test-only presenter lifecycle fixture library (UI thread only). */
final class PresenterLifecycleFixture {

  static {
    System.loadLibrary("apptraverse_minimal_app_test_fixture");
  }

  private PresenterLifecycleFixture() {}

  static native long nativeCreate(ViewGroup contentRoot);

  static native void nativeRemount(long handle);

  static native void nativeRemoveMainListRow(long handle, int rowIndex);

  static native int nativeEnqueueCount(long handle);

  static native void nativeDestroy(long handle);
}
