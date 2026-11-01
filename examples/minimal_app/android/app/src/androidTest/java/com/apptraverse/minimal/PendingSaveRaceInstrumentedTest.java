package com.apptraverse.minimal;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertTrue;

/** Deterministic pending-background-save ordering (native host probe). */
@RunWith(AndroidJUnit4.class)
public final class PendingSaveRaceInstrumentedTest {

  @After
  public void tearDown() {
    NativeRuntime.nativeDisableCheckpointPersistTestGate();
  }

  @Test
  public void pendingSaveDuringFirstPersistRunsSecondCheckpoint() {
    String result = NativeRuntime.nativeRunPendingBackgroundSaveRaceProbe();
    assertTrue("probe: " + result, result.startsWith("OK:"));
  }
}
