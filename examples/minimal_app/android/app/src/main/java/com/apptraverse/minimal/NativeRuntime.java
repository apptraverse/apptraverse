package com.apptraverse.minimal;

/** JNI entry points for the Minimal App Android shell. */
public final class NativeRuntime {

  private NativeRuntime() {}

  static native long nativeCreate(
      String filesDir, String stateSubdir, NativeUiBridge uiBridge);

  static native void nativeSetPresentationRoot(
      long handle, android.app.Activity activity, android.view.ViewGroup contentRoot);

  static native void nativeStartModel(long handle);

  static native void nativeConsumePublication(long handle, int kind);

  static native void nativeRequestBackgroundSave(long handle);

  static native void nativeDestroy(long handle);

  static native void nativeSetCheckpointPersistTestGateRoot(String filesDir);

  static native void nativeEnableCheckpointPersistTestGate();

  static native void nativeOpenCheckpointPersistTestGate();

  static native void nativeDisableCheckpointPersistTestGate();

  static native boolean nativeWaitForCheckpointPersistGateEntries(
      int expectedCount, long timeoutMs);

  static native String nativeRunPendingBackgroundSaveRaceProbe();

  static native String nativeRunEarlyBackgroundSaveProbe();

  static native String nativeRunModelThreadStartSyncProbe();

  /** A1R1: distill → cold LoadApplication label probe (no UI presenters). */
  static native String nativeRunColdReloadApplyStateProbe(String profileDir);

  static native String nativeRunColdReloadProfileStateProbe(
      String profileDir, int expectedSelectionIndex, boolean expectedApplyEnabled,
      String expectedTitle);

  static native String nativeRunColdLoadLabelProbe(String profileDir);

  static native String nativeRunInitialPublicationLabelProbe(String profileDir);
}
