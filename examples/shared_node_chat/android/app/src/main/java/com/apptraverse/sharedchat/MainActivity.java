package com.apptraverse.sharedchat;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.widget.*;
import java.lang.ref.WeakReference;

public final class MainActivity extends Activity {
  static { System.loadLibrary("shared_chat_android"); }
  private static final Handler UI=new Handler(Looper.getMainLooper());
  private static WeakReference<MainActivity> current=new WeakReference<>(null);
  private static boolean queued;
  private final View[] controls=new View[12];
  private final TextView[] textViews=new TextView[12];
  private final long[] pending=new long[12];
  private FrameLayout surface;
  private boolean applying;
  private boolean scrollMessagesToEnd;
  private long revision;
  private float density;
  static native void registerPushTokenNative(String token);
  static void registerPushToken(String token){UI.post(()->{if(current.get()!=null)registerPushTokenNative(token);});}
  private native void startNative(String path,String name,boolean host,String invite,boolean loadOnly,String pushServiceUid);
  private native void renderNative(boolean remount);
  private native void inputNative(int control,String text);
  private native void actionNative(int control);
  private native void resizeNative(int width,int height);
  private native boolean checkpointNative();
  private native void stopNative();

  public static void enqueuePublication(){
    UI.post(()->{
      if(queued)return;queued=true;
      UI.post(()->{queued=false;MainActivity activity=current.get();if(activity!=null)activity.renderNative(false);});
    });
  }
  @Override public void onCreate(Bundle saved){
    super.onCreate(saved);density=getResources().getDisplayMetrics().density;
    surface=new FrameLayout(this);setContentView(surface);current=new WeakReference<>(this);
    // TextView height is only final after layout (including keyboard resizing).
    // Scroll once for new history, without moving focus out of the composer.
    surface.getViewTreeObserver().addOnPreDrawListener(()->{
      if(scrollMessagesToEnd){
        scrollMessagesToEnd=false;
        ScrollView messages=(ScrollView)controls[8];
        messages.scrollTo(0,textViews[8].getHeight());
      }
      return true;
    });
    String profile=getIntent().getStringExtra("profile");if(profile==null)profile="default";
    // Profiles are names inside private app storage, never arbitrary external paths.
    if(!profile.matches("[A-Za-z0-9_-]+"))throw new IllegalArgumentException("Invalid profile name");
    String name=getIntent().getStringExtra("name");if(name==null)name="Android";
    String invite=getIntent().getStringExtra("invite");if(invite==null)invite="";
    startNative(new java.io.File(getFilesDir(),profile).getAbsolutePath(),name,
      getIntent().getBooleanExtra("host",true),invite,getIntent().getBooleanExtra("loadOnly",false),BuildConfig.PUSH_SERVICE_UID);
    PushSetup.start(this);
    renderNative(true);
    surface.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or_,ob)->{
      if(r-l>0&&b-t>0)resizeNative(Math.round((r-l)/density),Math.round((b-t)/density));
    });
  }
  public void presentTitle(String title){setTitle(title);}
  public void present(int id,String text,int x,int y,int width,int height,boolean enabled,boolean visible,boolean readOnly,long acknowledged){
    applying=true;
    try {
      if(controls[id]==null)createControl(id);
      TextView view=textViews[id];
      revision=Math.max(revision,acknowledged);
      if(acknowledged>=pending[id]&&!view.getText().toString().equals(text)){
        view.setText(text);
        if(id==8)scrollMessagesToEnd=true;
      }
      if(view instanceof EditText){
        EditText edit=(EditText)view;
        edit.setShowSoftInputOnFocus(!readOnly);
        edit.setFocusable(!readOnly);edit.setFocusableInTouchMode(!readOnly);
        edit.setCursorVisible(!readOnly);
      }
      controls[id].setEnabled(enabled);controls[id].setVisibility(visible?View.VISIBLE:View.GONE);
      FrameLayout.LayoutParams frame=new FrameLayout.LayoutParams(Math.max(0,Math.round(width*density)),Math.max(0,Math.round(height*density)));
      frame.leftMargin=Math.round(x*density);frame.topMargin=Math.round(y*density);controls[id].setLayoutParams(frame);
    } finally {applying=false;}
  }
  private void createControl(int id){
    TextView text;
    if(id==3||id==9){
      EditText edit=new EditText(this);edit.setSingleLine(true);edit.setTextSize(14);edit.setPadding(4,0,4,0);
      edit.addTextChangedListener(new TextWatcher(){
        public void beforeTextChanged(CharSequence s,int start,int count,int after){}
        public void onTextChanged(CharSequence s,int start,int before,int count){if(!applying){pending[id]=++revision;inputNative(id,s.toString());}}
        public void afterTextChanged(Editable e){}
      });text=edit;
    } else if(id==2||id==4||id==10){
      Button button=new Button(this);button.setTextSize(12);button.setPadding(0,0,0,0);
      button.setOnClickListener(v->{
        if(id==2)((ClipboardManager)getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("SharedNode ID",textViews[1].getText()));
        actionNative(id);
      });text=button;
    } else {
      text=new TextView(this);text.setTextSize(14);text.setTextIsSelectable(id==1||id==6||id==8);
      if(id==1){text.setSingleLine(true);text.setEllipsize(android.text.TextUtils.TruncateAt.END);}
    }
    text.setContentDescription("shared-chat-"+id);textViews[id]=text;
    if(id==6||id==8){ScrollView scroll=new ScrollView(this);scroll.addView(text);controls[id]=scroll;}
    else controls[id]=text;
    surface.addView(controls[id]);
  }
  @Override protected void onResume(){super.onResume();PushSetup.resume(this);}
  @Override protected void onStop(){
    if(!checkpointNative())throw new IllegalStateException("SharedNode checkpoint failed");
    super.onStop();
  }
  @Override protected void onDestroy(){
    if(current.get()==this)current.clear();
    if(isFinishing())stopNative();super.onDestroy();
  }
}
