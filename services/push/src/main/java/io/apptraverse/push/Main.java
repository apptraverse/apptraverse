package io.apptraverse.push;
import io.aether.cloud.client.*;
import com.google.auth.oauth2.GoogleCredentials;
import com.google.firebase.*;
import com.google.firebase.messaging.*;
import java.nio.file.*;
import java.net.URI;
import java.util.*;
import java.util.concurrent.*;

public final class Main {
  public static void main(String[] args)throws Exception {
    Path directory=Path.of(env("PUSH_STATE_DIR","state"));Files.createDirectories(directory);
    // ADC reads GOOGLE_APPLICATION_CREDENTIALS. Never ship this key with clients.
    FirebaseApp firebase="false".equalsIgnoreCase(env("PUSH_FCM_ENABLED","true"))?null:
        FirebaseApp.initializeApp(FirebaseOptions.builder()
          .setCredentials(GoogleCredentials.getApplicationDefault())
          .setConnectTimeout(10000).setReadTimeout(20000).setWriteTimeout(10000).build());
    String vapidPublic=env("PUSH_VAPID_PUBLIC","");
    String vapidPrivate=env("PUSH_VAPID_PRIVATE","");
    if(vapidPublic.isEmpty()!=vapidPrivate.isEmpty())throw new IllegalArgumentException("Incomplete VAPID key pair");
    WebPushSender web=vapidPublic.isEmpty()?null:new WebPushSender(vapidPublic,vapidPrivate,
        env("PUSH_VAPID_SUBJECT",""));
    String appleKey=env("APNS_KEY_FILE","");
    ApplePush apple=appleKey.isBlank()?null:new ApplePush(env("APNS_TEAM_ID",""),env("APNS_KEY_ID",""),Path.of(appleKey));
    Set<UUID> senders=new HashSet<>();
    for(String uid:env("PUSH_ALLOWED_SENDERS","").split(","))if(!uid.isBlank())senders.add(UUID.fromString(uid.trim()));
    PushService.Delivery firebaseDelivery=firebase==null?null:token->{
      Message message=Message.builder().setToken(token)
        .setNotification(Notification.builder().setTitle("SharedNode Chat").setBody("New message").build())
        .setAndroidConfig(AndroidConfig.builder().setPriority(AndroidConfig.Priority.HIGH)
          .setNotification(AndroidNotification.builder().setChannelId("chat_messages").build()).build()).build();
      FirebaseMessaging.getInstance(firebase).send(message);
    };
    PushService service=new PushService(directory.resolve("devices.db").toString(),senders,
        firebaseDelivery,apple==null?null:apple::send,web==null?null:web::send);
    UUID parent=UUID.fromString(env("AETHER_PARENT_UID","3ac93165-3d37-4970-87a6-fa4ee27744e4"));
    ClientStateInFile state=new ClientStateInFile(parent,List.of(URI.create(env("AETHER_REGISTRATION_URI","tcp://registration.aethernet.io:9010"))),directory.resolve("identity.bin").toFile());
    AetherCloudClient client=new AetherCloudClient(state,"apptraverse-push");
    AetherEndpoint endpoint=new AetherEndpoint(client,service::handle);
    Runtime.getRuntime().addShutdownHook(new Thread(()->{
      // One cleanup failure must not skip every remaining resource.
      cleanup("endpoint",endpoint::close);
      cleanup("identity",state::saveState);
      cleanup("Aether",()->client.destroy(true));
      cleanup("database",service::close);
      if(firebase!=null)cleanup("Firebase",firebase::delete);
    }));
    CountDownLatch started=new CountDownLatch(1);client.startFuture.to(started::countDown);
    try {
      client.connect();
      if(!started.await(120,TimeUnit.SECONDS))throw new IllegalStateException("Aether registration timed out");
      state.saveState();
    } catch(Exception e) {
      System.err.println("Push startup failed: "+e.getClass().getSimpleName());
      System.exit(1); // Runs cleanup even when SDK threads keep the JVM alive.
      return;
    }
    System.out.println("PUSH_SERVICE_UID="+client.getUid());
    if(senders.isEmpty())System.out.println("No notify senders allowed. Set PUSH_ALLOWED_SENDERS to the Windows chat UID and restart.");
    new CountDownLatch(1).await();
  }
  private static String env(String key,String fallback){return System.getenv().getOrDefault(key,fallback);}
  @FunctionalInterface private interface Cleanup { void run() throws Exception; }
  private static void cleanup(String name,Cleanup action){
    try{action.run();}catch(Exception e){System.err.println("Push shutdown failed ("+name+"): "+e.getClass().getSimpleName());}
  }
}
