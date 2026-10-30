package com.apptraverse.ui.android;

import android.app.Activity;
import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
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

  public static FrameLayout createClientMount(ViewGroup contentRoot) {
    Context context = contentRoot.getContext();
    FrameLayout client = new FrameLayout(context);
    client.setLayoutParams(new ViewGroup.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT));
    contentRoot.addView(client);
    return client;
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
    TextView row;
    if (rowIndex < list.getChildCount()) {
      row = (TextView) list.getChildAt(rowIndex);
    } else {
      row = new TextView(list.getContext());
      list.addView(row, new LinearLayout.LayoutParams(
          ViewGroup.LayoutParams.MATCH_PARENT,
          ViewGroup.LayoutParams.WRAP_CONTENT));
      row.setOnClickListener(new View.OnClickListener() {
        @Override
        public void onClick(View v) {
          nativeOnListSelection(nativePresenter, rowIndex);
        }
      });
    }
    row.setText(label);
    row.setContentDescription(label);
    row.setSelected(selected);
  }

  public static void clearListRows(LinearLayout list, int keepCount) {
    while (list.getChildCount() > keepCount) {
      list.removeViewAt(list.getChildCount() - 1);
    }
  }

  private static native void nativeOnPushButtonClick(long nativePresenter);

  private static native void nativeOnListSelection(long nativePresenter, int rowIndex);
}
