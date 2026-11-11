package com.apptraverse.ui.android;

import android.app.Activity;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/** JNI helpers for apptraverse_ui_android presenters (UI thread only). */
public final class AppTraverseAndroidUi {

  private AppTraverseAndroidUi() {}

  public static void setActivityTitle(Activity activity, String title) {
    activity.setTitle(title);
  }

  private static final class ClientMount extends FrameLayout {
    private long nativePresenter;

    ClientMount(Context context, long presenter) {
      super(context);
      nativePresenter = presenter;
    }

    @Override protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
      super.onSizeChanged(width, height, oldWidth, oldHeight);
      if (nativePresenter != 0 && width > 0 && height > 0) {
        nativeOnWindowClientSize(nativePresenter, width, height,
            getResources().getDisplayMetrics().densityDpi);
      }
    }
  }

  public static FrameLayout createClientMount(ViewGroup contentRoot, long nativePresenter) {
    Context context = contentRoot.getContext();
    FrameLayout client = new ClientMount(context, nativePresenter);
    client.setLayoutParams(new ViewGroup.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT));
    contentRoot.addView(client);
    return client;
  }

  /** Detaches the presenter-owned client mount; the host content root stays. */
  public static void releaseClientMount(FrameLayout client) {
    ((ClientMount) client).nativePresenter = 0;
    detachFromParent(client);
  }

  public static Button createPushButton(ViewGroup parent, int controlId, String label,
      boolean enabled, int x, int y, int width, int height, long nativePresenter) {
    Context context = parent.getContext();
    Button button = new Button(context);
    button.setText(label);
    button.setEnabled(enabled);
    button.setTag(controlId);
    button.setContentDescription(label);
    FrameLayout.LayoutParams params =
        new FrameLayout.LayoutParams(width, height);
    params.leftMargin = x;
    params.topMargin = y;
    parent.addView(button, params);
    button.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View v) {
        nativeOnPushButtonClick(nativePresenter);
      }
    });
    return button;
  }

  public static void updatePushButton(Button button, String label, boolean enabled) {
    button.setText(label);
    button.setEnabled(enabled);
    button.setContentDescription(label);
  }

  public static void updatePushButtonLayout(Button button, int x, int y, int width,
      int height) {
    FrameLayout.LayoutParams frame = (FrameLayout.LayoutParams) button.getLayoutParams();
    frame.leftMargin = x;
    frame.topMargin = y;
    frame.width = width;
    frame.height = height;
    button.setLayoutParams(frame);
  }

  public static void updateListContainerLayout(LinearLayout list, int x, int y,
      int width, int height) {
    FrameLayout.LayoutParams frame = (FrameLayout.LayoutParams) list.getLayoutParams();
    frame.leftMargin = x;
    frame.topMargin = y;
    frame.width = width;
    frame.height = height;
    list.setLayoutParams(frame);
  }

  /** Content size of a view in physical pixels (measures against parent if needed). */
  public static int[] contentSizeForView(View view) {
    int width = view.getWidth();
    int height = view.getHeight();
    if (width > 0 && height > 0) {
      return new int[] {width, height};
    }
    int parentWidth = 0;
    int parentHeight = 0;
    ViewParent parent = view.getParent();
    if (parent instanceof View) {
      View parentView = (View) parent;
      parentWidth = parentView.getWidth();
      parentHeight = parentView.getHeight();
    }
    if (parentWidth <= 0 || parentHeight <= 0) {
      parentWidth = view.getResources().getDisplayMetrics().widthPixels;
      parentHeight = view.getResources().getDisplayMetrics().heightPixels;
    }
    view.measure(
        View.MeasureSpec.makeMeasureSpec(parentWidth, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(parentHeight, View.MeasureSpec.EXACTLY));
    return new int[] {view.getMeasuredWidth(), view.getMeasuredHeight()};
  }

  public static void releasePushButton(Button button) {
    button.setOnClickListener(null);
    detachFromParent(button);
  }

  public static LinearLayout createListContainer(ViewGroup parent, int controlId, int x,
      int y, int width, int height) {
    Context context = parent.getContext();
    LinearLayout list = new LinearLayout(context);
    list.setOrientation(LinearLayout.VERTICAL);
    list.setTag(controlId);
    FrameLayout.LayoutParams params =
        new FrameLayout.LayoutParams(width, height);
    params.leftMargin = x;
    params.topMargin = y;
    parent.addView(list, params);
    return list;
  }

  public static void bindListRow(LinearLayout list, int rowIndex, String label,
      boolean selected, long nativePresenter) {
    Context ctx = list.getContext();
    float padPx = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 12f,
        ctx.getResources().getDisplayMetrics());
    TextView row;
    if (rowIndex < list.getChildCount()) {
      row = (TextView) list.getChildAt(rowIndex);
    } else {
      row = new TextView(ctx);
      row.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
      row.setClickable(true);
      row.setFocusable(true);
      list.addView(row, new LinearLayout.LayoutParams(
          ViewGroup.LayoutParams.MATCH_PARENT,
          ViewGroup.LayoutParams.WRAP_CONTENT));
    }
    row.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View v) {
        int index = list.indexOfChild(v);
        if (index >= 0) {
          nativeOnListSelection(nativePresenter, index);
        }
      }
    });
    row.setText(label);
    row.setContentDescription(label);
    row.setSelected(selected);

    // Visible selection from mirror `selected` (no separate Java selection state).
    int fgSelected = Color.parseColor("#FFFFFF");
    int fgNormal = Color.parseColor("#E8EAF0");
    row.setTextColor(new ColorStateList(
        new int[][] {
            new int[] {android.R.attr.state_selected},
            new int[] {}
        },
        new int[] {fgSelected, fgNormal}));

    GradientDrawable bg = new GradientDrawable();
    bg.setShape(GradientDrawable.RECTANGLE);
    bg.setCornerRadius(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 6f,
        ctx.getResources().getDisplayMetrics()));
    if (selected) {
      bg.setColor(Color.parseColor("#1E3A5F"));
      bg.setStroke((int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 1f,
          ctx.getResources().getDisplayMetrics()),
          Color.parseColor("#4A90D9"));
    } else {
      bg.setColor(Color.parseColor("#2B2F38"));
      bg.setStroke(0, Color.TRANSPARENT);
    }
    row.setBackground(bg);
    row.setPadding((int) padPx, (int) padPx, (int) padPx, (int) padPx);
  }

  public static void clearListRows(LinearLayout list, int keepCount) {
    while (list.getChildCount() > keepCount) {
      int last = list.getChildCount() - 1;
      list.getChildAt(last).setOnClickListener(null);
      list.removeViewAt(last);
    }
  }

  public static void releaseListContainer(LinearLayout list) {
    clearListRows(list, 0);
    detachFromParent(list);
  }

  private static void detachFromParent(View view) {
    ViewParent parent = view.getParent();
    if (parent instanceof ViewGroup) {
      ((ViewGroup) parent).removeView(view);
    }
  }

  private static native void nativeOnPushButtonClick(long nativePresenter);

  private static native void nativeOnListSelection(long nativePresenter, int rowIndex);

  private static native void nativeOnWindowClientSize(long nativePresenter,
      int width, int height, int densityDpi);
}
