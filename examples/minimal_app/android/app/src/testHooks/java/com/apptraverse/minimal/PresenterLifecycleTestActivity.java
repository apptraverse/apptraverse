package com.apptraverse.minimal;

import android.app.Activity;
import android.os.Bundle;
import android.widget.FrameLayout;

/** TestHooks-only native presenter host; independent of the product runtime. */
public final class PresenterLifecycleTestActivity extends Activity {
  private FrameLayout contentRoot;

  @Override protected void onCreate(Bundle state) {
    super.onCreate(state);
    contentRoot = new FrameLayout(this);
    setContentView(contentRoot);
  }

  FrameLayout contentRoot() { return contentRoot; }
}
