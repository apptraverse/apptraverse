package com.apptraverse.singleclientchat;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/**
 * Debug-build-only Send channel for automated tests. Release builds omit this
 * receiver. The only accepted action queues text through
 * {@link SingleClientChatApplication#send(String)}.
 */
public final class DebugCommandReceiver extends BroadcastReceiver {

  static final String ACTION = "com.apptraverse.singleclientchat.DEBUG_SEND";
  static final String EXTRA_TEXT = "text";
  static final int MAX_TEXT_CHARS = 1024;

  private static final String TAG = "AppTraverseChat";

  @Override
  public void onReceive(Context context, Intent intent) {
    if (intent == null || !ACTION.equals(intent.getAction())) {
      setResultCode(Activity.RESULT_CANCELED);
      return;
    }

    String text = intent.getStringExtra(EXTRA_TEXT);
    if (text == null) {
      setResultCode(Activity.RESULT_CANCELED);
      return;
    }
    text = text.trim();
    if (text.isEmpty() || text.length() > MAX_TEXT_CHARS) {
      setResultCode(Activity.RESULT_CANCELED);
      return;
    }

    if (context == null) {
      setResultCode(Activity.RESULT_CANCELED);
      return;
    }
    Context appContext = context.getApplicationContext();
    if (!(appContext instanceof SingleClientChatApplication)) {
      setResultCode(Activity.RESULT_CANCELED);
      return;
    }

    ((SingleClientChatApplication) appContext).send(text);
    Log.i(TAG, "DEBUG_COMMAND_SEND_QUEUED text=" + text);
    setResultCode(Activity.RESULT_OK);
  }
}
