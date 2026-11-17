package com.apptraverse.windowcatalog;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

/** Native Activity shell; page selection belongs to the model. */
public final class MainActivity extends Activity {
  private static final String TAG = "AppTraverseWindowCatalog";
  private static final int SWIPE_MIN_DISTANCE_PX = 80;
  public static final String EXTRA_STATE_SUBDIR = "stateSubdir";
  public static final String DEFAULT_STATE_SUBDIR = "window_catalog_state";
  private TextView loading;
  private float gestureDownX, gestureDownY;
  private long gestureDownTime;
  private boolean trackingGesture;

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
    app.initRuntime(getFilesDir().getAbsolutePath(), stateSubdir,
        getIntent().getStringExtra("localSeedSubdir"));
    NativeRuntime.nativeSetPresentationRoot(app.runtimeHandle(), this, pageContainer);
    app.startModelOnce();
  }

  @Override public boolean onCreateOptionsMenu(android.view.Menu menu) { return true; }

  @Override public boolean onPrepareOptionsMenu(android.view.Menu menu) {
    menu.clear();
    com.apptraverse.ui.android.AppTraverseAndroidUi.populateAppearanceMenu(
        (android.view.ViewGroup) findViewById(R.id.page_container), menu);
    return true;
  }

  @Override public boolean dispatchTouchEvent(MotionEvent event) {
    if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
      gestureDownX = event.getX(); gestureDownY = event.getY();
      gestureDownTime = event.getEventTime(); trackingGesture = true;
    } else if (event.getActionMasked() == MotionEvent.ACTION_CANCEL) {
      trackingGesture = false;
    } else if (event.getActionMasked() == MotionEvent.ACTION_UP && trackingGesture) {
      trackingGesture = false;
      float dx = event.getX() - gestureDownX;
      float dy = event.getY() - gestureDownY;
      // A quick horizontal swipe selects a model page. Slow pans remain native
      // overflow scrolling, and taps retain native click/accessibility behavior.
      if (event.getEventTime() - gestureDownTime < 700
          && Math.abs(dx) >= SWIPE_MIN_DISTANCE_PX && Math.abs(dx) > Math.abs(dy)) {
        application().mobileSwipe(dx < 0 ? 1 : -1);
        MotionEvent cancel = MotionEvent.obtain(event);
        cancel.setAction(MotionEvent.ACTION_CANCEL);
        super.dispatchTouchEvent(cancel); cancel.recycle();
        return true;
      }
    }
    return super.dispatchTouchEvent(event) || trackingGesture;
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
    invalidateOptionsMenu();
    Log.i(TAG, "PAGE_SHOWN id=" + activeWindowId + " count=" + windowIds.length);
  }

  private WindowCatalogApplication application() {
    return (WindowCatalogApplication) getApplication();
  }
}
