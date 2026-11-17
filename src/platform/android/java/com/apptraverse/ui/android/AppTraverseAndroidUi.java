package com.apptraverse.ui.android;

import android.app.Activity;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.view.ContextThemeWrapper;
import android.content.res.Configuration;
import android.hardware.display.DisplayManager;
import android.view.Display;
import android.graphics.Point;
import android.provider.Settings;
import android.view.accessibility.AccessibilityManager;
import java.util.ArrayList;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.ScrollView;
import android.widget.HorizontalScrollView;
import android.view.Menu;
import android.database.ContentObserver;
import android.os.Handler;
import android.os.Looper;

/** JNI helpers for apptraverse_ui_android presenters (UI thread only). */
public final class AppTraverseAndroidUi {

  private AppTraverseAndroidUi() {}

  public static void setActivityTitle(Activity activity, String title) {
    activity.setTitle(title);
  }

  private static final class ClientMount extends FrameLayout {
    private long nativePresenter;
    private boolean refreshPosted;
    private HorizontalScrollView viewport;
    private int mode;
    private boolean soften;
    private final java.util.WeakHashMap<TextView, Float> textSizesSp = new java.util.WeakHashMap<>();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ContentObserver settings = new ContentObserver(handler) {
      @Override public void onChange(boolean selfChange) { refresh(); }
    };
    private final DisplayManager.DisplayListener displays = new DisplayManager.DisplayListener() {
      public void onDisplayAdded(int id) { refresh(); }
      public void onDisplayRemoved(int id) { refresh(); }
      public void onDisplayChanged(int id) { refresh(); }
    };
    private final AccessibilityManager.AccessibilityStateChangeListener access = enabled -> refresh();
    private final AccessibilityManager.TouchExplorationStateChangeListener exploration = enabled -> refresh();

    void refresh() {
      if (refreshPosted) return;
      refreshPosted = true;
      post(() -> {
        refreshPosted = false;
        if (nativePresenter != 0 && viewport.getWidth() > 0 && viewport.getHeight() > 0)
          nativeOnWindowClientSize(nativePresenter, viewport.getWidth(), viewport.getHeight(),
              getResources().getDisplayMetrics().densityDpi);
      });
    }

    ClientMount(Context context, long presenter) {
      super(context);
      nativePresenter = presenter;
      context.getContentResolver().registerContentObserver(Settings.System.getUriFor(Settings.System.FONT_SCALE), false, settings);
      context.getContentResolver().registerContentObserver(Settings.Global.getUriFor(Settings.Global.ANIMATOR_DURATION_SCALE), false, settings);
      ((DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE)).registerDisplayListener(displays, handler);
      AccessibilityManager manager = (AccessibilityManager) context.getSystemService(Context.ACCESSIBILITY_SERVICE);
      manager.addAccessibilityStateChangeListener(access);
      manager.addTouchExplorationStateChangeListener(exploration);
    }
    @Override protected void onConfigurationChanged(Configuration configuration) {
      super.onConfigurationChanged(configuration);
      for (java.util.Map.Entry<TextView, Float> item : textSizesSp.entrySet())
        item.getKey().setTextSize(TypedValue.COMPLEX_UNIT_SP, item.getValue());
      refresh();
    }
    void release() {
      nativePresenter = 0;
      getContext().getContentResolver().unregisterContentObserver(settings);
      ((DisplayManager) getContext().getSystemService(Context.DISPLAY_SERVICE)).unregisterDisplayListener(displays);
      AccessibilityManager manager = (AccessibilityManager) getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
      manager.removeAccessibilityStateChangeListener(access);
      manager.removeTouchExplorationStateChangeListener(exploration);
      detachFromParent(viewport);
    }
  }

  public static FrameLayout createClientMount(ViewGroup contentRoot, long nativePresenter) {
    Context context = contentRoot.getContext();
    ClientMount client = new ClientMount(context, nativePresenter);
    HorizontalScrollView horizontal = new HorizontalScrollView(context) {
      @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        client.refresh();
      }
    };
    client.viewport = horizontal;
    horizontal.setFillViewport(true);
    ScrollView vertical = new ScrollView(context);
    vertical.setFillViewport(true);
    vertical.addView(client, new ViewGroup.LayoutParams(1, 1));
    horizontal.addView(vertical, new ViewGroup.LayoutParams(1, ViewGroup.LayoutParams.MATCH_PARENT));
    contentRoot.addView(horizontal, new ViewGroup.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    return client;
  }

  public static void setClientMountVisible(FrameLayout client, boolean visible) {
    ((ClientMount) client).viewport.setVisibility(visible ? View.VISIBLE : View.GONE);
    if (visible) ((ClientMount) client).refresh();
  }

  public static void releaseClientMount(FrameLayout client) {
    ((ClientMount) client).release();
  }

  /** Native menu commands use the same presenter/proxy/Event path as controls. */
  public static void populateAppearanceMenu(ViewGroup root, Menu menu) {
    ClientMount client = visibleClient(root);
    if (client == null) return; // The loading screen has no native presenter yet.
    String[] names = {"Auto theme", "Light theme", "Dark theme"};
    for (int i = 0; i < names.length; ++i) {
      final int mode = i;
      menu.add(1, 100 + i, i, names[i]).setCheckable(true).setChecked(client.mode == i)
          .setOnMenuItemClickListener(item -> {
            nativeOnAppearanceChanged(client.nativePresenter, mode, client.soften);
            return true;
          });
    }
    menu.setGroupCheckable(1, true, true);
    menu.add(0, 103, 3, "Soften dark theme").setCheckable(true).setChecked(client.soften)
        .setOnMenuItemClickListener(item -> {
          nativeOnAppearanceChanged(client.nativePresenter, client.mode, !client.soften);
          return true;
        });
  }

  private static ClientMount visibleClient(ViewGroup group) {
    for (int i = 0; i < group.getChildCount(); ++i) {
      View child = group.getChildAt(i);
      if (child.getVisibility() != View.VISIBLE) continue;
      if (child instanceof ClientMount) return (ClientMount) child;
      if (child instanceof ViewGroup) {
        ClientMount found = visibleClient((ViewGroup) child);
        if (found != null) return found;
      }
    }
    return null;
  }

  private static float nativeTextSizeSp(Context context, int styleAttribute, TextView view) {
    android.content.res.TypedArray style = context.obtainStyledAttributes(
        null, new int[] {android.R.attr.textSize}, styleAttribute, 0);
    TypedValue size = style.peekValue(0);
    float sp = size != null && size.type == TypedValue.TYPE_DIMENSION
            && (size.data & TypedValue.COMPLEX_UNIT_MASK) == TypedValue.COMPLEX_UNIT_SP
        ? TypedValue.complexToFloat(size.data)
        : view.getTextSize() / context.getResources().getDisplayMetrics().scaledDensity;
    style.recycle();
    return sp;
  }

  private static final class TextBinding implements android.text.TextWatcher {
    long presenter;
    boolean applying, readOnly;
    android.text.method.KeyListener keyListener;
    TextBinding(long p) {
      presenter = p;
    }
    public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
    public void onTextChanged(CharSequence s, int start, int before, int count) {
      if (!applying && presenter != 0)
        nativeOnTextEdited(presenter, s.toString());
    }
    public void afterTextChanged(android.text.Editable e) {}
  }
  private static final java.util.WeakHashMap<View, TextBinding> textBindings =
      new java.util.WeakHashMap<>();
  public static View createTextControl(ViewGroup parent, int id, int kind, long presenter) {
    TextView text = kind == 0 ? new TextView(parent.getContext())
                              : new android.widget.EditText(parent.getContext());
    text.setTag(id);
    text.setTextIsSelectable(true);
    TextBinding binding = new TextBinding(presenter);
    textBindings.put(text, binding);
    if (text instanceof android.widget.EditText) {
      android.widget.EditText edit = (android.widget.EditText) text;
      edit.setSingleLine(kind == 1);
      binding.keyListener = edit.getKeyListener();
      edit.addTextChangedListener(binding);
      edit.setOnEditorActionListener((v, action, event) -> {
        if (kind == 1) {
          if (event == null
              || (event.getAction() == android.view.KeyEvent.ACTION_DOWN
                  && event.getRepeatCount() == 0))
            nativeOnTextAccepted(binding.presenter);
          return true;
        }
        return false;
      });
    }
    ((ClientMount) parent).textSizesSp.put(text,
        nativeTextSizeSp(parent.getContext(),
            kind == 0 ? android.R.attr.textViewStyle : android.R.attr.editTextStyle, text));
    parent.addView(text, new FrameLayout.LayoutParams(1, 1));
    refreshParent(text);
    return text;
  }
  public static void updateTextControl(View view, String value, String accessibleLabel,
      boolean enabled, boolean visible, boolean readOnly, boolean apply) {
    TextView text = (TextView) view;
    TextBinding binding = textBindings.get(view);
    binding.applying = true;
    try {
      CharSequence name = accessibleLabel.isEmpty() ? null : accessibleLabel;
      if (!android.text.TextUtils.equals(text.getContentDescription(), name))
        text.setContentDescription(name);
      if (apply && !text.getText().toString().equals(value)) {
        int a = text.getSelectionStart(), z = text.getSelectionEnd();
        text.setText(value);
        if (text instanceof android.widget.EditText && a >= 0 && z >= 0)
          ((android.widget.EditText) text)
              .setSelection(Math.min(a, value.length()), Math.min(z, value.length()));
      }
      text.setEnabled(enabled);
      text.setVisibility(visible ? View.VISIBLE : View.GONE);
      if (text instanceof android.widget.EditText) {
        android.widget.EditText edit = (android.widget.EditText) text;
        // Editing configuration changes only with the model's read-only state;
        // repeated publications must not restart a native composing session.
        if (binding.readOnly != readOnly) {
          edit.setShowSoftInputOnFocus(!readOnly);
          edit.setCursorVisible(!readOnly);
          edit.setKeyListener(readOnly ? null : binding.keyListener);
          binding.readOnly = readOnly;
        }
      }
    } finally {
      binding.applying = false;
    }
    refreshParent(text);
  }
  public static void releaseTextControl(View view) {
    TextBinding binding = textBindings.remove(view);
    binding.presenter = 0;
    if (view instanceof android.widget.EditText) {
      ((android.widget.EditText) view).removeTextChangedListener(binding);
      ((android.widget.EditText) view).setOnEditorActionListener(null);
    }
    detachFromParent(view);
  }
  private static native void nativeOnTextEdited(long presenter, String text);
  private static native void nativeOnTextAccepted(long presenter);

  private static final java.util.WeakHashMap<Button, Integer> focusTargets =
      new java.util.WeakHashMap<>();
  public static Button createPushButton(ViewGroup parent, int controlId, String label,
      boolean enabled, int x, int y, int width, int height, long nativePresenter) {
    Context context = parent.getContext();
    Button button = new Button(context);
    float sp = nativeTextSizeSp(context, android.R.attr.buttonStyle, button);
    ((ClientMount) parent).textSizesSp.put(button, sp);
    button.setText(label);
    button.setEnabled(enabled);
    button.setTag(controlId);
    button.setContentDescription(label);
    FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(width, height);
    params.leftMargin = x;
    params.topMargin = y;
    parent.addView(button, params);
    refreshParent(button);
    button.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View v) {
        nativeOnPushButtonClick(nativePresenter);
        int id = focusTargets.getOrDefault(button, 0);
        if (id != 0) {
          View target = parent.findViewWithTag(id);
          if (target != null)
            target.requestFocus();
        }
      }
    });
    return button;
  }

  public static void updatePushButton(
      Button button, String label, boolean enabled, boolean visible, int focusControl) {
    button.setText(label);
    button.setEnabled(enabled);
    button.setVisibility(visible ? View.VISIBLE : View.GONE);
    focusTargets.put(button, focusControl);
    button.setContentDescription(label);
    refreshParent(button);
  }

  public static void updatePushButtonLayout(Button button, int x, int y, int width, int height) {
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
    ViewParent parent = button.getParent();
    detachFromParent(button);
    if (parent instanceof ClientMount) ((ClientMount) parent).refresh();
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
    refreshParent(list);
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
      ((ClientMount) list.getParent()).textSizesSp.put(row, 16f);
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

    // Native theme attributes retain focused/pressed/selected feedback.
    row.setTextColor(themeColor(ctx, android.R.attr.textColorPrimary));
    TypedValue selectable = new TypedValue();
    ctx.getTheme().resolveAttribute(android.R.attr.selectableItemBackground, selectable, true);
    row.setBackgroundResource(selectable.resourceId);
    row.setActivated(selected);
    row.setTypeface(null, selected ? android.graphics.Typeface.BOLD : android.graphics.Typeface.NORMAL);
    row.setPadding((int) padPx, (int) padPx, (int) padPx, (int) padPx);
    refreshParent(list);
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

  private static int themeColor(Context context, int attr) {
    android.content.res.TypedArray a = context.obtainStyledAttributes(new int[] {attr});
    int value = a.getColor(0, 0); a.recycle(); return value;
  }

  private static void refreshParent(View view) {
    ViewParent parent = view.getParent();
    if (parent instanceof ClientMount) ((ClientMount) parent).refresh();
  }

  /** Measure native text widgets with platform SP/font scaling (including nonlinear scaling). */
  private static int minimumTextWidth(View view) {
    if (!(view instanceof TextView) || view instanceof android.widget.EditText)
      return 0;
    TextView text = (TextView) view;
    float width = 0;
    for (String word : text.getText().toString().split("\\s+"))
      width = Math.max(width, text.getPaint().measureText(word));
    return (int) Math.ceil(width) + view.getPaddingLeft() + view.getPaddingRight();
  }
  public static int[] measureControls(FrameLayout client) {
    int[] result = new int[client.getChildCount() * 5];
    for (int i = 0; i < client.getChildCount(); ++i) {
      View child = client.getChildAt(i);
      child.measure(View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED),
          View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
      int offset = i * 5;
      result[offset] = (Integer) child.getTag();
      result[offset + 1] =
          child instanceof Button ? child.getMinimumWidth() : minimumTextWidth(child);
      result[offset + 2] = child instanceof android.widget.EditText
          ? Math.max(child.getMinimumHeight(),
                ((TextView) child).getLineHeight() + child.getPaddingTop()
                    + child.getPaddingBottom())
          : child instanceof TextView
          ? Math.max(child.getMinimumHeight(), child.getMeasuredHeight())
          : 0;
      result[offset + 3] = child instanceof Button ? child.getMeasuredWidth() : 0;
      result[offset + 4] = result[offset + 2];
    }
    return result;
  }

  public static int measureControlHeight(FrameLayout client, int id, int width) {
    View child = client.findViewWithTag(id);
    child.measure(View.MeasureSpec.makeMeasureSpec(Math.max(1, width), View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
    return child instanceof android.widget.EditText
        ? Math.max(child.getMinimumHeight(),
              ((TextView) child).getLineHeight() + child.getPaddingTop() + child.getPaddingBottom())
        : child instanceof TextView ? Math.max(child.getMinimumHeight(), child.getMeasuredHeight())
                                    : 0;
  }

  public static void applyControlRects(FrameLayout client, int[] rects, int contentWidth, int contentHeight) {
    ViewGroup.LayoutParams content = client.getLayoutParams();
    if (content.width != contentWidth || content.height != contentHeight) {
      content.width = contentWidth; content.height = contentHeight; client.setLayoutParams(content);
      View vertical = (View) client.getParent();
      ViewGroup.LayoutParams outer = vertical.getLayoutParams();
      outer.width = contentWidth; vertical.setLayoutParams(outer);
    }
    for (int i = 0; i < rects.length; i += 5) {
      View child = client.findViewWithTag(rects[i]);
      FrameLayout.LayoutParams frame = (FrameLayout.LayoutParams) child.getLayoutParams();
      if (frame.leftMargin == rects[i+1] && frame.topMargin == rects[i+2] &&
          frame.width == rects[i+3] && frame.height == rects[i+4]) continue;
      frame.leftMargin = rects[i+1]; frame.topMargin = rects[i+2];
      frame.width = rects[i+3]; frame.height = rects[i+4]; child.setLayoutParams(frame);
    }
  }

  public static String[] deviceEnvironment(Activity activity) {
    ArrayList<String> values = new ArrayList<>();
    Configuration config = activity.getResources().getConfiguration();
    android.util.DisplayMetrics metrics = activity.getResources().getDisplayMetrics();
    AccessibilityManager access = (AccessibilityManager) activity.getSystemService(Context.ACCESSIBILITY_SERVICE);
    values.add(Integer.toString(metrics.densityDpi));
    values.add(Integer.toString(Math.round(config.fontScale * 1000)));
    values.add((config.uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES ? "1" : "0");
    values.add(Settings.Global.getFloat(activity.getContentResolver(), Settings.Global.ANIMATOR_DURATION_SCALE, 1) == 0 ? "1" : "0");
    values.add(access.isTouchExplorationEnabled() ? "1" : "0");
    Context systemTheme = new ContextThemeWrapper(activity,
        (config.uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES
            ? android.R.style.Theme_Material : android.R.style.Theme_Material_Light);
    values.add(Integer.toString(themeColor(systemTheme, android.R.attr.colorBackground)));
    values.add(Integer.toString(themeColor(systemTheme, android.R.attr.textColorPrimary)));
    Display[] displays = ((DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE)).getDisplays();
    values.add(Integer.toString(displays.length));
    for (Display display : displays) {
      Point size = new Point(); display.getRealSize(size);
      android.graphics.Rect work = new android.graphics.Rect(); display.getRectSize(work);
      values.add("android-display:" + display.getDisplayId() + ":" + display.getName());
      values.add(Integer.toString(size.x)); values.add(Integer.toString(size.y));
      values.add(Integer.toString(work.width())); values.add(Integer.toString(work.height()));
      values.add(display.getDisplayId() == Display.DEFAULT_DISPLAY ? "1" : "0");
      values.add(Integer.toString(display.getRotation()));
      values.add(Integer.toString(Math.round(display.getRefreshRate())));
    }
    return values.toArray(new String[0]);
  }

  public static void applyAppearance(FrameLayout client, int mode, boolean soften) {
    ((ClientMount) client).mode = mode;
    ((ClientMount) client).soften = soften;
    boolean dark = mode == 2 || (mode == 0 &&
        (client.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES);
    Context theme = new ContextThemeWrapper(client.getContext(), dark ? android.R.style.Theme_Material : android.R.style.Theme_Material_Light);
    client.setBackgroundColor(dark && soften ? Color.rgb(32, 32, 32) : themeColor(theme, android.R.attr.colorBackground));
    applyTextAppearance(client, theme, dark && soften);
  }

  private static void applyTextAppearance(ViewGroup group, Context theme, boolean soften) {
    for (int i = 0; i < group.getChildCount(); ++i) {
      View child = group.getChildAt(i);
      if (child instanceof TextView) {
        android.content.res.TypedArray colors = theme.obtainStyledAttributes(new int[] {
            android.R.attr.textColorPrimary, android.R.attr.colorButtonNormal});
        ColorStateList foreground = colors.getColorStateList(0);
        if (soften) {
          // Only the enabled foreground is softened. Disabled controls retain
          // the native theme's state color (including its disabled alpha).
          int disabled = foreground.getColorForState(
              new int[] {-android.R.attr.state_enabled}, foreground.getDefaultColor());
          foreground = new ColorStateList(
              new int[][] {new int[] {-android.R.attr.state_enabled}, new int[] {}},
              new int[] {disabled, Color.rgb(222, 222, 222)});
        }
        ((TextView) child).setTextColor(foreground);
        if (child instanceof Button) child.setBackgroundTintList(colors.getColorStateList(1));
        colors.recycle();
      }
      if (child instanceof ViewGroup) applyTextAppearance((ViewGroup) child, theme, soften);
    }
  }

  private static void detachFromParent(View view) {
    ViewParent parent = view.getParent();
    if (parent instanceof ViewGroup) {
      ((ViewGroup) parent).removeView(view);
    }
  }

  private static native void nativeOnAppearanceChanged(long nativePresenter, int mode, boolean soften);

  private static native void nativeOnPushButtonClick(long nativePresenter);

  private static native void nativeOnListSelection(long nativePresenter, int rowIndex);

  private static native void nativeOnWindowClientSize(long nativePresenter,
      int width, int height, int densityDpi);
}
