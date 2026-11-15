package com.apptraverse.sharedchat;
import android.app.*;
import android.content.Intent;
import com.google.firebase.messaging.*;
public final class ChatMessagingService extends FirebaseMessagingService {
  @Override public void onNewToken(String token) {
    // A background callback must not create a native chat session. getToken()
    // registers the current token whenever the chat next starts/resumes.
    MainActivity.registerPushToken(token);
  }
  @Override public void onMessageReceived(RemoteMessage message) {
    NotificationManager manager=getSystemService(NotificationManager.class);
    manager.createNotificationChannel(new NotificationChannel("chat_messages","Chat messages",NotificationManager.IMPORTANCE_HIGH));
    Intent intent=new Intent(this,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP|Intent.FLAG_ACTIVITY_SINGLE_TOP);
    PendingIntent open=PendingIntent.getActivity(this,0,intent,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
    manager.notify(1,new Notification.Builder(this,"chat_messages").setSmallIcon(android.R.drawable.stat_notify_chat)
      .setContentTitle("SharedNode Chat").setContentText("New message").setAutoCancel(true).setContentIntent(open).build());
  }
}
