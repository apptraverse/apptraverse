package com.apptraverse.minimal;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.uiautomator.By;
import androidx.test.uiautomator.UiDevice;
import androidx.test.uiautomator.Until;

import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;

@RunWith(AndroidJUnit4.class)
public final class DetailRemountInstrumentedTest {
  @Test
  public void activityRecreationKeepsActiveDetailAtSameViewport() throws Exception {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    UiDevice device = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
    String profile = "minimal_detail_remount";
    MinimalAppTestLaunch.launchFreshProfile(context, device, profile);
    int pid = MinimalAppTestLaunch.readDemoProcessPidStrict(context, device, -1);

    assertNotNull(device.wait(Until.findObject(By.desc("Open Detail")), 15000));
    device.findObject(By.desc("Open Detail")).click();
    assertNotNull(device.wait(Until.findObject(By.desc("Close Detail")), 15000));
    assertFalse(device.hasObject(By.desc("Open Detail")));

    Intent recreate = new Intent(Intent.ACTION_MAIN);
    recreate.setComponent(new ComponentName("com.apptraverse.minimal",
        "com.apptraverse.minimal.MainActivity"));
    recreate.addCategory(Intent.CATEGORY_LAUNCHER);
    recreate.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
    recreate.putExtra(MinimalAppStateDirs.INTENT_EXTRA_STATE_SUBDIR, profile);
    recreate.putExtra(MinimalAppStateDirs.INTENT_EXTRA_SKIP_CHECKPOINT_SAVE, true);
    context.startActivity(recreate);

    assertEquals(pid, MinimalAppTestLaunch.readDemoProcessPidStrict(context, device, -1));
    assertNotNull(device.wait(Until.findObject(By.desc("Close Detail")), 15000));
    assertFalse(device.hasObject(By.desc("Open Detail")));
  }
}
