package com.apptraverse.surfaces;

import android.app.Activity;
import android.content.res.Configuration;
import android.os.Bundle;
import android.util.Log;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Host Activity: one pager of Surface pages plus [ Add ] [ Remove current ].
 * Which page becomes current is presentation policy; the page it settles on is
 * reported to the model so a restart reopens it. The page list and the
 * persisted current page come from the native runtime after every publication.
 *
 * Screen rotation / host resize reports usable presentation size to the model
 * Event path. Button row orientation follows SurfacePresenter::IsWide from the
 * published mirror — not Android's orientation enum.
 */
public final class MainActivity extends Activity implements NativeUiBridge.Listener {

  private static final String TAG = "AppTraverseSurfaces";
  private static final int SWIPE_MIN_DISTANCE_PX = 80;

  private TextView pageIndicator;
  private TextView surfaceTitle;
  private TextView loading;
  private LinearLayout controlsRow;
  private View rootContent;
  private View pageContainer;

  private long[] pageIds = new long[0];
  private int[] pageNumbers = new int[0];
  private int currentIndex;
  // Last page id the model knows about, so the publication it triggers does
  // not report the same page back.
  private long reportedCurrentId;
  private boolean controlsWide = true;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    pageIndicator = findViewById(R.id.page_indicator);
    surfaceTitle = findViewById(R.id.surface_title);
    loading = findViewById(R.id.loading);
    controlsRow = findViewById(R.id.controls_row);
    pageContainer = findViewById(R.id.page_container);
    rootContent = findViewById(android.R.id.content);

    Button addButton = findViewById(R.id.add_surface);
    addButton.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        if (pageIds.length == 0) {
          return;
        }
        application().addFrom(pageIds[currentIndex]);
      }
    });

    Button removeButton = findViewById(R.id.remove_current);
    removeButton.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        if (pageIds.length == 0) {
          return;
        }
        application().removeSurface(pageIds[currentIndex]);
      }
    });

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
            showPage(dx < 0 ? currentIndex + 1 : currentIndex - 1);
            return true;
          }
        });

    pageContainer.setOnTouchListener(new View.OnTouchListener() {
      @Override
      public boolean onTouch(View view, MotionEvent event) {
        return swipeDetector.onTouchEvent(event);
      }
    });

    // Measure the host content area (not page_container): control orientation
    // must not feed back into presentation size.
    rootContent.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
      @Override
      public void onLayoutChange(View view, int left, int top, int right,
                                 int bottom, int oldLeft, int oldTop,
                                 int oldRight, int oldBottom) {
        reportHostPresentationSize(right - left, bottom - top);
      }
    });

    applyControlsOrientation(controlsWide);
    Log.i(TAG, "ACTIVITY_CREATED instance="
        + Integer.toHexString(System.identityHashCode(this)));
  }

  @Override
  public void onConfigurationChanged(Configuration newConfig) {
    super.onConfigurationChanged(newConfig);
    // Rotation is handled here (manifest configChanges). Do not switch
    // controls from newConfig.orientation — only report the new host size;
    // IsWide after publication rebuilds the UI.
    rootContent.requestLayout();
    rootContent.post(new Runnable() {
      @Override
      public void run() {
        reportHostPresentationSize(rootContent.getWidth(),
            rootContent.getHeight());
      }
    });
  }

  @Override
  protected void onStart() {
    super.onStart();
    application().uiBridge().attach(this);
  }

  @Override
  protected void onStop() {
    // Last reliable Android edge before a background kill: checkpoint topology
    // and mobile_current. Back still Saves inside the model Run drain.
    if (!isFinishing()) {
      application().persistState();
    }
    application().uiBridge().detach(this);
    super.onStop();
  }

  @Override
  public void onBackPressed() {
    // Back closes the whole application, it never removes a Surface. The
    // Activity finishes only after the model thread saved and stopped.
    Log.i(TAG, "BACK_REQUESTED_STOP");
    application().requestStop();
  }

  @Override
  public void onPages(long[] ids, int[] numbers, long currentId, boolean isWide) {
    pageIds = ids;
    pageNumbers = numbers;
    reportedCurrentId = currentId;
    if (isWide != controlsWide) {
      applyControlsOrientation(isWide);
    }
    // Initial publication may land before the first layout; report again once
    // the host is ready so the first size Event is not dropped.
    reportHostPresentationSize(rootContent.getWidth(), rootContent.getHeight());
    for (int i = 0; i < ids.length; ++i) {
      if (ids[i] == currentId) {
        showPage(i);
        return;
      }
    }
    // No persisted current page, or it was just removed: keep the user on a
    // neighbour page and report that choice.
    showPage(Math.min(currentIndex, numbers.length - 1));
  }

  @Override
  public void onModelStopped() {
    finishAndRemoveTask();
    System.exit(0);
  }

  private void reportHostPresentationSize(int width, int height) {
    if (width <= 0 || height <= 0) {
      return;
    }
    application().reportPresentationSize(width, height);
  }

  private void applyControlsOrientation(boolean isWide) {
    controlsWide = isWide;
    controlsRow.setOrientation(
        isWide ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
    int width = isWide ? 0 : LinearLayout.LayoutParams.MATCH_PARENT;
    float weight = isWide ? 1.f : 0.f;
    for (int i = 0; i < controlsRow.getChildCount(); ++i) {
      View child = controlsRow.getChildAt(i);
      LinearLayout.LayoutParams params =
          (LinearLayout.LayoutParams) child.getLayoutParams();
      params.width = width;
      params.height = LinearLayout.LayoutParams.WRAP_CONTENT;
      params.weight = weight;
      child.setLayoutParams(params);
    }
    Log.i(TAG, "CONTROLS_ORIENTATION wide=" + (isWide ? "1" : "0"));
  }

  private void showPage(int index) {
    if (pageNumbers.length == 0) {
      return;
    }
    currentIndex = Math.max(0, Math.min(index, pageNumbers.length - 1));
    loading.setVisibility(View.GONE);
    surfaceTitle.setText("Surface " + pageNumbers[currentIndex]);
    pageIndicator.setText((currentIndex + 1) + " / " + pageNumbers.length);
    Log.i(TAG, "PAGE_SHOWN number=" + pageNumbers[currentIndex]
        + " index=" + currentIndex + " count=" + pageNumbers.length);

    if (pageIds[currentIndex] != reportedCurrentId) {
      reportedCurrentId = pageIds[currentIndex];
      application().pageShown(reportedCurrentId);
    }
  }

  private SurfacesApplication application() {
    return (SurfacesApplication) getApplication();
  }
}
