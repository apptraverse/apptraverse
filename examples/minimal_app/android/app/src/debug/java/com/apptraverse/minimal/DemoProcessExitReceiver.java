package com.apptraverse.minimal;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Process;

/** Debug-only: lets instrumented tests terminate {@code :demo} without {@code am kill} on the package. */
public final class DemoProcessExitReceiver extends BroadcastReceiver {

  @Override
  public void onReceive(Context context, Intent intent) {
    if (intent == null
        || !MinimalAppTestControl.ACTION_EXIT_DEMO_PROCESS.equals(intent.getAction())) {
      return;
    }
    String processName = context.getApplicationInfo().processName;
    if (!MinimalAppTestControl.isDemoHostProcessName(processName)) {
      return;
    }
    Process.killProcess(Process.myPid());
  }
}
