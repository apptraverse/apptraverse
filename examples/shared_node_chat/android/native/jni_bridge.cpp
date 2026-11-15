#include <jni.h>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include "chat_ui_session.h"
#include "android_jni_util.h"
namespace gui=apptraverse::chat::gui;
using namespace apptraverse::ui::android;
namespace {
JavaVM* vm{};
jclass activity_class{};
jmethodID enqueue{},present{},title{};
struct Host final:gui::Presentation {
  JNIEnv* env{};
  jobject activity{};
  void PresentWindow(gui::ChatUi const& model) override {
    auto text=Utf8ToJstring(env,model.Title());
    CheckedJni(env,"chat title",[&]{env->CallVoidMethod(activity,title,text);});
    env->DeleteLocalRef(text);
  }
  void Present(gui::Control id,gui::ControlState const& state,gui::ChatUi const& model) override {
    std::string text=state.text;
    if(id==gui::Control::People||id==gui::Control::Messages){
      text.clear();for(auto const& row:state.rows){if(!text.empty())text+='\n';text+=row;}
    }
    auto value=Utf8ToJstring(env,text);
    CheckedJni(env,"chat control",[&]{env->CallVoidMethod(activity,present,
      static_cast<jint>(id),value,state.frame.x,state.frame.y,state.frame.width,state.frame.height,
      static_cast<jboolean>(state.enabled),static_cast<jboolean>(state.visible),
      static_cast<jboolean>(state.read_only),static_cast<jlong>(model.editing.revision));});
    env->DeleteLocalRef(value);
  }
  void Remove(gui::Control) override {}
  void RemoveWindow() override {}
} host;
std::unique_ptr<gui::Session> session;
void Changed(){
  JNIEnv* env{};bool attached=false;
  if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK){
    if(vm->AttachCurrentThread(&env,nullptr)!=JNI_OK)std::abort();attached=true;
  }
  CheckedJni(env,"queue chat publication",[&]{env->CallStaticVoidMethod(activity_class,enqueue);});
  if(attached)vm->DetachCurrentThread();
}
}
extern "C" JNIEXPORT void JNICALL Java_com_apptraverse_sharedchat_MainActivity_startNative(
    JNIEnv* env,jobject activity,jstring path,jstring name,jboolean is_host,jstring invite,jboolean load_only,jstring push_uid){
  if(!activity_class){
    env->GetJavaVM(&vm);auto cls=env->GetObjectClass(activity);
    activity_class=static_cast<jclass>(CheckedJni(env,"chat class",[&]{return env->NewGlobalRef(cls);}));
    env->DeleteLocalRef(cls);
    enqueue=CheckedJni(env,"chat notification method",[&]{return env->GetStaticMethodID(activity_class,"enqueuePublication","()V");});
    present=CheckedJni(env,"chat present method",[&]{return env->GetMethodID(activity_class,"present","(ILjava/lang/String;IIIIZZZJ)V");});
    title=CheckedJni(env,"chat title method",[&]{return env->GetMethodID(activity_class,"presentTitle","(Ljava/lang/String;)V");});
  }
  if(!session){
    session=std::make_unique<gui::Session>(JstringToUtf8(env,path),JstringToUtf8(env,name),
      is_host,JstringToUtf8(env,invite),Changed,load_only);
    session->ConfigurePush(JstringToUtf8(env,push_uid));
    session->Start();
  }
}
extern "C" JNIEXPORT void JNICALL Java_com_apptraverse_sharedchat_MainActivity_renderNative(JNIEnv* env,jobject activity,jboolean remount){
  if(!session)return;
  host.env=env;host.activity=activity;
  session->ApplyPublication(host);
  if(remount)if(auto* model=session->Mirror()){
    host.PresentWindow(*model);
    for(unsigned i=0;i<static_cast<unsigned>(gui::Control::Count);++i)
      host.Present(static_cast<gui::Control>(i),model->controls[i],*model);
  }
  host.activity=nullptr;host.env=nullptr;
}
extern "C" JNIEXPORT void JNICALL Java_com_apptraverse_sharedchat_MainActivity_inputNative(JNIEnv* env,jobject,jint id,jstring value){
  if(!session)return;auto text=JstringToUtf8(env,value);
  if(id==static_cast<jint>(gui::Control::Input))session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::EditDraft,std::move(text));
  if(id==static_cast<jint>(gui::Control::Invite))session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::EditInvite,std::move(text));
}
extern "C" JNIEXPORT void JNICALL Java_com_apptraverse_sharedchat_MainActivity_actionNative(JNIEnv*,jobject,jint id){
  if(!session)return;
  if(id==static_cast<jint>(gui::Control::Send))session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::Send);
  if(id==static_cast<jint>(gui::Control::Join))session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::Join);
  if(id==static_cast<jint>(gui::Control::Copy))session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::ClipboardResult,true);
}
extern "C" JNIEXPORT void JNICALL Java_com_apptraverse_sharedchat_MainActivity_resizeNative(JNIEnv*,jobject,jint width,jint height){
  if(session)session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::Resize,width,height);
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_apptraverse_sharedchat_MainActivity_checkpointNative(JNIEnv*,jobject){
  if(!session)return true;
  std::mutex mutex;std::condition_variable ready;bool done=false,ok=false;
  session->Checkpoint([&](bool result){std::lock_guard lock(mutex);ok=result;done=true;ready.notify_one();});
  std::unique_lock lock(mutex);ready.wait(lock,[&]{return done;});return ok;
}
extern "C" JNIEXPORT void JNICALL Java_com_apptraverse_sharedchat_MainActivity_stopNative(JNIEnv*,jobject){session.reset();}

extern "C" JNIEXPORT void JNICALL Java_com_apptraverse_sharedchat_MainActivity_registerPushTokenNative(JNIEnv* env,jclass,jstring token){
  if(session)session->RegisterPushToken(JstringToUtf8(env,token));
}
