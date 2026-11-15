#include "apptraverse/chat_session.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
int main(int argc,char** argv){
  if(argc!=2)return 2;
  std::filesystem::path dir=argv[1];
  auto deadline=std::chrono::steady_clock::now()+180s;
  auto wait=[&](auto ready){while(std::chrono::steady_clock::now()<deadline){if(ready())return true;std::this_thread::sleep_for(50ms);}return false;};
  if(!wait([&]{return std::filesystem::exists(dir/"service.uid");}))return 3;
  std::string uid;std::ifstream(dir/"service.uid")>>uid;
  apptraverse::chat::ChatSession windows{dir/"windows","Windows"},phone{dir/"android","Android"};
  windows.ConfigurePush(uid);phone.ConfigurePush(uid);windows.Start();phone.Start();
  if(!wait([&]{return windows.View().ready&&phone.View().ready;}))return 4;
  phone.RegisterPushToken("transport-probe-token");windows.CreateRoom();
  if(!wait([&]{return windows.View().in_room;}))return 5;
  phone.JoinRoom(windows.View().local_uid);
  if(!wait([&]{return windows.View().participants.size()==2&&phone.View().participants.size()==2;}))return 6;
  if(!wait([&]{return phone.View().push_registered;}))return 8;
  windows.Send("push transport acceptance",[&](bool accepted){if(accepted)windows.NotifyPushPeers();});
  if(!wait([&]{return std::filesystem::exists(dir/"notify-1")&&windows.View().push_notify_status==0&&windows.View().push_notify_request>=1;}))return 9;
  std::this_thread::sleep_for(6s); // exceed the link stall timeout while idle
  windows.NotifyPushPeers();
  if(!wait([&]{return std::filesystem::exists(dir/"notify-2")&&windows.View().push_notify_request>=2;}))return 10;
  std::this_thread::sleep_for(6s);
  windows.NotifyPushPeers();
  bool ok=wait([&]{return std::filesystem::exists(dir/"delivered")&&windows.View().push_notify_status==0&&windows.View().push_notify_request>=3;});
  windows.Stop();phone.Stop();if(!ok)return 7;
  std::cout<<"LIVE_TRANSPORT_PASS\n";
}
