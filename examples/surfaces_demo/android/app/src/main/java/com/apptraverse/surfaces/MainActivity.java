package com.apptraverse.surfaces;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

/**
 * Host Activity: one pager of Surface pages plus [ Add ] [ Remove current ].
 * The current page is presentation state only, it never reaches the model.
 * The page list comes from the native runtime after every publication.
 */
public final class MainActivity extends Activity implements NativeUiBridge.Listener {

  private static final String TAG = "AppTraverseSurfaces";
  private static final int SWIPE_MIN_DISTANCE_PX = 80;

  private TextView pageIndicator;
  private TextView surfaceTitle;
  private TextView loading;

  private long[] pageIds = new long[0];
  private int[] pageNumbers = new int[0];
  private int currentIndex;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    pageIndicator = findViewById(R.id.page_indicator);
    surfaceTitle = findViewById(R.id.surface_title);
    loading = findViewById(R.id.loading);

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

    View pageContainer = findViewById(R.id.page_container);
    pageContainer.setOnTouchListener(new View.OnTouchListener() {
      @Override
      public boolean onTouch(View view, MotionEvent event) {
        return swipeDetector.onTouchEvent(event);
      }
    });

    Log.i(TAG, "ACTIVITY_CREATED instance="
        + Integer.toHexString(System.identityHashCode(this)));
  }

  @Override
  protected void onStart() {
    super.onStart();
    application().uiBridge().attach(this);
  }

  @Override
  protected void onStop() {
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
  public void onPages(long[] ids, int[] numbers) {
    pageIds = ids;
    pageNumbers = numbers;
    // Structural removal keeps the user on a neighbour page.
    showPage(Math.min(currentIndex, numbers.length - 1));
  }

  @Override
  public void onModelStopped() {
    finishAndRemoveTask();
    System.exit(0);
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
  }

  private SurfacesApplication application() {
    return (SurfacesApplication) getApplication();
  }
}
