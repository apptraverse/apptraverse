package io.apptraverse.push;

import java.math.BigInteger;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.security.interfaces.*;
import java.security.spec.ECGenParameterSpec;
import java.time.Instant;
import java.util.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class WebPushSenderTest {
  private static byte[] fixed(BigInteger value) {
    byte[] input=value.toByteArray(),output=new byte[32];
    System.arraycopy(input,Math.max(0,input.length-32),output,Math.max(0,32-input.length),Math.min(32,input.length));
    return output;
  }
  @Test void vapidSignatureAndPayloadFreeRequest() throws Exception {
    KeyPairGenerator generator=KeyPairGenerator.getInstance("EC");
    generator.initialize(new ECGenParameterSpec("secp256r1"));
    KeyPair pair=generator.generateKeyPair();
    ECPublicKey publicKey=(ECPublicKey)pair.getPublic();
    byte[] raw=new byte[65];raw[0]=4;
    System.arraycopy(fixed(publicKey.getW().getAffineX()),0,raw,1,32);
    System.arraycopy(fixed(publicKey.getW().getAffineY()),0,raw,33,32);
    Base64.Encoder encoder=Base64.getUrlEncoder().withoutPadding();
    String pub=encoder.encodeToString(raw),priv=encoder.encodeToString(fixed(((ECPrivateKey)pair.getPrivate()).getS()));
    WebPushSender sender=new WebPushSender(pub,priv,"mailto:push@example.org");
    String endpoint="https://updates.push.services.mozilla.com/wpush/v2/subscription";
    Instant now=Instant.ofEpochSecond(2000000000);
    var request=sender.request(endpoint,now);
    assertEquals("POST",request.method());
    assertEquals(0,request.bodyPublisher().orElseThrow().contentLength());
    assertEquals("3600",request.headers().firstValue("TTL").orElseThrow());
    String header=request.headers().firstValue("Authorization").orElseThrow();
    assertTrue(header.startsWith("vapid t="));assertTrue(header.endsWith(", k="+pub));
    String token=header.substring(8,header.indexOf(", k="));
    String[] parts=token.split("\\.");assertEquals(3,parts.length);
    String claims=new String(Base64.getUrlDecoder().decode(parts[1]),StandardCharsets.UTF_8);
    assertTrue(claims.contains("\"aud\":\"https://updates.push.services.mozilla.com\""));
    assertTrue(claims.contains("\"exp\":2000043200"));
    Signature verifier=Signature.getInstance("SHA256withECDSAinP1363Format");
    verifier.initVerify(publicKey);
    verifier.update((parts[0]+"."+parts[1]).getBytes(StandardCharsets.US_ASCII));
    assertTrue(verifier.verify(Base64.getUrlDecoder().decode(parts[2])));
    assertFalse(WebPushSender.validEndpoint("http://localhost/push"));
    assertFalse(WebPushSender.validEndpoint("https://127.0.0.1/push"));
    assertFalse(WebPushSender.validEndpoint("https://fcm.googleapis.com:8443/push"));
    assertTrue(WebPushSender.validEndpoint("https://wns2-db5p.notify.windows.com/push"));
    assertFalse(WebPushSender.validEndpoint("https://notify.windows.com.evil.example/push"));
    assertThrows(IllegalArgumentException.class,()->sender.request("https://example.com/push",now));
    assertThrows(IllegalArgumentException.class,()->new WebPushSender(pub,priv,"mailto:\"x\":1"));
  }
}
