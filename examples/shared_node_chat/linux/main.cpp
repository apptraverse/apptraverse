#include <gtk/gtk.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include "chat_ui_session.h"
#include "apptraverse/platform/linux/linux_ui_client_mount.h"
#include "apptraverse/noninteractive_crt.h"

namespace gui = apptraverse::chat::gui;
namespace {
struct Application final : gui::Presentation {
  GtkWidget* window{};
  GtkWidget* mount{};
  std::array<GtkWidget*,static_cast<unsigned>(gui::Control::Count)> controls{};
  std::array<GtkWidget*,static_cast<unsigned>(gui::Control::Count)> contents{};
  std::array<std::uint64_t,static_cast<unsigned>(gui::Control::Count)> pending_edits{};
  std::uint64_t submitted_edit{};
  std::unique_ptr<gui::Session> session;
  std::atomic<bool> closing{false}, queued{false};
  bool applying{};
  void Publish() {
    if(closing.load() || queued.exchange(true)) return;
    g_idle_add_full(G_PRIORITY_DEFAULT,[](gpointer data)->gboolean {
      auto& app=*static_cast<Application*>(data);app.queued=false;
      if(!app.closing)app.session->ApplyPublication(app);
      return G_SOURCE_REMOVE;
    },this,nullptr);
  }
  void PresentWindow(gui::ChatUi const& model) override {
    applying=true;
    if(!window) {
      window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
      mount=apptraverse::ui::linux::CreateClientMount();
      gtk_container_add(GTK_CONTAINER(window),mount);
      gtk_window_set_default_size(GTK_WINDOW(window),model.restore_client_width(),model.restore_client_height());
      if(model.HasExplicitPosition())gtk_window_move(GTK_WINDOW(window),model.x(),model.y());
      g_signal_connect(window,"delete-event",G_CALLBACK((+[](GtkWidget*,GdkEvent*,gpointer p)->gboolean {
        auto& app=*static_cast<Application*>(p);app.closing=true;gtk_main_quit();return TRUE;
      })),this);
      g_signal_connect(window,"configure-event",G_CALLBACK((+[](GtkWidget* w,GdkEventConfigure*,gpointer p)->gboolean {
        auto& app=*static_cast<Application*>(p);if(app.applying||app.closing)return FALSE;
        int x,y,width,height;gtk_window_get_position(GTK_WINDOW(w),&x,&y);gtk_window_get_size(GTK_WINDOW(w),&width,&height);
        app.session->Proxy().Invoke(gui::RootId(),&apptraverse::ui::UiDesktopWindow::SubmitFrameFromNative,x,y,width,height,true);
        return FALSE;
      })),this);
      gtk_widget_show_all(window);
    }
    gtk_window_set_title(GTK_WINDOW(window),model.Title().c_str());
    applying=false;
  }
  GtkWidget* Create(gui::Control id) {
    auto index=static_cast<unsigned>(id);GtkWidget* widget{};
    if(id==gui::Control::Uid||id==gui::Control::Invite||id==gui::Control::Input) {
      widget=gtk_entry_new();gtk_editable_set_editable(GTK_EDITABLE(widget),id!=gui::Control::Uid);
      g_object_set_data(G_OBJECT(widget),"control",GUINT_TO_POINTER(index));
      g_signal_connect(widget,"changed",G_CALLBACK((+[](GtkEditable* edit,gpointer p) {
        auto& app=*static_cast<Application*>(p);if(app.applying||app.closing)return;
        auto id=static_cast<gui::Control>(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(edit),"control")));
        std::string text=gtk_entry_get_text(GTK_ENTRY(edit));
        app.submitted_edit=std::max(app.submitted_edit,app.session->Mirror()->editing.revision)+1;
        app.pending_edits[static_cast<unsigned>(id)]=app.submitted_edit;
        if(id==gui::Control::Input)app.session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::EditDraft,std::move(text));
        if(id==gui::Control::Invite)app.session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::EditInvite,std::move(text));
      })),this);
    } else if(id==gui::Control::Copy||id==gui::Control::Join||id==gui::Control::Send) {
      widget=gtk_button_new();g_object_set_data(G_OBJECT(widget),"control",GUINT_TO_POINTER(index));
      g_signal_connect(widget,"clicked",G_CALLBACK((+[](GtkButton* button,gpointer p) {
        auto& app=*static_cast<Application*>(p);if(app.applying||app.closing)return;
        auto id=static_cast<gui::Control>(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button),"control")));
        if(id==gui::Control::Send)app.session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::Send);
        if(id==gui::Control::Join)app.session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::Join);
        if(id==gui::Control::Copy) {
          auto* model=app.session->Mirror();
          gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),model->Get(gui::Control::Uid).text.c_str(),-1);
          app.session->Proxy().Invoke(gui::RootId(),&gui::ChatUi::ClipboardResult,true);
        }
      })),this);
    } else if(id==gui::Control::People||id==gui::Control::Messages) {
      widget=gtk_scrolled_window_new(nullptr,nullptr);auto* view=gtk_text_view_new();contents[index]=view;
      gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);
      gtk_container_add(GTK_CONTAINER(widget),view);gtk_widget_show(view);
    } else {
      widget=gtk_label_new("");gtk_label_set_xalign(GTK_LABEL(widget),0);gtk_label_set_ellipsize(GTK_LABEL(widget),PANGO_ELLIPSIZE_END);
    }
    std::string name="shared-chat-"+std::to_string(index);gtk_widget_set_name(widget,name.c_str());
    gtk_fixed_put(GTK_FIXED(mount),widget,0,0);return widget;
  }
  void Present(gui::Control id,gui::ControlState const& state,gui::ChatUi const& model) override {
    applying=true;auto index=static_cast<unsigned>(id);auto*& widget=controls[index];if(!widget)widget=Create(id);
    if(GTK_IS_ENTRY(widget)) {
      // Only an acknowledged edit may replace native text. A later Send can
      // already have cleared it in the same coalesced publication.
      if(model.editing.revision>=pending_edits[index]&&state.text!=gtk_entry_get_text(GTK_ENTRY(widget)))gtk_entry_set_text(GTK_ENTRY(widget),state.text.c_str());
      gtk_editable_set_editable(GTK_EDITABLE(widget),!state.read_only);
    } else if(GTK_IS_BUTTON(widget))gtk_button_set_label(GTK_BUTTON(widget),state.text.c_str());
    else if(contents[index]) {
      std::string text;for(auto const& row:state.rows){if(!text.empty())text+='\n';text+=row;}
      auto* buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(contents[index]));
      GtkTextIter begin,end;gtk_text_buffer_get_bounds(buffer,&begin,&end);auto* prior=gtk_text_buffer_get_text(buffer,&begin,&end,FALSE);
      if(text!=prior)gtk_text_buffer_set_text(buffer,text.c_str(),static_cast<int>(text.size()));g_free(prior);
    } else gtk_label_set_text(GTK_LABEL(widget),state.text.c_str());
    gtk_fixed_move(GTK_FIXED(mount),widget,state.frame.x,state.frame.y);
    gtk_widget_set_size_request(widget,state.frame.width,state.frame.height);
    gtk_widget_set_sensitive(widget,state.enabled);gtk_widget_set_visible(widget,state.visible);applying=false;
  }
  void Remove(gui::Control id) override {auto* w=controls[static_cast<unsigned>(id)];if(w)gtk_widget_hide(w);}
  void RemoveWindow() override {
    if(window)gtk_widget_destroy(window);window=nullptr;mount=nullptr;controls.fill(nullptr);contents.fill(nullptr);
  }
};
}
int main(int argc,char** argv) {
  std::filesystem::path profile="shared-chat-state";std::string name="Host",invite;bool host=true;
  for(int i=1;i<argc;++i){std::string arg=argv[i];
    if(arg=="--profile"&&i+1<argc)profile=argv[++i];
    else if(arg=="--name"&&i+1<argc)name=argv[++i];
    else if(arg=="--host")host=true;
    else if(arg=="--client"){host=false;if(i+1<argc&&std::string(argv[i+1]).find("--")!=0)invite=argv[++i];}
    else return 2;
  }
  if(!gtk_init_check(nullptr,nullptr))return 1;
  Application app;app.session=std::make_unique<gui::Session>(profile,name,host,invite,[&]{app.Publish();},true);
  app.session->Start();gtk_main();app.closing=true;app.session->Stop();
  while(g_source_remove_by_user_data(&app)){}
  return 0;
}
