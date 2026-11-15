package com.apptraverse.sharedchat;
import android.app.*;
import android.os.Build;
import android.content.pm.PackageManager;
import com.google.firebase.messaging.FirebaseMessaging;
final class PushSetup {
  static void start(Activity activity) {
    activity.getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel("chat_messages","Chat messages",NotificationManager.IMPORTANCE_HIGH));
    if(Build.VERSION.SDK_INT>=33&&activity.checkSelfPermission("android.permission.POST_NOTIFICATIONS")!=PackageManager.PERMISSION_GRANTED)
      activity.requestPermissions(new String[]{"android.permission.POST_NOTIFICATIONS"},701);
    refresh();
  }
  static void refresh() {
    FirebaseMessaging.getInstance().getToken().addOnSuccessListener(MainActivity::registerPushToken)
      .addOnFailureListener(e->android.util.Log.e("SharedChatPush","Token registration failed",e));
  }
}
