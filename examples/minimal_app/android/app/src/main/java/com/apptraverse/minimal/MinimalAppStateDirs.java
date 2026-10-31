package com.apptraverse.minimal;

/** Relative names under the app files directory for model persistence. */
public final class MinimalAppStateDirs {

  public static final String DEFAULT = "minimal_app_android_state";

  /** Intent extra for instrumented runs (isolated profile under files dir). */
  public static final String INTENT_EXTRA_STATE_SUBDIR = "stateSubdir";

  /** When true, {@link MainActivity#onStop()} skips background checkpoint save. */
  public static final String INTENT_EXTRA_SKIP_CHECKPOINT_SAVE = "skipCheckpointSave";

  private MinimalAppStateDirs() {}
}
