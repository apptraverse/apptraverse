package io.apptraverse.push;

import java.net.URI;
import java.net.http.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.*;
import java.security.spec.PKCS8EncodedKeySpec;
import java.time.*;
import java.util.*;

/** Native APNs provider. Keys stay exclusively on the service host. */
public final class ApplePush {
  public record Address(String environment, String topic, String token) {
    public static Address parse(String value) {
      String[] parts=value.split("\\|",-1);
      if(parts.length!=3 || !Set.of("sandbox","production").contains(parts[0]) ||
          !parts[1].matches("[A-Za-z0-9.-]{1,255}") ||
          !parts[2].matches("[0-9a-f]{2,512}") || parts[2].length()%2!=0)
        throw new IllegalArgumentException("APNs address");
      return new Address(parts[0],parts[1],parts[2]);
    }
  }
  private final String team, keyId;
  private final PrivateKey key;
  private final HttpClient client=HttpClient.newBuilder().version(HttpClient.Version.HTTP_2)
      .connectTimeout(Duration.ofSeconds(10)).build();
  private String jwt;
  private long issued;
  public ApplePush(String team,String keyId,Path file)throws Exception {
    if(!team.matches("[A-Z0-9]{10}")||!keyId.matches("[A-Z0-9]{10}"))
      throw new IllegalArgumentException("APNs team/key ID");
    this.team=team;this.keyId=keyId;
    String pem=Files.readString(file).replace("-----BEGIN PRIVATE KEY-----","")
      .replace("-----END PRIVATE KEY-----","").replaceAll("\\s","");
    key=KeyFactory.getInstance("EC").generatePrivate(new PKCS8EncodedKeySpec(Base64.getDecoder().decode(pem)));
  }
  private static String base64(byte[] bytes){return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);}
  synchronized String authorization(long now)throws Exception {
    if(jwt!=null&&now>=issued&&now-issued<3000)return jwt;
    String header=base64(("{\"alg\":\"ES256\",\"kid\":\""+keyId+"\"}").getBytes(StandardCharsets.UTF_8));
    String claims=base64(("{\"iss\":\""+team+"\",\"iat\":"+now+"}").getBytes(StandardCharsets.UTF_8));
    String data=header+"."+claims;
    Signature signer=Signature.getInstance("SHA256withECDSAinP1363Format");
    signer.initSign(key);signer.update(data.getBytes(StandardCharsets.US_ASCII));
    jwt=data+"."+base64(signer.sign());issued=now;return jwt;
  }
  HttpRequest request(String value,long now)throws Exception {
    Address address=Address.parse(value);
    String host=address.environment().equals("sandbox")?"api.sandbox.push.apple.com":"api.push.apple.com";
    return HttpRequest.newBuilder(URI.create("https://"+host+"/3/device/"+address.token()))
      .timeout(Duration.ofSeconds(20)).header("authorization","bearer "+authorization(now))
      .header("apns-topic",address.topic()).header("apns-push-type","alert")
      .header("apns-priority","10").header("apns-expiration","0")
      .POST(HttpRequest.BodyPublishers.ofString("{\"aps\":{\"alert\":{\"title\":\"SharedNode Chat\",\"body\":\"New message\"},\"sound\":\"default\"}}" )).build();
  }
  public void send(String value)throws Exception {
    var response=client.send(request(value,Instant.now().getEpochSecond()),HttpResponse.BodyHandlers.discarding());
    if(response.statusCode()!=200||response.version()!=HttpClient.Version.HTTP_2)
      throw new java.io.IOException("APNs rejected request: HTTP "+response.statusCode());
  }
}
