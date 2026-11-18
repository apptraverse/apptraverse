# NativeRuntime methods are bound with RegisterNatives from JNI_OnLoad and
# NativeUiBridge methods are resolved by name from native code.
-keep class com.apptraverse.windowcatalog.NativeRuntime { *; }
-keep class com.apptraverse.windowcatalog.NativeUiBridge { *; }
-keep class com.apptraverse.ui.android.AppTraverseAndroidUi { *; }
