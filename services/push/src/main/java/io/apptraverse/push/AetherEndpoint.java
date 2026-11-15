package io.apptraverse.push;
import io.aether.cloud.client.AetherCloudClient;
import java.util.UUID;
import java.util.concurrent.*;
import java.util.function.BiFunction;

/** Binary API over authenticated Aether peer streams. No object graph. */
public final class AetherEndpoint implements AutoCloseable {
  private final ExecutorService worker=Executors.newSingleThreadExecutor();
  public AetherEndpoint(AetherCloudClient client,BiFunction<UUID,Protocol.Packet,Protocol.Packet> handler) {
    client.onClientStream(node->{
      Protocol.Reader reader=new Protocol.Reader();
      node.bufferIn.add(bytes->{
        try{reader.feed(bytes,p->{
          if(p.method()!=1&&p.method()!=2)return;
          worker.execute(()->{
            Protocol.Packet answer=handler.apply(node.getConsumerUUID(),p);
            node.send(Protocol.encode(answer));
            System.out.println("Push result method="+p.method()+" status="+answer.status());
          });
        });}catch(IllegalArgumentException e){System.err.println("Rejected malformed push frame");}
      });
    });
  }
  public void close()throws InterruptedException{worker.shutdown();worker.awaitTermination(30,TimeUnit.SECONDS);}
}
