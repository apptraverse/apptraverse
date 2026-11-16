package io.apptraverse.push;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import java.nio.file.Path;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
import static org.junit.jupiter.api.Assertions.*;
class PushTest {
  @TempDir Path directory;
  static final UUID PHONE=UUID.fromString("11111111-1111-1111-1111-111111111111"),WINDOWS=UUID.fromString("22222222-2222-2222-2222-222222222222");
  @Test void sharedWireFixtureAndChunkedStream(){
    byte[] expected=HexFormat.of().parseHex("4154504e0015010101020304050607080003616263");
    var packet=new Protocol.Packet(1,0x0102030405060708L,"abc",0);
    assertArrayEquals(expected,Protocol.encode(packet));assertEquals(packet,Protocol.decode(expected));
    List<Protocol.Packet> received=new ArrayList<>();var reader=new Protocol.Reader();
    for(byte b:expected)reader.feed(new byte[]{b},received::add);
    reader.feed(expected,received::add);assertEquals(List.of(packet,packet),received);
    expected[6]=2;assertThrows(IllegalArgumentException.class,()->Protocol.decode(expected));
    var web=new Protocol.Packet(4,9,"https://fcm.googleapis.com/fcm/send/test",0);
    assertEquals(web,Protocol.decode(Protocol.encode(web)));
    var webAck=new Protocol.Packet(132,9,"",0);
    assertEquals(webAck,Protocol.decode(Protocol.encode(webAck)));
  }
  @Test void registrationOwnershipAuthorizationAndRestart()throws Exception{
    String db=directory.resolve("devices.db").toString();List<String> delivered=new ArrayList<>();
    try(var service=new PushService(db,Set.of(WINDOWS),delivered::add)){
      assertEquals(0,service.handle(PHONE,new Protocol.Packet(1,1,"first",0)).status());
      assertEquals(0,service.handle(PHONE,new Protocol.Packet(1,2,"updated",0)).status());
      assertEquals(1,service.handle(PHONE,new Protocol.Packet(2,3,PHONE.toString(),0)).status());
    }
    try(var service=new PushService(db,Set.of(WINDOWS),delivered::add)){
      assertEquals(0,service.handle(WINDOWS,new Protocol.Packet(2,4,PHONE.toString(),0)).status());
      assertEquals(List.of("updated"),delivered);
      assertEquals(1,service.handle(WINDOWS,new Protocol.Packet(2,5,WINDOWS.toString(),0)).status());
    }
  }
  @Test void webRegistrationAndNotificationUseAuthenticatedUid()throws Exception {
    String db=directory.resolve("web-devices.db").toString();
    String endpoint="https://updates.push.services.mozilla.com/wpush/v2/test-subscription";
    List<String> sent=new ArrayList<>(),fcm=new ArrayList<>();
    try(var service=new PushService(db,Set.of(WINDOWS),fcm::add,null,sent::add)){
      assertEquals(1,service.handle(PHONE,new Protocol.Packet(4,1,"http://localhost/push",0)).status());
      assertEquals(0,service.handle(PHONE,new Protocol.Packet(1,9,"old-fcm-token",0)).status());
      assertEquals(0,service.handle(PHONE,new Protocol.Packet(4,2,endpoint,0)).status());
      assertEquals(1,service.handle(PHONE,new Protocol.Packet(2,3,PHONE.toString(),0)).status());
      assertEquals(0,service.handle(WINDOWS,new Protocol.Packet(2,4,PHONE.toString(),0)).status());
      assertTrue(fcm.isEmpty());
    }
    try(var service=new PushService(db,Set.of(WINDOWS),token->{},null,sent::add)){
      assertEquals(0,service.handle(WINDOWS,new Protocol.Packet(2,5,PHONE.toString(),0)).status());
      assertEquals(List.of(endpoint,endpoint),sent);
    }
    try(var service=new PushService(db,Set.of(WINDOWS),token->{},null,value->{throw new Exception("provider down");})){
      assertEquals(2,service.handle(WINDOWS,new Protocol.Packet(2,6,PHONE.toString(),0)).status());
    }
    try(var service=new PushService(db,Set.of(WINDOWS),null,null,sent::add)){
      assertEquals(1,service.handle(PHONE,new Protocol.Packet(1,7,"fcm-token",0)).status());
      assertEquals(0,service.handle(WINDOWS,new Protocol.Packet(2,8,PHONE.toString(),0)).status());
    }
  }
  @Test void providerFailureIsNotAcknowledgedAsSuccess()throws Exception{
    try(var service=new PushService(directory.resolve("db").toString(),Set.of(WINDOWS),token->{throw new Exception("provider unavailable");})){
      service.handle(PHONE,new Protocol.Packet(1,1,"token",0));
      assertEquals(2,service.handle(WINDOWS,new Protocol.Packet(2,2,PHONE.toString(),0)).status());
    }
  }
  @Test void utf8BoundariesAndEveryStreamSplit(){
    for(String value:List.of("", "\u041f\u0443\u0448 \ud83d\udd14", "x".repeat(2048))){
      var packet=new Protocol.Packet(1,-1L,value,0);
      byte[] frame=Protocol.encode(packet);
      assertEquals(packet,Protocol.decode(frame));
      for(int split=0;split<=frame.length;split++){
        var reader=new Protocol.Reader();List<Protocol.Packet> received=new ArrayList<>();
        reader.feed(Arrays.copyOfRange(frame,0,split),received::add);
        reader.feed(Arrays.copyOfRange(frame,split,frame.length),received::add);
        assertEquals(List.of(packet),received);
      }
    }
    assertThrows(IllegalArgumentException.class,()->Protocol.encode(new Protocol.Packet(1,1,"x".repeat(2049),0)));
    assertThrows(IllegalArgumentException.class,()->Protocol.encode(new Protocol.Packet(1,1,"\ud800",0)));
    assertThrows(IllegalArgumentException.class,()->Protocol.encode(new Protocol.Packet(5,1,"",0)));
    assertThrows(IllegalArgumentException.class,()->Protocol.encode(new Protocol.Packet(129,1,"",3)));
  }
  @Test void malformedFramesAndReaderRecovery(){
    for(String hex:List.of("c080", "eda080", "f4908080", "f09f", "80")){
      byte[] invalid=HexFormat.of().parseHex(hex);
      byte[] frame=Protocol.encode(new Protocol.Packet(1,1,"x".repeat(invalid.length),0));
      System.arraycopy(invalid,0,frame,18,invalid.length);
      assertThrows(IllegalArgumentException.class,()->Protocol.decode(frame));
    }
    byte[] valid=Protocol.encode(new Protocol.Packet(129,Long.MIN_VALUE,"",0));
    for(int field:new int[]{0,4,5,6,7,16}){
      byte[] invalid=valid.clone();invalid[field]=(byte)(field==5?0:0xff);
      assertThrows(IllegalArgumentException.class,()->Protocol.decode(invalid));
      var reader=new Protocol.Reader();List<Protocol.Packet> received=new ArrayList<>();
      assertThrows(IllegalArgumentException.class,()->reader.feed(invalid,received::add));
      reader.feed(valid,received::add);
      assertEquals(List.of(Protocol.decode(valid)),received);
    }
  }
  @Test void providerValidationFailureIsStillProviderFailure()throws Exception{
    try(var service=new PushService(directory.resolve("db").toString(),Set.of(WINDOWS),token->{throw new IllegalArgumentException("invalid provider token");})){
      service.handle(PHONE,new Protocol.Packet(1,1,"token",0));
      assertEquals(2,service.handle(WINDOWS,new Protocol.Packet(2,2,PHONE.toString(),0)).status());
      assertEquals(1,service.handle(WINDOWS,new Protocol.Packet(2,3,"1-1-1-1-1",0)).status());
    }
  }
}
