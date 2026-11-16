package io.apptraverse.push;
import java.sql.*;
import java.util.*;

public final class PushService implements AutoCloseable {
  @FunctionalInterface public interface Delivery { void send(String token) throws Exception; }
  private final Connection db;
  private final Set<UUID> senders;
  private final Delivery delivery, apple, webDelivery;
  public PushService(String database,Set<UUID> senders,Delivery delivery) throws SQLException {
    this(database,senders,delivery,token->{throw new IllegalStateException("APNs not configured");},null);
  }
  public PushService(String database,Set<UUID> senders,Delivery delivery,Delivery apple) throws SQLException {
    this(database,senders,delivery,apple,null);
  }
  public PushService(String database,Set<UUID> senders,Delivery delivery,Delivery apple,Delivery webDelivery) throws SQLException {
    this.senders=Set.copyOf(senders);this.delivery=delivery;this.apple=apple;this.webDelivery=webDelivery;
    db=DriverManager.getConnection("jdbc:sqlite:"+database);
    try(Statement s=db.createStatement()){
      s.executeUpdate("CREATE TABLE IF NOT EXISTS devices(uid TEXT PRIMARY KEY, token TEXT NOT NULL)");
      s.executeUpdate("CREATE TABLE IF NOT EXISTS apple_devices(uid TEXT PRIMARY KEY, token TEXT NOT NULL)");
      s.executeUpdate("CREATE TABLE IF NOT EXISTS web_devices(uid TEXT PRIMARY KEY, endpoint TEXT NOT NULL)");
    }
  }
  public synchronized Protocol.Packet handle(UUID sender,Protocol.Packet p) {
    int status=1;
    try {
      if((p.method()==1||p.method()==3||p.method()==4)&&!p.value().isBlank()) {
        int method=p.method();
        if((method==1&&delivery==null)||(method==3&&apple==null)||(method==4&&webDelivery==null))
          throw new IllegalArgumentException("provider disabled");
        if(method==3)ApplePush.Address.parse(p.value());
        if(method==4&&!WebPushSender.validEndpoint(p.value()))throw new IllegalArgumentException("Web Push endpoint");
        String table=method==1?"devices":method==3?"apple_devices":"web_devices";
        String field=method==4?"endpoint":"token";
        try(PreparedStatement s=db.prepareStatement("INSERT INTO "+table+"(uid,"+field+") VALUES(?,?) ON CONFLICT(uid) DO UPDATE SET "+field+"=excluded."+field)){
          s.setString(1,sender.toString());s.setString(2,p.value());s.executeUpdate();
        }
        for(String other:List.of("devices","apple_devices","web_devices"))if(!other.equals(table)){
          try(PreparedStatement clear=db.prepareStatement("DELETE FROM "+other+" WHERE uid=?")){
            clear.setString(1,sender.toString());clear.executeUpdate();
          }
        }
        status=0;
      } else if(p.method()==2&&senders.contains(sender)) {
        String uid=UUID.fromString(p.value()).toString();
        if(!uid.equalsIgnoreCase(p.value()))throw new IllegalArgumentException("target UID");
        boolean found=false,failed=false;
        try(PreparedStatement s=db.prepareStatement("SELECT token,0 FROM devices WHERE uid=? UNION ALL SELECT token,1 FROM apple_devices WHERE uid=? UNION ALL SELECT endpoint,2 FROM web_devices WHERE uid=?")) {
          s.setString(1,uid);s.setString(2,uid);s.setString(3,uid);
          try(ResultSet r=s.executeQuery()){
            while(r.next()){
              found=true;
              Delivery provider=switch(r.getInt(2)){case 0->delivery;case 1->apple;default->webDelivery;};
              try{
                if(provider==null)throw new IllegalStateException("provider disabled");
                provider.send(r.getString(1));
              }catch(Exception e){failed=true;System.err.println("Push delivery failed: "+e.getClass().getSimpleName());}
            }
          }
        }
        status=!found?1:failed?2:0;
      }
    }catch(IllegalArgumentException e){status=1;}
    catch(Exception e){status=2;System.err.println("Push operation failed: "+e.getClass().getSimpleName());}
    return new Protocol.Packet(p.method()|128,p.request(),"",status);
  }
  public synchronized void close()throws SQLException{db.close();}
}
