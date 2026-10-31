package com.apptraverse.minimal;

import android.content.Context;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.UiObject2;
import androidx.test.uiautomator.Until;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public final class InitialUiInstrumentedTest {

  private UiDevice device;

  private static final String PROFILE = "minimal_app_instrumented_initial_ui";

  @Before
  public void setUp() {
    device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    MinimalAppTestLaunch.launchFreshProfile(context, device, PROFILE);
  }

  @Test
  public void initialPublicationShowsMainControls() {
    UiObject2 apply = device.wait(Until.findObject(By.desc("Apply")), 15000);
    UiObject2 openDetail = device.wait(Until.findObject(By.desc("Open Detail")), 15000);
    UiObject2 rowA = device.wait(Until.findObject(By.desc("Row A")), 15000);
    UiObject2 rowB = device.wait(Until.findObject(By.desc("Row B")), 15000);

    assertNotNull(apply);
    assertNotNull(openDetail);
    assertNotNull(rowA);
    assertNotNull(rowB);
    assertTrue(apply.isEnabled());
    assertFalse(rowA.isSelected());
    assertFalse(rowB.isSelected());
    assertFalse(device.hasObject(By.desc("Detail Panel")));
  }
}
