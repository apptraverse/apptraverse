package io.apptraverse.push;

import java.security.*;
import java.security.interfaces.*;
import java.security.spec.ECGenParameterSpec;
import java.util.Base64;

/** Run once; put the private value only in the push service environment. */
public final class VapidKeys {
  public static void main(String[] args) throws Exception {
    KeyPairGenerator generator=KeyPairGenerator.getInstance("EC");
    generator.initialize(new ECGenParameterSpec("secp256r1"));
    KeyPair pair=generator.generateKeyPair();
    ECPublicKey publicKey=(ECPublicKey)pair.getPublic();
    ECPrivateKey privateKey=(ECPrivateKey)pair.getPrivate();
    byte[] raw=new byte[65];raw[0]=4;
    System.arraycopy(fixed(publicKey.getW().getAffineX()),0,raw,1,32);
    System.arraycopy(fixed(publicKey.getW().getAffineY()),0,raw,33,32);
    var b64=Base64.getUrlEncoder().withoutPadding();
    System.out.println("PUSH_VAPID_PUBLIC="+b64.encodeToString(raw));
    System.out.println("PUSH_VAPID_PRIVATE="+b64.encodeToString(fixed(privateKey.getS())));
  }
  private static byte[] fixed(java.math.BigInteger value) {
    byte[] input=value.toByteArray(),output=new byte[32];
    System.arraycopy(input,Math.max(0,input.length-32),output,Math.max(0,32-input.length),Math.min(32,input.length));
    return output;
  }
}
