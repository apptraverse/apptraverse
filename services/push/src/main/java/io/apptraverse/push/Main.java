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
    FirebaseApp firebase=FirebaseApp.initializeApp(FirebaseOptions.builder().setCredentials(GoogleCredentials.getApplicationDefault()).build());
    Set<UUID> senders=new HashSet<>();
    for(String uid:env("PUSH_ALLOWED_SENDERS","").split(","))if(!uid.isBlank())senders.add(UUID.fromString(uid.trim()));
    PushService service=new PushService(directory.resolve("devices.db").toString(),senders,token->{
      Message message=Message.builder().setToken(token)
        .setNotification(Notification.builder().setTitle("SharedNode Chat").setBody("New message").build())
        .setAndroidConfig(AndroidConfig.builder().setPriority(AndroidConfig.Priority.HIGH)
          .setNotification(AndroidNotification.builder().setChannelId("chat_messages").build()).build()).build();
      FirebaseMessaging.getInstance(firebase).send(message);
    });
    UUID parent=UUID.fromString(env("AETHER_PARENT_UID","3ac93165-3d37-4970-87a6-fa4ee27744e4"));
    ClientStateInFile state=new ClientStateInFile(parent,List.of(URI.create(env("AETHER_REGISTRATION_URI","tcp://registration.aethernet.io:9010"))),directory.resolve("identity.bin").toFile());
    AetherCloudClient client=new AetherCloudClient(state,"apptraverse-push");
    AetherEndpoint endpoint=new AetherEndpoint(client,service::handle);
    Runtime.getRuntime().addShutdownHook(new Thread(()->{
      try{endpoint.close();state.saveState();client.destroy(true);service.close();firebase.delete();}catch(Exception e){System.err.println("Push shutdown failed");}
    }));
    CountDownLatch started=new CountDownLatch(1);client.startFuture.to(started::countDown);
    client.connect();if(!started.await(120,TimeUnit.SECONDS))throw new IllegalStateException("Aether registration timed out");state.saveState();
    System.out.println("PUSH_SERVICE_UID="+client.getUid());
    if(senders.isEmpty())System.out.println("No notify senders allowed. Set PUSH_ALLOWED_SENDERS to the Windows chat UID and restart.");
    new CountDownLatch(1).await();
  }
  private static String env(String key,String fallback){return System.getenv().getOrDefault(key,fallback);}
}
