package io.apptraverse.push;
import java.nio.*;
import java.nio.charset.*;
import java.io.ByteArrayOutputStream;
import java.util.function.Consumer;

public final class Protocol {
  public record Packet(int method, long request, String value, int status) {}
  public static byte[] encode(Packet p) {
    byte[] text=p.value().getBytes(StandardCharsets.UTF_8);
    if(text.length>2048)throw new IllegalArgumentException("value too long");
    boolean response=(p.method()&128)!=0;
    ByteBuffer b=ByteBuffer.allocate(response?17:18+text.length).order(ByteOrder.BIG_ENDIAN);
    b.putInt(0x4154504e).putShort((short)b.capacity()).put((byte)1).put((byte)p.method()).putLong(p.request());
    if(response)b.put((byte)p.status());else b.putShort((short)text.length).put(text);
    return b.array();
  }
  public static Packet decode(byte[] bytes) {
    if(bytes.length<17||bytes.length>4096)throw new IllegalArgumentException("frame size");
    ByteBuffer b=ByteBuffer.wrap(bytes).order(ByteOrder.BIG_ENDIAN);
    if(b.getInt()!=0x4154504e||Short.toUnsignedInt(b.getShort())!=bytes.length||b.get()!=1)
      throw new IllegalArgumentException("frame header/version");
    int method=Byte.toUnsignedInt(b.get());long request=b.getLong();
    if(method==129||method==130){if(b.remaining()!=1)throw new IllegalArgumentException("response");return new Packet(method,request,"",Byte.toUnsignedInt(b.get()));}
    if((method!=1&&method!=2)||b.remaining()<2)throw new IllegalArgumentException("method");
    int n=Short.toUnsignedInt(b.getShort());if(n>2048||n!=b.remaining())throw new IllegalArgumentException("string size");
    try {
      String text=StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT).decode(b).toString();
      return new Packet(method,request,text,0);
    }catch(CharacterCodingException e){throw new IllegalArgumentException("UTF-8",e);}
  }
  public static final class Reader {
    private final ByteArrayOutputStream buffer=new ByteArrayOutputStream();
    private int size;
    public synchronized void feed(byte[] chunk,Consumer<Packet> receiver) {
      try {
        for(byte c:chunk) {
          buffer.write(c);
          if(buffer.size()==6){byte[] b=buffer.toByteArray();size=((b[4]&255)<<8)|(b[5]&255);if(size<17||size>4096)throw new IllegalArgumentException("frame size");}
          if(size>0&&buffer.size()==size){Packet p=decode(buffer.toByteArray());buffer.reset();size=0;receiver.accept(p);}
        }
      }catch(RuntimeException e){buffer.reset();size=0;throw e;}
    }
  }
}
