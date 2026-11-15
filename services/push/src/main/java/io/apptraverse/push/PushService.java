package io.apptraverse.push;
import java.sql.*;
import java.util.*;

public final class PushService implements AutoCloseable {
  @FunctionalInterface public interface Delivery { void send(String token) throws Exception; }
  private final Connection db;
  private final Set<UUID> senders;
  private final Delivery delivery;
  public PushService(String database,Set<UUID> senders,Delivery delivery) throws SQLException {
    this.senders=Set.copyOf(senders);this.delivery=delivery;
    db=DriverManager.getConnection("jdbc:sqlite:"+database);
    try(Statement s=db.createStatement()){s.executeUpdate("CREATE TABLE IF NOT EXISTS devices(uid TEXT PRIMARY KEY, token TEXT NOT NULL)");}
  }
  public synchronized Protocol.Packet handle(UUID sender,Protocol.Packet p) {
    int status=1;
    try {
      if(p.method()==1&&!p.value().isBlank()) {
        try(PreparedStatement s=db.prepareStatement("INSERT INTO devices VALUES(?,?) ON CONFLICT(uid) DO UPDATE SET token=excluded.token")){
          s.setString(1,sender.toString());s.setString(2,p.value());s.executeUpdate();status=0;
        }
      } else if(p.method()==2&&senders.contains(sender)) {
        String uid=UUID.fromString(p.value()).toString();
        try(PreparedStatement s=db.prepareStatement("SELECT token FROM devices WHERE uid=?")) {
          s.setString(1,uid);
          try(ResultSet r=s.executeQuery()){if(r.next()){status=2;delivery.send(r.getString(1));status=0;}}
        }
      }
    }catch(IllegalArgumentException e){status=1;}
    catch(Exception e){status=2;System.err.println("Push operation failed: "+e.getClass().getSimpleName());}
    return new Protocol.Packet(p.method()|128,p.request(),"",status);
  }
  public void close()throws SQLException{db.close();}
}
