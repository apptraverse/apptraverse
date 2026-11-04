package com.apptraverse.minimal;

import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.ArrayList;
import java.util.List;

import static org.junit.Assert.assertTrue;

/**
 * Android presenter teardown through a real ApplicationUiSession: Views left behind by an
 * unloaded presenter or a removed list row must not send model commands, and the window client
 * mount must leave the host content root.
 */
@RunWith(AndroidJUnit4.class)
public final class PresenterLifecycleInstrumentedTest {

  private static final int APPLY_BUTTON_TAG = 7101;
  private static final int ROW_LIST_TAG = 7102;
  private static final int REMOUNT_CYCLES = 3;

  private static final class Controls {
    View mount;
    View button;
    ViewGroup list;
    final List<View> rows = new ArrayList<>();
  }

  private final List<String> violations = new ArrayList<>();
  private final List<Controls> released = new ArrayList<>();
  private FrameLayout contentRoot;
  private long handle;

  private void expect(boolean condition, String what) {
    if (!condition) {
      violations.add(what);
    }
  }

  private int enqueued() {
    return PresenterLifecycleFixture.nativeEnqueueCount(handle);
  }

  private Controls currentControls(String stage, int expectedRows) {
    Controls c = new Controls();
    expect(contentRoot.getChildCount() == 1,
        stage + ": content root children=" + contentRoot.getChildCount() + " expected 1");
    c.mount = contentRoot.getChildAt(contentRoot.getChildCount() - 1);
    c.button = c.mount.findViewWithTag(APPLY_BUTTON_TAG);
    c.list = (ViewGroup) c.mount.findViewWithTag(ROW_LIST_TAG);
    expect(c.list.getChildCount() == expectedRows,
        stage + ": rows=" + c.list.getChildCount() + " expected " + expectedRows);
    for (int i = 0; i < c.list.getChildCount(); ++i) {
      c.rows.add(c.list.getChildAt(i));
    }
    return c;
  }

  private void expectClickSends(View view, int commands, String what) {
    int before = enqueued();
    view.performClick();
    int sent = enqueued() - before;
    expect(sent == commands, what + ": sent " + sent + " command(s), expected " + commands);
  }

  private void expectLive(Controls c, String stage) {
    expectClickSends(c.button, 1, stage + " button");
    for (int i = 0; i < c.rows.size(); ++i) {
      expectClickSends(c.rows.get(i), 1, stage + " row " + i);
    }
  }

  private void expectSilent(Controls c, String stage) {
    expectClickSends(c.button, 0, stage + " old button");
    for (int i = 0; i < c.rows.size(); ++i) {
      expectClickSends(c.rows.get(i), 0, stage + " old row " + i);
    }
  }

  private void expectReleased(Controls c, String stage) {
    expect(c.mount.getParent() == null, stage + ": old client mount still attached");
    expect(!c.button.hasOnClickListeners(), stage + ": old button keeps OnClickListener");
    for (int i = 0; i < c.rows.size(); ++i) {
      expect(!c.rows.get(i).hasOnClickListeners(),
          stage + ": old row " + i + " keeps OnClickListener");
    }
  }

  private Controls remount(Controls previous, String stage, int rows) {
    PresenterLifecycleFixture.nativeRemount(handle);
    Controls next = currentControls(stage, rows);
    expect(next.mount != previous.mount, stage + ": client mount not recreated");
    expectSilent(previous, stage);
    expectReleased(previous, stage);
    expectLive(next, stage + " new");
    released.add(previous);
    return next;
  }

  private void destroy(Controls current, String stage) {
    PresenterLifecycleFixture.nativeDestroy(handle);
    handle = 0;
    expect(contentRoot.getChildCount() == 0,
        stage + ": content root children=" + contentRoot.getChildCount() + " expected 0");
    released.add(current);
    for (int i = 0; i < released.size(); ++i) {
      expectReleased(released.get(i), stage + " saved controls #" + i);
    }
  }

  private void runLifecycleScenario() {
    contentRoot = new FrameLayout(
        InstrumentationRegistry.getInstrumentation().getTargetContext());

    handle = PresenterLifecycleFixture.nativeCreate(contentRoot);
    Controls current = currentControls("initial", 2);
    expectLive(current, "initial");

    current = remount(current, "remount", 2);

    View removedRow = current.rows.get(1);
    PresenterLifecycleFixture.nativeRemoveMainListRow(handle, 1);
    expect(current.list.getChildCount() == 1,
        "shrink: rows=" + current.list.getChildCount() + " expected 1");
    expect(removedRow.getParent() == null, "shrink: removed row still attached");
    expect(!removedRow.hasOnClickListeners(), "shrink: removed row keeps OnClickListener");
    expectClickSends(removedRow, 0, "shrink removed row");
    current.rows.remove(1);
    expect(current.list.getChildAt(0) == current.rows.get(0), "shrink: row 0 view replaced");
    expectLive(current, "shrink remaining");

    for (int cycle = 0; cycle < REMOUNT_CYCLES; ++cycle) {
      current = remount(current, "cycle " + cycle, 1);
    }
    destroy(current, "session end");

    released.clear();
    handle = PresenterLifecycleFixture.nativeCreate(contentRoot);
    current = currentControls("recreate", 2);
    expectLive(current, "recreate");
    destroy(current, "recreate end");
  }

  @Test
  public void presenterTeardownReleasesListenersAndViews() {
    InstrumentationRegistry.getInstrumentation().runOnMainSync(this::runLifecycleScenario);
    assertTrue("violations:\n" + String.join("\n", violations), violations.isEmpty());
  }
}
