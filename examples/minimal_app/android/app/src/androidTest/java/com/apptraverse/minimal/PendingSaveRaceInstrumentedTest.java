package com.apptraverse.minimal;

import android.content.Context;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertTrue;

/** Deterministic pending-background-save ordering (native host probe). */
@RunWith(AndroidJUnit4.class)
public final class PendingSaveRaceInstrumentedTest {

  static {
    InstrumentedTestNative.ensureLoaded();
  }

  @Before
  public void setUp() {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    NativeTestRuntime.nativeSetCheckpointPersistTestGateRoot(
        context.getFilesDir().getAbsolutePath());
  }

  @After
  public void tearDown() {
    NativeTestRuntime.nativeDisableCheckpointPersistTestGate();
  }

  @Test
  public void pendingSaveDuringFirstPersistRunsSecondCheckpoint() {
    String result = NativeTestRuntime.nativeRunPendingBackgroundSaveRaceProbe();
    assertTrue("probe: " + result, result.startsWith("OK:"));
  }
}
