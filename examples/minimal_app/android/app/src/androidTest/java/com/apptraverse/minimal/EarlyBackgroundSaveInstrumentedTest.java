package com.apptraverse.minimal;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertTrue;

/** onStop before model_thread_live_: pending checkpoint flushed when worker goes live. */
@RunWith(AndroidJUnit4.class)
public final class EarlyBackgroundSaveInstrumentedTest {

  @Test
  public void earlyBackgroundSavePendingFlushedWhenModelLive() {
    String result = NativeRuntime.nativeRunEarlyBackgroundSaveProbe();
    assertTrue("probe: " + result, result.startsWith("OK:"));
  }
}
