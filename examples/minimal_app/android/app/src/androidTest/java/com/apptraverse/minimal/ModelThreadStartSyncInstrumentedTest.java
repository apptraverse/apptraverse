package com.apptraverse.minimal;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import android.content.Context;
import android.widget.FrameLayout;
import java.io.File;

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
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    File stateDir = new File(context.getCacheDir(), "model-thread-start-" + System.nanoTime());
    assertTrue("Create isolated probe profile", stateDir.mkdirs());
    String[] result = new String[1];
    InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
      FrameLayout root = new FrameLayout(context);
      result[0] = NativeTestRuntime.nativeRunModelThreadStartSyncProbe(
          root, stateDir.getAbsolutePath(), new NativeUiBridge());
      assertTrue("Scheduling probe must not mount native Views", root.getChildCount() == 0);
    });
    assertTrue("probe: " + result[0], result[0] != null && result[0].startsWith("OK:"));
  }
}
