package io.apptraverse.push;
import io.aether.cloud.client.*;
import java.net.URI;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.atomic.AtomicInteger;
// Explicit real-cloud acceptance. Never runs as an ordinary unit test.
public final class LiveTransportProbe {
  public static void main(String[] args)throws Exception {
    Path directory=Path.of(args[0]);Files.createDirectories(directory);
    ClientStateInFile state=new ClientStateInFile(UUID.fromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"),List.of(URI.create("tcp://registration.aethernet.io:9010")),directory.resolve("service-identity.bin").toFile());
    AetherCloudClient client=new AetherCloudClient(state,"push-transport-probe");
    AtomicReference<UUID> phone=new AtomicReference<>();CountDownLatch delivered=new CountDownLatch(6);
    AtomicInteger notifications=new AtomicInteger();
    try(AetherEndpoint endpoint=new AetherEndpoint(client,(sender,p)->{
      int status=1;
      if(p.method()==1&&(p.value().equals("transport-probe-token")||p.value().equals("transport-probe-token-rotated"))){
        phone.set(sender);status=0;
        if(p.value().endsWith("-rotated"))try{Files.writeString(directory.resolve("token-rotated"),"registered");}catch(java.io.IOException e){throw new RuntimeException(e);}
      }
      if(p.method()==3&&phone.get()!=null&&sender.equals(phone.get())) {
        ApplePush.Address.parse(p.value());status=0;
        try{Files.writeString(directory.resolve("apple-registered"),"registered");}catch(java.io.IOException e){throw new RuntimeException(e);}
      }
      if(p.method()==2&&phone.get()!=null&&p.value().equals(phone.get().toString())&&!sender.equals(phone.get())){status=0;
        try{Files.writeString(directory.resolve("notify-"+notifications.incrementAndGet()),"received");}catch(java.io.IOException e){throw new RuntimeException(e);}
        delivered.countDown();}
      return new Protocol.Packet(p.method()|128,p.request(),"",status);
    })){
      CountDownLatch started=new CountDownLatch(1);client.startFuture.to(started::countDown);client.connect();
      if(!started.await(120,TimeUnit.SECONDS))throw new IllegalStateException("registration timed out");
      state.saveState();Files.writeString(directory.resolve("service.uid"),client.getUid().toString());
      if(!delivered.await(150,TimeUnit.SECONDS))throw new IllegalStateException("binary notify timed out");
      Files.writeString(directory.resolve("delivered"),"C++ -> Aether -> Java binary register/notify PASS");
      Thread.sleep(10000);
    }finally{client.destroy(true);state.saveState();}
    System.out.println("LIVE_TRANSPORT_PASS");System.exit(0);
  }
}
