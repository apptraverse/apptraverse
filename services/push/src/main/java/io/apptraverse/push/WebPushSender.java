package io.apptraverse.push;

import java.math.BigInteger;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.security.interfaces.ECPublicKey;
import java.security.spec.*;
import java.time.Duration;
import java.time.Instant;
import java.util.Base64;
import java.util.Set;

/** Payload-free RFC 8030 Web Push with RFC 8292 VAPID authentication. */
public final class WebPushSender {
  private static final Set<String> HOSTS=Set.of(
      "fcm.googleapis.com", "updates.push.services.mozilla.com", "web.push.apple.com");
  private static final Base64.Encoder BASE64=Base64.getUrlEncoder().withoutPadding();
  private final PrivateKey privateKey;
  private final String publicKey, subject;
  private final HttpClient http;

  public WebPushSender(String publicKey,String privateKey,String subject) throws Exception {
    this(publicKey,privateKey,subject,HttpClient.newBuilder()
        .connectTimeout(Duration.ofSeconds(10)).followRedirects(HttpClient.Redirect.NEVER).build());
  }
  WebPushSender(String publicKey,String privateKey,String subject,HttpClient http) throws Exception {
    byte[] raw=Base64.getUrlDecoder().decode(publicKey);
    byte[] secret=Base64.getUrlDecoder().decode(privateKey);
    if(raw.length!=65||raw[0]!=4||secret.length!=32||
        !subject.matches("mailto:[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+"))
      throw new IllegalArgumentException("VAPID configuration");
    AlgorithmParameters parameters=AlgorithmParameters.getInstance("EC");
    parameters.init(new ECGenParameterSpec("secp256r1"));
    ECParameterSpec curve=parameters.getParameterSpec(ECParameterSpec.class);
    KeyFactory factory=KeyFactory.getInstance("EC");
    this.privateKey=factory.generatePrivate(new ECPrivateKeySpec(new BigInteger(1,secret),curve));
    ECPublicKey pub=(ECPublicKey)factory.generatePublic(new ECPublicKeySpec(
        new ECPoint(new BigInteger(1,java.util.Arrays.copyOfRange(raw,1,33)),
                    new BigInteger(1,java.util.Arrays.copyOfRange(raw,33,65))),curve));
    Signature check=Signature.getInstance("SHA256withECDSAinP1363Format");
    check.initSign(this.privateKey);check.update(new byte[]{1});byte[] signed=check.sign();
    check.initVerify(pub);check.update(new byte[]{1});
    if(!check.verify(signed))throw new IllegalArgumentException("VAPID key pair does not match");
    this.publicKey=publicKey;this.subject=subject;this.http=http;
  }

  public static boolean validEndpoint(String value) {
    try {
      URI uri=URI.create(value);
      String host=uri.getHost();
      return "https".equals(uri.getScheme())&&uri.getUserInfo()==null&&
          (uri.getPort()==-1||uri.getPort()==443)&&uri.getFragment()==null&&
          host!=null&&(HOSTS.contains(host)||host.endsWith(".notify.windows.com"))&&
          uri.getRawPath()!=null&&!uri.getRawPath().isEmpty();
    }catch(IllegalArgumentException e){return false;}
  }

  String authorization(URI endpoint,Instant now) throws GeneralSecurityException {
    String origin="https://"+endpoint.getHost();
    String header=BASE64.encodeToString("{\"typ\":\"JWT\",\"alg\":\"ES256\"}".getBytes(StandardCharsets.US_ASCII));
    String claims="{\"aud\":\""+origin+"\",\"exp\":"+(now.getEpochSecond()+12*3600)+
        ",\"sub\":\""+subject+"\"}";
    String input=header+"."+BASE64.encodeToString(claims.getBytes(StandardCharsets.UTF_8));
    Signature signer=Signature.getInstance("SHA256withECDSAinP1363Format");
    signer.initSign(privateKey);signer.update(input.getBytes(StandardCharsets.US_ASCII));
    return "vapid t="+input+"."+BASE64.encodeToString(signer.sign())+", k="+publicKey;
  }

  public void send(String endpoint) throws Exception {
    HttpRequest request=request(endpoint,Instant.now());
    int status=http.send(request,HttpResponse.BodyHandlers.discarding()).statusCode();
    if(status<200||status>=300)throw new IllegalStateException("Web Push HTTP "+status);
  }

  HttpRequest request(String endpoint,Instant now) throws GeneralSecurityException {
    if(!validEndpoint(endpoint))throw new IllegalArgumentException("Web Push endpoint");
    URI uri=URI.create(endpoint);
    return HttpRequest.newBuilder(uri).timeout(Duration.ofSeconds(20))
        .header("TTL","3600").header("Authorization",authorization(uri,now))
        .POST(HttpRequest.BodyPublishers.noBody()).build();
  }
}
