# NativeRuntime methods are bound with RegisterNatives from JNI_OnLoad and
# NativeUiBridge methods are resolved by name from native code.
-keep class com.apptraverse.windowcatalog.NativeRuntime { *; }
-keep class com.apptraverse.windowcatalog.NativeUiBridge { *; }
-keep class com.apptraverse.ui.android.AppTraverseAndroidUi { *; }
# Looked up by name from WindowCatalogNativeRuntime::SyncMobilePagerChrome.
-keepclassmembers class com.apptraverse.windowcatalog.MainActivity {
  public void setPresentedFullscreen(boolean);
}
