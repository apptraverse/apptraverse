package com.apptraverse.windowcatalog;

import android.app.Activity;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

/** Native Activity shell; page selection belongs to the model. */
public final class MainActivity extends Activity
    implements com.apptraverse.ui.android.AppTraverseAndroidUi.ActivityAppearanceHost {
  private static final int SWIPE_MIN_DISTANCE_PX = 80;
  public static final String EXTRA_STATE_SUBDIR = "stateSubdir";
  public static final String DEFAULT_STATE_SUBDIR = "window_catalog_state";
  private TextView loading;
  private float gestureDownX, gestureDownY;
  private long gestureDownTime;
  private boolean trackingGesture;
  private boolean mountedDark, themeRecreationPosted;

  private boolean isDark(int mode) {
    return mode == 2 || (mode == 0 && (getResources().getConfiguration().uiMode
        & android.content.res.Configuration.UI_MODE_NIGHT_MASK)
        == android.content.res.Configuration.UI_MODE_NIGHT_YES);
  }

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    mountedDark = isDark(application().appearanceMode());
    setTheme(mountedDark ? android.R.style.Theme_Material : android.R.style.Theme_Material_Light);
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

  @Override public void setPresentedAppearance(int mode, boolean soften) {
    application().setAppearanceMode(mode);
    if (mountedDark == isDark(mode) || themeRecreationPosted) return;
    themeRecreationPosted = true;
    // Recreate standard Android chrome with its native theme after this apply.
    getWindow().getDecorView().post(() -> {
      themeRecreationPosted = false;
      if (!isFinishing() && !isDestroyed() && mountedDark != isDark(application().appearanceMode()))
        recreate();
    });
  }

  @Override public boolean onCreateOptionsMenu(android.view.Menu menu) { return true; }

  @Override public boolean onPrepareOptionsMenu(android.view.Menu menu) {
    menu.clear();
    NativeRuntime.nativePopulateMenu(application().runtimeHandle(), menu);
    return true;
  }

  @Override public boolean onOptionsItemSelected(android.view.MenuItem item) {
    if (item.hasSubMenu()) return super.onOptionsItemSelected(item);
    NativeRuntime.nativeExecuteMenuCommand(application().runtimeHandle(), item.getItemId());
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
  }

  private WindowCatalogApplication application() {
    return (WindowCatalogApplication) getApplication();
  }
}
