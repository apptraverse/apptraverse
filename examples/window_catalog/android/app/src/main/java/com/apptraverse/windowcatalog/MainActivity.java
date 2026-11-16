package com.apptraverse.windowcatalog;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

/** Native Activity shell; page selection and layout belong to the model. */
public final class MainActivity extends Activity {
  private static final String TAG = "AppTraverseWindowCatalog";
  private static final int SWIPE_MIN_DISTANCE_PX = 80;
  public static final String EXTRA_STATE_SUBDIR = "stateSubdir";
  public static final String DEFAULT_STATE_SUBDIR = "window_catalog_state";
  private TextView loading;
  private GestureDetector swipeDetector;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);
    loading = findViewById(R.id.loading);
    FrameLayout pageContainer = findViewById(R.id.page_container);
    WindowCatalogApplication app = application();
    app.setActivity(this);
    String stateSubdir = getIntent().getStringExtra(EXTRA_STATE_SUBDIR);
    if (stateSubdir == null || stateSubdir.isEmpty()) stateSubdir = DEFAULT_STATE_SUBDIR;
    app.initRuntime(getFilesDir().getAbsolutePath(), stateSubdir);
    NativeRuntime.nativeSetPresentationRoot(app.runtimeHandle(), this, pageContainer);
    app.startModelOnce();

    swipeDetector = new GestureDetector(this, new GestureDetector.SimpleOnGestureListener() {
      @Override public boolean onDown(MotionEvent event) { return true; }
      @Override public boolean onFling(MotionEvent down, MotionEvent up,
                                       float velocityX, float velocityY) {
        float dx = up.getX() - down.getX();
        if (Math.abs(dx) < SWIPE_MIN_DISTANCE_PX
            || Math.abs(dx) < Math.abs(up.getY() - down.getY())) return false;
        application().mobileSwipe(dx < 0 ? 1 : -1);
        return true;
      }
    });

  }

  @Override public boolean dispatchTouchEvent(MotionEvent event) {
    // Observe gestures even when a native child fills the client mount. A
    // recognized swipe cancels the child's pending click; it only sends intent.
    if (swipeDetector != null && swipeDetector.onTouchEvent(event)
        && event.getActionMasked() == MotionEvent.ACTION_UP) {
      MotionEvent cancel = MotionEvent.obtain(event);
      cancel.setAction(MotionEvent.ACTION_CANCEL);
      super.dispatchTouchEvent(cancel);
      cancel.recycle();
      return true;
    }
    return super.dispatchTouchEvent(event);
  }

  @Override protected void onStop() {
    // Android may terminate a background process without another callback.
    // Persist one USER file after accepted model work at this lifecycle boundary.
    application().flushPendingBackgroundSaves();
    super.onStop();
  }

  @Override protected void onDestroy() {
    application().clearActivity(this);
    super.onDestroy();
  }

  public void onMobilePages(long[] windowIds, long activeWindowId) {
    loading.setVisibility(View.GONE);
    Log.i(TAG, "PAGE_SHOWN id=" + activeWindowId + " count=" + windowIds.length);
  }

  private WindowCatalogApplication application() {
    return (WindowCatalogApplication) getApplication();
  }
}
