#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/proxying.h>
#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include "chat_ui_session.h"
#include "apptraverse/platform/web/web_idbfs.h"
namespace gui=apptraverse::chat::gui;
namespace {
std::unique_ptr<gui::Session> session;
std::string profile;
auto* queue=em_proxying_queue_create();
struct Barrier{std::mutex mutex;std::condition_variable ready;bool done{},ok{};};
EM_JS(void,BeginTransportStore,(Barrier* barrier),{
  var mount=FS.lookupPath('/shared-network',{follow_mount:true}).node.mount;
  IDBFS.syncfs(mount,false,function(error){Module._SharedStored(barrier,error?0:1);});
});
bool FlushTransport(){
  Barrier barrier;
  if(!emscripten_proxy_async(queue,emscripten_main_runtime_thread_id(),[](void* value){BeginTransportStore(static_cast<Barrier*>(value));},&barrier))return false;
  std::unique_lock lock(barrier.mutex);barrier.ready.wait(lock,[&]{return barrier.done;});return barrier.ok;
}
EM_JS(void,PresentControl,(unsigned id,const char* text,int x,int y,int width,int height,int enabled,int visible,int read_only,std::uint64_t revision),{
  var root=document.getElementById('chat'),element=document.getElementById('control-'+id);
  if(!element){
    var tag=(id===1||id===3||id===9)?'input':(id===2||id===4||id===10)?'button':(id===6||id===8)?'pre':'label';
    element=document.createElement(tag);element.id='control-'+id;element.pending=0n;root.appendChild(element);
    if(id===3||id===9)element.addEventListener('input',function(){
      Module.sharedRevision=(Module.sharedRevision||0n)+1n;element.pending=Module.sharedRevision;
      var ptr=stringToNewUTF8(element.value);Module._SharedEdit(id,BigInt(ptr));_free(ptr);
    });
    if(id===2)element.addEventListener('click',function(){
      navigator.clipboard.writeText(document.getElementById('control-1').value).then(()=>Module._SharedCopyResult(1),()=>Module._SharedCopyResult(0));
    });
    if(id===4||id===10)element.addEventListener('click',()=>Module._SharedAction(id));
  }
  if(!Module.sharedRevision||revision>Module.sharedRevision)Module.sharedRevision=revision;
  var value=UTF8ToString(Number(text));
  if(element.tagName==='INPUT'){
    if(revision>=element.pending&&element.value!==value)element.value=value;
    element.readOnly=!!read_only;
  }else if(element.textContent!==value)element.textContent=value;
  element.disabled=!enabled;element.hidden=!visible;
  Object.assign(element.style,{left:x+'px',top:y+'px',width:Math.max(0,width)+'px',height:Math.max(0,height)+'px'});
});
EM_JS(void,SetTitle,(const char* text),{document.title=UTF8ToString(Number(text));document.getElementById('loading').style.display='none';});
struct Host:gui::Presentation{
  void PresentWindow(gui::ChatUi const& model)override{SetTitle(model.Title().c_str());}
  void RemoveWindow()override{}
  void Remove(gui::Control)override{}
  void Present(gui::Control id,gui::ControlState const& state,gui::ChatUi const& model)override{
    std::string text=state.text;if(id==gui::Control::People||id==gui::Control::Messages){text.clear();for(auto const& row:state.rows){if(!text.empty())text+='\n';text+=row;}}
    PresentControl(static_cast<unsigned>(id),text.c_str(),state.frame.x,state.frame.y,state.frame.width,state.frame.height,state.enabled,state.visible,state.read_only,model.editing.revision);
  }
} host;
void Render(){if(session)session->ApplyPublication(host);}
void Changed(){if(!emscripten_proxy_async(queue,emscripten_main_runtime_thread_id(),[](void*){Render();},nullptr))std::abort();}
EM_JS(void,Error,(),{document.getElementById('loading').textContent='Could not load saved state';});
EM_JS(void,InstallLifecycle,(),{
  var resize=()=>Module._SharedResize(document.documentElement.clientWidth,window.innerHeight);
  addEventListener('resize',resize);resize();
  addEventListener('pagehide',()=>Module._SharedSave());
  document.addEventListener('visibilitychange',()=>{if(document.hidden)Module._SharedSave();});
});
}
extern "C" {
EMSCRIPTEN_KEEPALIVE void SharedStored(Barrier* barrier,int ok){std::lock_guard lock(barrier->mutex);barrier->ok=ok;barrier->done=true;barrier->ready.notify_one();}
EMSCRIPTEN_KEEPALIVE void SharedResize(int width,int height){if(session)session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::Resize,width,height);}
EMSCRIPTEN_KEEPALIVE void SharedEdit(unsigned id,const char* text){if(session)session->Proxy().Invoke(gui::RootId(),id==3?&gui::ChatUi::EditInvite:&gui::ChatUi::EditDraft,std::string{text});}
EMSCRIPTEN_KEEPALIVE void SharedAction(unsigned id){if(session)session->Proxy().Invoke(gui::RootId(),id==4?&gui::ChatUi::Join:&gui::ChatUi::Send);}
EMSCRIPTEN_KEEPALIVE void SharedCopyResult(int ok){if(session)session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::ClipboardResult,ok!=0);}
EMSCRIPTEN_KEEPALIVE int SharedSave(){
  if(!session||!session->Mirror())return 1;
  std::vector<std::uint8_t> bytes;
  return session->CaptureUser(bytes)&&apptraverse::web::CommitIdbfsFileForPageLifecycle("/shared-ui",profile+"/user.bin",bytes);
}
EMSCRIPTEN_KEEPALIVE void SharedStart(const char* who,const char* invited,const char* profile_name,int is_host,int load_only){
  auto name=std::string{who},invite=std::string{invited},key=std::string{profile_name};profile="/shared-ui/"+key;
  apptraverse::web::MountIdbfsAndSyncLoad("/shared-ui",[name,invite,key,is_host,load_only]{
    apptraverse::web::MountIdbfsAndSyncLoad("/shared-network",[name,invite,key,is_host,load_only]{
      session=std::make_unique<gui::Session>(profile,name,is_host,invite,Changed,load_only,FlushTransport,"/shared-network/"+key);
      session->Start();InstallLifecycle();
    },Error);
  },Error);
}
EM_JS(void,BeginQuery,(),{
    var query=new URLSearchParams(location.search),profile=query.get('profile')||'default';
    if(!/^[A-Za-z0-9_-]+$/.test(profile))throw new Error('Invalid profile');
    var args=[query.get('name')||'Web',query.get('invite')||'',profile].map(value=>stringToNewUTF8(value));
    Module._SharedStart(BigInt(args[0]),BigInt(args[1]),BigInt(args[2]),query.get('role')==='client'?0:1,query.get('loadOnly')==='1'?1:0);
    args.forEach(ptr=>_free(ptr));
});
EMSCRIPTEN_KEEPALIVE void SharedBoot(){BeginQuery();}
}
int main(){return 0;}
