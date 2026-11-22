-keepclassmembers class com.apptraverse.sharedchat.MainActivity {
    public static void enqueuePublication();
    android.view.ViewGroup contentRoot();
    void copyUid(java.lang.String);
    native <methods>;
}

# Generic presenters look up these entry points by name and signature through JNI.
-keep class com.apptraverse.ui.android.AppTraverseAndroidUi {
    public static <methods>;
    native <methods>;
}
