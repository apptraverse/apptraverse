package io.apptraverse.push;

import java.util.List;
import java.util.UUID;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class EndpointTest {
  @Test void boundedQueueDrainsAndRejectsOverloadWithoutThrowingIntoReceiver() throws Exception {
    var entered = new CountDownLatch(1);
    var release = new CountDownLatch(1);
    List<Protocol.Packet> replies = new CopyOnWriteArrayList<>();
    var endpoint = new AetherEndpoint((sender, packet) -> {
      entered.countDown();
      try { assertTrue(release.await(5, TimeUnit.SECONDS)); }
      catch (InterruptedException e) { throw new RuntimeException(e); }
      return new Protocol.Packet(129, packet.request(), "", 0);
    }, 1);
    try {
      UUID sender = UUID.randomUUID();
      endpoint.dispatch(sender, new Protocol.Packet(1, 1, "token", 0), replies::add);
      assertTrue(entered.await(5, TimeUnit.SECONDS));
      endpoint.dispatch(sender, new Protocol.Packet(1, 2, "token", 0), replies::add);
      endpoint.dispatch(sender, new Protocol.Packet(1, 3, "token", 0), replies::add);
      assertEquals(List.of(new Protocol.Packet(129, 3, "", 2)), replies);
      release.countDown();
      endpoint.close();
      assertEquals(List.of(3L, 1L, 2L), replies.stream().map(Protocol.Packet::request).toList());
      assertEquals(List.of(2, 0, 0), replies.stream().map(Protocol.Packet::status).toList());
      endpoint.dispatch(sender, new Protocol.Packet(1, 4, "token", 0), replies::add);
      assertEquals(3, replies.size());
    } finally { release.countDown(); endpoint.close(); }
  }

  @Test void handlerAndReplyExceptionsDoNotStopLaterRequests() throws Exception {
    List<Protocol.Packet> replies = new CopyOnWriteArrayList<>();
    try (var endpoint = new AetherEndpoint((sender, packet) -> {
      if (packet.request() == 1) throw new IllegalStateException("test failure");
      return new Protocol.Packet(129, packet.request(), "", 0);
    }, 4)) {
      UUID sender = UUID.randomUUID();
      endpoint.dispatch(sender, new Protocol.Packet(1, 1, "token", 0), replies::add);
      endpoint.dispatch(sender, new Protocol.Packet(1, 2, "token", 0), answer -> { throw new IllegalStateException("reply failed"); });
      endpoint.dispatch(sender, new Protocol.Packet(1, 3, "token", 0), replies::add);
    }
    assertEquals(List.of(new Protocol.Packet(129, 1, "", 2), new Protocol.Packet(129, 3, "", 0)), replies);
  }
}
