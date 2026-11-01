package com.apptraverse.minimal;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertTrue;

/** Deterministic StartModelThread wait vs fast worker entry (native probe). */
@RunWith(AndroidJUnit4.class)
public final class ModelThreadStartSyncInstrumentedTest {

  static {
    InstrumentedTestNative.ensureLoaded();
  }

  @Test
  public void startModelThreadUnblocksOnWorkerEntry() {
    String result = NativeRuntime.nativeRunModelThreadStartSyncProbe();
    assertTrue("probe: " + result, result.startsWith("OK:"));
  }
}
