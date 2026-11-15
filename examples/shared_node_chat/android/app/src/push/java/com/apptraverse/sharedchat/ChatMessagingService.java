package com.apptraverse.sharedchat;
import android.util.Log;
import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;
public final class ChatMessagingService extends FirebaseMessagingService {
  @Override public void onNewToken(String token) {
    // A background callback must not create a native chat session. getToken()
    // registers the current token whenever the chat next starts/resumes.
    MainActivity.registerPushToken(token);
  }
  @Override public void onMessageReceived(RemoteMessage message) {
    // With the pinned FCM SDK and this service's notification payload, Android
    // displays background notifications automatically. This callback handles
    // foreground delivery; SharedNode already updates the open chat, so do not
    // post another notification. Data-only pushes are not part of this contract.
    Log.d("SharedChatPush", "Foreground push consumed without notification");
  }
}
