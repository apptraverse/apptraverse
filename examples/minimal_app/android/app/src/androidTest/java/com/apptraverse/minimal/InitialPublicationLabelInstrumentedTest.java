package com.apptraverse.minimal;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public final class InitialPublicationLabelInstrumentedTest {

  @Test
  public void productionColdLoadInitialPublicationHasOpenerLabel() {
    String profileDir = "/data/data/com.apptraverse.minimal/files/a1r1_initial_pub_probe";
    String result = NativeRuntime.nativeRunInitialPublicationLabelProbe(profileDir);
    assertTrue("probe: " + result, result.startsWith("OK:"));
  }
}
