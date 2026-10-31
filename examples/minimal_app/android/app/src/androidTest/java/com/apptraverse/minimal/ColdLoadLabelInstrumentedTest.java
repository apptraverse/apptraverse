package com.apptraverse.minimal;

import android.content.Context;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public final class ColdLoadLabelInstrumentedTest {

  @Test
  public void coldLoadPreservesOpenerLabelBeforePrepare() {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    String profileDir =
        context.getFilesDir().getAbsolutePath() + "/a1r1_cold_load_probe";
    String result = NativeRuntime.nativeRunColdLoadLabelProbe(profileDir);
    assertTrue("cold load probe: " + result, result.startsWith("OK:"));
  }
}
