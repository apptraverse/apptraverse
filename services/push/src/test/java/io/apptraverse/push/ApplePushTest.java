package io.apptraverse.push;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import java.nio.file.*;
import java.security.*;
import java.security.spec.ECGenParameterSpec;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;
class ApplePushTest {
  @TempDir Path directory;
  @Test void signedJwtUsesRawEs256AndRefreshes()throws Exception {
    var generator=KeyPairGenerator.getInstance("EC");generator.initialize(new ECGenParameterSpec("secp256r1"));
    var pair=generator.generateKeyPair();Path file=directory.resolve("test.p8");
    Files.writeString(file,"-----BEGIN PRIVATE KEY-----\n"+Base64.getEncoder().encodeToString(pair.getPrivate().getEncoded())+"\n-----END PRIVATE KEY-----");
    var provider=new ApplePush("ABCDEFGHIJ","0123456789",file);
    var request=provider.request("sandbox|org.apptraverse.SharedNodeChat.macos|aabb",10000);
    assertEquals("https://api.sandbox.push.apple.com/3/device/aabb",request.uri().toString());
    assertEquals("alert",request.headers().firstValue("apns-push-type").orElseThrow());
    assertEquals("org.apptraverse.SharedNodeChat.macos",request.headers().firstValue("apns-topic").orElseThrow());
    assertEquals("POST",request.method());
    assertEquals("api.push.apple.com",provider.request("production|example.app|aabb",10000).uri().getHost());
    String jwt=provider.authorization(10000);String[] parts=jwt.split("\\.");
    byte[] signature=Base64.getUrlDecoder().decode(parts[2]);assertEquals(64,signature.length);
    var verifier=Signature.getInstance("SHA256withECDSAinP1363Format");verifier.initVerify(pair.getPublic());
    verifier.update((parts[0]+"."+parts[1]).getBytes(java.nio.charset.StandardCharsets.US_ASCII));assertTrue(verifier.verify(signature));
    assertEquals(jwt,provider.authorization(12999));assertNotEquals(jwt,provider.authorization(13000));
  }
  @Test void registrationRoutingReplacementAndPersistence()throws Exception {
    UUID device=UUID.randomUUID(),sender=UUID.randomUUID();String db=directory.resolve("db").toString();
    String address="sandbox|org.apptraverse.SharedNodeChat.macos|aabb";
    List<String> apple=new ArrayList<>(),fcm=new ArrayList<>();
    try(var service=new PushService(db,Set.of(sender),fcm::add,apple::add)) {
      assertEquals(0,service.handle(device,new Protocol.Packet(3,1,address,0)).status());
      assertEquals(1,service.handle(device,new Protocol.Packet(2,2,device.toString(),0)).status());
      assertEquals(1,service.handle(device,new Protocol.Packet(3,3,"sandbox|bad/topic|aabb",0)).status());
    }
    try(var service=new PushService(db,Set.of(sender),fcm::add,apple::add)) {
      assertEquals(0,service.handle(sender,new Protocol.Packet(2,4,device.toString(),0)).status());
      assertEquals(List.of(address),apple);assertTrue(fcm.isEmpty());
      assertEquals(0,service.handle(device,new Protocol.Packet(1,5,"fcm-token",0)).status());
      assertEquals(0,service.handle(sender,new Protocol.Packet(2,6,device.toString(),0)).status());
      assertEquals(List.of("fcm-token"),fcm);assertEquals(1,apple.size());
    }
  }
  @Test void newMethodFramingAndValidation() {
    var packet=new Protocol.Packet(3,42,"sandbox|org.apptraverse.SharedNodeChat.ios|aabb",0);
    byte[] bytes=Protocol.encode(packet);
    for(int split=0;split<=bytes.length;split++) {
      var reader=new Protocol.Reader();List<Protocol.Packet> result=new ArrayList<>();
      reader.feed(Arrays.copyOfRange(bytes,0,split),result::add);reader.feed(Arrays.copyOfRange(bytes,split,bytes.length),result::add);
      assertEquals(List.of(packet),result);
    }
    var response=new Protocol.Packet(131,42,"",0);assertEquals(response,Protocol.decode(Protocol.encode(response)));
    for(String invalid:List.of("development|topic|aa","sandbox|topic|a","sandbox|topic|zz","sandbox|topic|aa|extra"))
      assertThrows(IllegalArgumentException.class,()->ApplePush.Address.parse(invalid));
  }
}
