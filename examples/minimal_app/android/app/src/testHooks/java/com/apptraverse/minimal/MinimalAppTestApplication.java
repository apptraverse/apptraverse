package com.apptraverse.minimal;

import java.lang.reflect.Field;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

/** Lifecycle observation and cold-reload controls belong to the explicit test variant. */
public final class MinimalAppTestApplication extends MinimalAppApplication {
  public interface BackgroundSaveObserver {
    void onBackgroundSaveFinished(int persistResult);
  }
  private final AtomicReference<BackgroundSaveObserver> observer = new AtomicReference<>();
  private final AtomicReference<CountDownLatch> latch = new AtomicReference<>();
  @Override public void onCreate() {
    super.onCreate();
    NativeTestRuntime.nativeSetCheckpointPersistTestGateRoot(getFilesDir().getAbsolutePath());
  }
  void setBackgroundSaveLatch(CountDownLatch value) { latch.set(value); }
  void clearBackgroundSaveLatch() { latch.set(null); }
  void setBackgroundSaveObserver(BackgroundSaveObserver value) { observer.set(value); }
  synchronized void clearRuntimeForColdReloadTest() {
    try {
      Field handle = MinimalAppApplication.class.getDeclaredField("runtimeHandle");
      Field started = MinimalAppApplication.class.getDeclaredField("modelThreadStarted");
      handle.setAccessible(true); started.setAccessible(true);
      long runtime = handle.getLong(this);
      if (runtime != 0) NativeRuntime.nativeDestroy(runtime);
      handle.setLong(this, 0L); started.setBoolean(this, false);
    } catch (ReflectiveOperationException error) {
      throw new AssertionError("test runtime lifecycle access failed", error);
    }
  }
  @Override public void onBackgroundSaveFinished(int result) {
    super.onBackgroundSaveFinished(result);
    BackgroundSaveObserver current = observer.get();
    if (current != null) current.onBackgroundSaveFinished(result);
    CountDownLatch currentLatch = latch.get();
    if (currentLatch != null) currentLatch.countDown();
  }
}
