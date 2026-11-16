package io.apptraverse.push;

import io.aether.cloud.client.AetherCloudClient;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiFunction;
import java.util.function.Consumer;

/** Binary API over authenticated Aether peer streams. No object graph. */
public final class AetherEndpoint implements AutoCloseable {
  private final ThreadPoolExecutor worker;
  private final BiFunction<UUID, Protocol.Packet, Protocol.Packet> handler;
  private final AtomicBoolean closing = new AtomicBoolean();

  public AetherEndpoint(AetherCloudClient client,
      BiFunction<UUID, Protocol.Packet, Protocol.Packet> handler) {
    this(handler, 256);
    client.onClientStream(node -> {
      Protocol.Reader reader = new Protocol.Reader();
      node.bufferIn.add(bytes -> {
        if (closing.get()) return;
        try {
          reader.feed(bytes, packet -> dispatch(node.getConsumerUUID(), packet,
              answer -> node.send(Protocol.encode(answer))));
        } catch (IllegalArgumentException e) {
          System.err.println("Rejected malformed push frame");
        }
      });
    });
  }

  // Same dispatcher used by the real endpoint and deterministic concurrency tests.
  AetherEndpoint(BiFunction<UUID, Protocol.Packet, Protocol.Packet> handler, int capacity) {
    this.handler = handler;
    worker = new ThreadPoolExecutor(1, 1, 0, TimeUnit.MILLISECONDS,
        new ArrayBlockingQueue<>(capacity));
  }

  void dispatch(UUID sender, Protocol.Packet packet, Consumer<Protocol.Packet> reply) {
    if (closing.get() || (packet.method() != 1 && packet.method() != 2 && packet.method() != 3 && packet.method() != 4)) return;
    try {
      worker.execute(() -> {
        Protocol.Packet answer;
        try {
          answer = handler.apply(sender, packet);
        } catch (RuntimeException e) {
          System.err.println("Push handler failed: " + e.getClass().getSimpleName());
          answer = failure(packet);
        }
        reply(reply, answer);
        System.out.println("Push result method=" + packet.method() + " status=" + answer.status());
      });
    } catch (RejectedExecutionException e) {
      // A slow provider must not create an unbounded queue or throw into the
      // SDK's receive thread. Shutdown and overload are normal endpoint states.
      if (!closing.get()) reply(reply, failure(packet));
    }
  }

  private static Protocol.Packet failure(Protocol.Packet packet) {
    return new Protocol.Packet(packet.method() | 128, packet.request(), "", 2);
  }

  private static void reply(Consumer<Protocol.Packet> reply, Protocol.Packet answer) {
    try {
      reply.accept(answer);
    } catch (RuntimeException e) {
      System.err.println("Push reply failed: " + e.getClass().getSimpleName());
    }
  }

  @Override public void close() throws InterruptedException {
    closing.set(true);
    worker.shutdown();
    try {
      if (!worker.awaitTermination(30, TimeUnit.SECONDS)) {
        worker.shutdownNow();
        if (!worker.awaitTermination(5, TimeUnit.SECONDS))
          throw new IllegalStateException("Push worker did not stop");
      }
    } catch (InterruptedException e) {
      worker.shutdownNow();
      throw e;
    }
  }
}
