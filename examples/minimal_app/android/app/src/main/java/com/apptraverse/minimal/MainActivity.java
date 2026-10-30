package com.apptraverse.minimal;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.FrameLayout;

/** Host Activity: one logical desktop page inside the content root. */
public final class MainActivity extends Activity {

  private static final String TAG = "AppTraverseMinimal";
  private static boolean modelStarted;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

    FrameLayout contentRoot = new FrameLayout(this);
    contentRoot.setLayoutParams(new ViewGroup.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT));
    setContentView(contentRoot);

    MinimalAppApplication app = (MinimalAppApplication) getApplication();
    if (app.runtimeHandle() == 0L) {
      Log.e(TAG, "Runtime missing");
      return;
    }

    NativeRuntime.nativeSetPresentationRoot(
        app.runtimeHandle(), this, contentRoot);

    if (!modelStarted) {
      modelStarted = true;
      app.startModelOnce();
    }
  }
}
