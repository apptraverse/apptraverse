package com.apptraverse.minimal;

/** Loads JNI in the instrumentation process (separate from target {@code :demo}). */
final class InstrumentedTestNative {

  private static boolean loaded;

  private InstrumentedTestNative() {}

  static void ensureLoaded() {
    if (loaded) {
      return;
    }
    System.loadLibrary("apptraverse_minimal_app");
    loaded = true;
  }
}
