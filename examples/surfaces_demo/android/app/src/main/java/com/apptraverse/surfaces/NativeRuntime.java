package com.apptraverse.surfaces;

/**
 * Java view of the native surfaces runtime. The native library binds these
 * methods with RegisterNatives from JNI_OnLoad, it exports no Java_* symbols.
 * Only stable Surface ObjIds cross this boundary, never model pointers.
 */
final class NativeRuntime {

  private NativeRuntime() {
  }

  /** Returns the runtime handle, or 0 when the runtime could not be created. */
  static native long nativeCreate(String filesDir, NativeUiBridge uiBridge);

  /** Runs the model session, blocks until the state was saved. */
  static native void nativeRunModel(long handle);

  /** Main thread: consumes the publication the model thread announced. */
  static native void nativeConsumePublication(long handle, int kind);

  /** Main thread: Add on the page that owns this Surface ObjId. */
  static native void nativeAddFromSurface(long handle, long surfaceId);

  /** Main thread: Remove the page that owns this Surface ObjId. */
  static native void nativeRemoveSurface(long handle, long surfaceId);

  /** Main thread: the pager settled on the page that owns this Surface ObjId. */
  static native void nativePageShown(long handle, long surfaceId);

  /** Main thread: checkpoint Application::Save without stopping the session. */
  static native void nativePersistState(long handle);

  /** Asks the model session to stop; the state is saved during the drain. */
  static native void nativeRequestStop(long handle);

  /** Main thread: unloads presenters and the GUI mirror after the model stopped. */
  static native void nativeUnloadUi(long handle);

  /** Destroys the runtime, the model thread must be joined first. */
  static native void nativeDestroy(long handle);
}
