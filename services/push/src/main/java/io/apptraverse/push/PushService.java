package io.apptraverse.push;
import java.sql.*;
import java.util.*;

public final class PushService implements AutoCloseable {
  @FunctionalInterface public interface Delivery { void send(String token) throws Exception; }
  private final Connection db;
  private final Set<UUID> senders;
  private final Delivery delivery, apple;
  public PushService(String database,Set<UUID> senders,Delivery delivery) throws SQLException {
    this(database,senders,delivery,token->{throw new IllegalStateException("APNs not configured");});
  }
  public PushService(String database,Set<UUID> senders,Delivery delivery,Delivery apple) throws SQLException {
    this.senders=Set.copyOf(senders);this.delivery=delivery;this.apple=apple;
    db=DriverManager.getConnection("jdbc:sqlite:"+database);
    try(Statement s=db.createStatement()){s.executeUpdate("CREATE TABLE IF NOT EXISTS devices(uid TEXT PRIMARY KEY, token TEXT NOT NULL)");s.executeUpdate("CREATE TABLE IF NOT EXISTS apple_devices(uid TEXT PRIMARY KEY, token TEXT NOT NULL)");}
  }
  public synchronized Protocol.Packet handle(UUID sender,Protocol.Packet p) {
    int status=1;
    try {
      if((p.method()==1||p.method()==3)&&!p.value().isBlank()) {
        if(p.method()==3)ApplePush.Address.parse(p.value());
        String table=p.method()==3?"apple_devices":"devices";
        String other=p.method()==3?"devices":"apple_devices";
        try(PreparedStatement s=db.prepareStatement("INSERT INTO "+table+" VALUES(?,?) ON CONFLICT(uid) DO UPDATE SET token=excluded.token")){
          s.setString(1,sender.toString());s.setString(2,p.value());s.executeUpdate();
          try(PreparedStatement clear=db.prepareStatement("DELETE FROM "+other+" WHERE uid=?")){
            clear.setString(1,sender.toString());clear.executeUpdate();
          }
          status=0;
        }
      } else if(p.method()==2&&senders.contains(sender)) {
        String uid=UUID.fromString(p.value()).toString();
        if(!uid.equalsIgnoreCase(p.value()))throw new IllegalArgumentException("target UID");
        try(PreparedStatement s=db.prepareStatement("SELECT token,0 FROM devices WHERE uid=? UNION ALL SELECT token,1 FROM apple_devices WHERE uid=?")) {
          s.setString(1,uid);s.setString(2,uid);
          try(ResultSet r=s.executeQuery()){
            if(r.next()){
              // Provider validation errors (including IllegalArgumentException)
              // are delivery failures, not malformed binary requests.
              status=2;
              try{(r.getInt(2)==1?apple:delivery).send(r.getString(1));status=0;}
              catch(Exception e){System.err.println("Push delivery failed: "+e.getClass().getSimpleName());}
            }
          }
        }
      }
    }catch(IllegalArgumentException e){status=1;}
    catch(Exception e){status=2;System.err.println("Push operation failed: "+e.getClass().getSimpleName());}
    return new Protocol.Packet(p.method()|128,p.request(),"",status);
  }
  public synchronized void close()throws SQLException{db.close();}
}
