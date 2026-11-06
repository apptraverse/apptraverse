package com.apptraverse.windowcatalog;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

/**
 * Mobile WindowCatalog host: swipe moves the persisted mobile_active_window ring
 * (catalog page 0, then open child windows in list order). Native chrome lives
 * inside each page client mount; this Activity only paginates visibility.
 */
public final class MainActivity extends Activity {

  private static final String TAG = "AppTraverseWindowCatalog";
  private static final int SWIPE_MIN_DISTANCE_PX = 80;

  /** Intent extra: USER profile directory name under app {@code files/}. */
  public static final String EXTRA_STATE_SUBDIR = "stateSubdir";
  public static final String DEFAULT_STATE_SUBDIR = "window_catalog_state";

  private TextView pageIndicator;
  private TextView loading;
  private FrameLayout pageContainer;

  private long[] pageWindowIds = new long[0];
  private int currentIndex;
  private long reportedActiveId;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    pageIndicator = findViewById(R.id.page_indicator);
    loading = findViewById(R.id.loading);
    pageContainer = findViewById(R.id.page_container);

    View controlsRow = findViewById(R.id.controls_row);
    if (controlsRow != null) {
      controlsRow.setVisibility(View.GONE);
    }
    TextView surfaceTitle = findViewById(R.id.surface_title);
    if (surfaceTitle != null) {
      surfaceTitle.setVisibility(View.GONE);
    }

    WindowCatalogApplication app = application();
    app.setActivity(this);

    String stateSubdir = getIntent().getStringExtra(EXTRA_STATE_SUBDIR);
    if (stateSubdir == null || stateSubdir.isEmpty()) {
      stateSubdir = DEFAULT_STATE_SUBDIR;
    }
    app.initRuntime(getFilesDir().getAbsolutePath(), stateSubdir);

    if (app.runtimeHandle() == 0L) {
      Log.e(TAG, "Runtime missing");
      return;
    }

    NativeRuntime.nativeSetPresentationRoot(
        app.runtimeHandle(), this, pageContainer);
    app.startModelOnce();

    final GestureDetector swipeDetector = new GestureDetector(this,
        new GestureDetector.SimpleOnGestureListener() {
          @Override
          public boolean onDown(MotionEvent event) {
            return true;
          }

          @Override
          public boolean onFling(MotionEvent down, MotionEvent up,
                                 float velocityX, float velocityY) {
            float dx = up.getX() - down.getX();
            if (Math.abs(dx) < SWIPE_MIN_DISTANCE_PX
                || Math.abs(dx) < Math.abs(up.getY() - down.getY())) {
              return false;
            }
            showPageProgrammatic(currentIndex + (dx < 0 ? 1 : -1), true);
            return true;
          }
        });

    pageContainer.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
      @Override
      public void onLayoutChange(View v, int left, int top, int right,
                                 int bottom, int oldLeft, int oldTop,
                                 int oldRight, int oldBottom) {
        if (right - left != oldRight - oldLeft
            || bottom - top != oldBottom - oldTop) {
          v.post(new Runnable() {
            @Override
            public void run() {
              application().mainViewportChanged();
            }
          });
        }
      }
    });

    pageContainer.setOnTouchListener(new View.OnTouchListener() {
      @Override
      public boolean onTouch(View view, MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
          View target = findViewAt(view, (int) event.getX(), (int) event.getY());
          if (target != null && target != view && target.isClickable()) {
            return false;
          }
        }
        return swipeDetector.onTouchEvent(event);
      }
    });
  }

  private static View findViewAt(View root, int x, int y) {
    if (!(root instanceof ViewGroup)) {
      return root;
    }
    ViewGroup group = (ViewGroup) root;
    for (int i = group.getChildCount() - 1; i >= 0; --i) {
      View child = group.getChildAt(i);
      float cx = x - child.getLeft();
      float cy = y - child.getTop();
      if (cx < 0 || cy < 0 || cx >= child.getWidth() || cy >= child.getHeight()) {
        continue;
      }
      if (child instanceof ViewGroup) {
        View deeper = findViewAt(child, (int) cx, (int) cy);
        if (deeper != null) {
          return deeper;
        }
      }
      return child;
    }
    return root;
  }

  @Override
  protected void onDestroy() {
    application().flushPendingBackgroundSaves();
    application().clearActivity(this);
    super.onDestroy();
  }

  @Override
  protected void onStop() {
    application().flushPendingBackgroundSaves();
    super.onStop();
  }

  public void onMobilePages(long[] windowIds, long activeWindowId) {
    pageWindowIds = windowIds;
    reportedActiveId = activeWindowId;
    for (int i = 0; i < windowIds.length; ++i) {
      if (windowIds[i] == activeWindowId) {
        showPageProgrammatic(i, false);
        return;
      }
    }
    showPageProgrammatic(0, false);
  }

  private void showPageProgrammatic(int index, boolean fromUserSwipe) {
    if (pageWindowIds.length == 0) {
      return;
    }
    int count = pageWindowIds.length;
    if (count <= 1 && fromUserSwipe) {
      return;
    }
    int wrapped = index;
    if (count > 1) {
      wrapped = ((index % count) + count) % count;
    } else {
      wrapped = 0;
    }
    currentIndex = wrapped;
    loading.setVisibility(View.GONE);
    pageIndicator.setText((currentIndex + 1) + " / " + count);
    Log.i(TAG, "PAGE_SHOWN index=" + currentIndex + " id="
        + pageWindowIds[currentIndex] + " count=" + count);

    if (fromUserSwipe) {
      long pageId = pageWindowIds[currentIndex];
      if (pageId != reportedActiveId) {
        reportedActiveId = pageId;
        application().mobilePageShown(pageId);
      }
    } else {
      reportedActiveId = pageWindowIds[currentIndex];
    }
  }

  private WindowCatalogApplication application() {
    return (WindowCatalogApplication) getApplication();
  }
}
