package com.apptraverse.minimal;

/** Shared constants for debug/test control of the Minimal App Android shell. */
public final class MinimalAppTestControl {

  public static final String ACTION_EXIT_DEMO_PROCESS =
      "com.apptraverse.minimal.action.EXIT_DEMO_PROCESS";

  private MinimalAppTestControl() {}

  static boolean isDemoHostProcessName(String processName) {
    if (processName == null) {
      return false;
    }
    return processName.equals("com.apptraverse.minimal:demo");
  }
}
