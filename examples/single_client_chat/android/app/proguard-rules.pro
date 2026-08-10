# The native library binds these members through JNI.
-keepclasseswithmembernames class com.apptraverse.singleclientchat.NativeRuntime {
  native <methods>;
}

-keepclassmembers class com.apptraverse.singleclientchat.NativeUiBridge {
  void onNativeStatus(java.lang.String);
  void onNativeTranscript(java.lang.String);
  void onNativeMessageCommitted(java.lang.String);
}
