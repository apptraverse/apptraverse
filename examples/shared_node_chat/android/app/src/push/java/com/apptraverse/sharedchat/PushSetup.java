package com.apptraverse.sharedchat;
import android.app.Activity;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.os.Build;
import android.content.pm.PackageManager;
import com.google.firebase.messaging.FirebaseMessaging;
final class PushSetup {
  static void start(Activity activity) {
    if (Build.VERSION.SDK_INT >= 26) {
      activity.getSystemService(NotificationManager.class).createNotificationChannel(
          new NotificationChannel("chat_messages", "Chat messages", NotificationManager.IMPORTANCE_HIGH));
    }
    if(Build.VERSION.SDK_INT>=33&&activity.checkSelfPermission("android.permission.POST_NOTIFICATIONS")!=PackageManager.PERMISSION_GRANTED)
      activity.requestPermissions(new String[]{"android.permission.POST_NOTIFICATIONS"},701);
  }
  static void resume(Activity activity) {
    // This example has one chat. Opening it consumes all its notifications,
    // including FCM's automatically generated tags and the old foreground ID.
    // AutoCancel alone only removes the particular notification that was tapped.
    activity.getSystemService(NotificationManager.class).cancelAll();
    FirebaseMessaging.getInstance().getToken().addOnSuccessListener(MainActivity::registerPushToken)
      .addOnFailureListener(e->android.util.Log.e("SharedChatPush","Token registration failed",e));
  }
}
