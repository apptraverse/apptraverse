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
  @Test void providerFailureIsNotAcknowledgedAsSuccess()throws Exception{
    try(var service=new PushService(directory.resolve("db").toString(),Set.of(WINDOWS),token->{throw new Exception("provider unavailable");})){
      service.handle(PHONE,new Protocol.Packet(1,1,"token",0));
      assertEquals(2,service.handle(WINDOWS,new Protocol.Packet(2,2,PHONE.toString(),0)).status());
    }
  }
}
