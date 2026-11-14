package com.apptraverse.minimal;

/** Explicit testHooks variant only; ordinary APK has no probe declarations. */
public final class NativeTestRuntime {
  private NativeTestRuntime() {}
  static native void nativeSetCheckpointPersistTestGateRoot(String filesDir);

  static native void nativeEnableCheckpointPersistTestGate();

  static native void nativeOpenCheckpointPersistTestGate();

  static native void nativeDisableCheckpointPersistTestGate();

  static native boolean nativeWaitForCheckpointPersistGateEntries(
      int expectedCount, long timeoutMs);

  static native String nativeRunPendingBackgroundSaveRaceProbe();

  static native String nativeRunEarlyBackgroundSaveProbe();

  static native String nativeRunModelThreadStartSyncProbe();

  static native String nativeRunColdReloadApplyStateProbe(String profileDir);

  static native String nativeRunColdReloadProfileStateProbe(
      String profileDir, int expectedSelectionIndex, boolean expectedApplyEnabled,
      String expectedTitle);

  static native String nativeRunColdLoadLabelProbe(String profileDir);

  static native String nativeRunInitialPublicationLabelProbe(String profileDir);

  static native String nativeRunEmbeddedBackingProbe(String profileDir, String emptyUserDir);
}
