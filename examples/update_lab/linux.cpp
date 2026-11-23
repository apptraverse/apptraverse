#include "session.h"
#include <gtk/gtk.h>
#include <algorithm>
namespace update_lab {
namespace {
struct Window {
  Options const& options;
  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ModelObjectProxy proxy{[this](auto work) { runtime.Post(std::move(work)); }};
  std::unique_ptr<apptraverse::ApplicationUiSession> ui;
  std::thread model;
  int result{1};
  GtkWidget *window, *greeting, *language_label, *release_label, *languages, *releases, *note_label, *note, *save, *clones, *add, *slow, *progress, *status, *bad, *export_button, *import_button;
  bool rendering{false}, initialized{false}, closing{false}, smoke_started{false};
  explicit Window(Options const& value) : options{value} {}
  void Send(Command c) { proxy.Invoke(kViewId, &LabView::Submit, std::move(c)); }
  static void Clear(GtkWidget* list) { GList* items = gtk_container_get_children(GTK_CONTAINER(list)); for (auto i=items;i;i=i->next) gtk_widget_destroy(GTK_WIDGET(i->data)); g_list_free(items); }
  void Render() {
    auto bytes = runtime.TakePublishedCopyForUiApply();
    if (bytes.empty()) return;
    if (!ui) ui = std::make_unique<apptraverse::ApplicationUiSession>(bytes, nullptr, proxy, nullptr, false);
    else ui->ApplyStructural(bytes);
    runtime.NotifyPublicationConsumed();
    auto const& view = static_cast<LabView&>(ui->root()); auto const& d = view.data;
    rendering = true;
    if (!initialized) { initialized=true; gtk_window_move(GTK_WINDOW(window),d.x,d.y); gtk_window_resize(GTK_WINDOW(window),d.width,d.height); Send({"initial","",0,0,0,0}); }
    auto title = d.text[Title] + " · E" + std::to_string(d.executable) + " / R" + std::to_string(d.release);
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    gtk_label_set_text(GTK_LABEL(greeting), d.ready ? d.text[Greeting].c_str() : d.text[Downloading].c_str());
    gtk_label_set_text(GTK_LABEL(language_label),d.text[Language].c_str()); gtk_label_set_text(GTK_LABEL(release_label),d.text[Version].c_str());
    gtk_label_set_text(GTK_LABEL(note_label),d.text[Note].c_str());
    if (!gtk_widget_has_focus(note)) gtk_entry_set_text(GTK_ENTRY(note),d.note.c_str());
    gtk_button_set_label(GTK_BUTTON(save),d.text[ApplyNote].c_str()); gtk_button_set_label(GTK_BUTTON(add),d.text[AddClone].c_str());
    gtk_button_set_label(GTK_BUTTON(slow),d.text[Slow].c_str()); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(slow),d.rate!=0);
    gtk_button_set_label(GTK_BUTTON(bad),d.text[Broken].c_str()); gtk_button_set_label(GTK_BUTTON(export_button),d.text[Export].c_str()); gtk_button_set_label(GTK_BUTTON(import_button),d.text[Import].c_str());
    Clear(languages);
    for (auto const& l : d.languages) { auto row=gtk_label_new(l=="en"?"English":l=="ru"?"Русский":"Deutsch"); gtk_container_add(GTK_CONTAINER(languages),row); }
    gtk_widget_show_all(languages);
    for (std::size_t n=0;n<d.languages.size();++n) if(d.languages[n]==d.language) gtk_list_box_select_row(GTK_LIST_BOX(languages),gtk_list_box_get_row_at_index(GTK_LIST_BOX(languages),n));
    Clear(releases);
    for (std::size_t n = 0; n < d.distribution.releases.size(); ++n) {
      auto title = "R" + std::to_string(d.distribution.releases[n]);
      gtk_container_add(GTK_CONTAINER(releases),gtk_label_new(title.c_str()));
      if (d.distribution.releases[n] == d.release)
        gtk_list_box_select_row(GTK_LIST_BOX(releases),gtk_list_box_get_row_at_index(GTK_LIST_BOX(releases),n));
    }
    gtk_widget_show_all(releases);
    Clear(clones); for(auto const& c:d.clones) { auto text=d.text[Clone]+" "+c; gtk_container_add(GTK_CONTAINER(clones),gtk_label_new(text.c_str())); } gtk_widget_show_all(clones);
    auto const& download=view.download->state; std::uint64_t done=0,total=0;
    for(auto const& f:download.files) if(std::find(download.requested.begin(),download.requested.end(),f.id)!=download.requested.end()) { done+=f.received;total+=f.size; }
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress),total?double(done)/total:0);
    std::string text=download.phase=="error"?d.text[Failure]:download.phase=="activated"||download.phase=="idle"?d.text[Ready]:d.text[Downloading];
    text += " · "+std::to_string(done)+" / "+std::to_string(total)+" B · "+d.device+"\n"+d.detail+"  "+d.distribution.offer;
    gtk_label_set_text(GTK_LABEL(status),text.c_str());
    rendering=false; AcknowledgeHealth(options,view);
    if(!options.snapshot_file.empty()) { auto json=InspectJson(view);if(!AtomicWrite(options.snapshot_file,Bytes{json.begin(),json.end()}))Fail("snapshot write failed"); }
    if(options.native_smoke && !smoke_started && d.commands_done) {
      smoke_started=true;
      g_timeout_add(100,+[](gpointer pointer)->gboolean {
        auto& self=*static_cast<Window*>(pointer);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self.slow),TRUE);
        gtk_list_box_select_row(GTK_LIST_BOX(self.languages),gtk_list_box_get_row_at_index(GTK_LIST_BOX(self.languages),1));
        gtk_button_clicked(GTK_BUTTON(self.add));
        return G_SOURCE_REMOVE;
      },this);
    }
    if(options.native_smoke && smoke_started && download.phase=="activated" && d.language=="ru" && d.clones.size()==3 && d.rate==65536) {
      auto row=gtk_list_box_get_selected_row(GTK_LIST_BOX(languages));
      if(std::string{gtk_label_get_text(GTK_LABEL(greeting))}!=d.text[Greeting] || !row || gtk_list_box_row_get_index(row)!=1)
        Fail("native language presentation differs from the model publication");
      if(!AtomicWrite(options.profile/"native-smoke.ok",Bytes{'1'}))Fail("native acceptance marker write failed");
      gtk_window_close(GTK_WINDOW(window));
    }
    if(d.restarting) gtk_window_close(GTK_WINDOW(window));
  }
  void FileDialog(bool save_file) {
    auto dialog=gtk_file_chooser_dialog_new(save_file?"Export USER":"Import USER",GTK_WINDOW(window),save_file?GTK_FILE_CHOOSER_ACTION_SAVE:GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,save_file?"Save":"Open",GTK_RESPONSE_ACCEPT,nullptr);
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { char* path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));Send({save_file?"export":"import",path,0,0,0,0});g_free(path); } gtk_widget_destroy(dialog);
  }
  void Create() {
    window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_default_size(GTK_WINDOW(window),820,720);
    gtk_widget_set_name(window,"AppTraverseUpdateLab");
    auto outer=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);gtk_container_set_border_width(GTK_CONTAINER(outer),20);gtk_container_add(GTK_CONTAINER(window),outer);
    auto put=[&](GtkWidget* widget,bool expand=false){gtk_box_pack_start(GTK_BOX(outer),widget,expand,expand,0);};
    greeting=gtk_label_new("Loading");put(greeting);
    auto columns=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,20);put(columns);
    auto column=[&](GtkWidget*& label,GtkWidget*& list) { auto box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);gtk_box_pack_start(GTK_BOX(columns),box,TRUE,TRUE,0);label=gtk_label_new("");gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,0);list=gtk_list_box_new();gtk_box_pack_start(GTK_BOX(box),list,TRUE,TRUE,0); };
    column(language_label,languages);column(release_label,releases);
    note_label=gtk_label_new("");put(note_label);
    auto row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);put(row);note=gtk_entry_new();save=gtk_button_new();gtk_box_pack_start(GTK_BOX(row),note,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),save,FALSE,FALSE,0);
    clones=gtk_list_box_new();auto scroller=gtk_scrolled_window_new(nullptr,nullptr);gtk_container_add(GTK_CONTAINER(scroller),clones);put(scroller,true);
    add=gtk_button_new();put(add);slow=gtk_check_button_new();put(slow);progress=gtk_progress_bar_new();put(progress);status=gtk_label_new("");gtk_label_set_line_wrap(GTK_LABEL(status),TRUE);put(status);
    row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);put(row);export_button=gtk_button_new();import_button=gtk_button_new();bad=gtk_button_new();for(auto b:{export_button,import_button,bad})gtk_box_pack_start(GTK_BOX(row),b,TRUE,TRUE,0);
    g_signal_connect(languages,"row-selected",G_CALLBACK(+[](GtkListBox*,GtkListBoxRow* row,gpointer p){
      auto& self=*static_cast<Window*>(p);if(self.rendering||!row||!self.ui)return;
      auto const& d=static_cast<LabView&>(self.ui->root()).data;int n=gtk_list_box_row_get_index(row);
      self.Send({"language",d.languages[n],0,0,0,0});
      // The selected row represents the active locale throughout the download.
      self.rendering=true;
      for(std::size_t i=0;i<d.languages.size();++i) if(d.languages[i]==d.language)
        gtk_list_box_select_row(GTK_LIST_BOX(self.languages),gtk_list_box_get_row_at_index(GTK_LIST_BOX(self.languages),i));
      self.rendering=false;
    }),this);
    g_signal_connect(releases,"row-selected",G_CALLBACK(+[](GtkListBox*,GtkListBoxRow* row,gpointer p){auto& self=*static_cast<Window*>(p);if(!self.rendering&&row&&self.ui)self.Send({"release","",static_cast<int>(static_cast<LabView&>(self.ui->root()).data.distribution.releases.at(gtk_list_box_row_get_index(row))),0,0,0});}),this);
    g_signal_connect(save,"clicked",G_CALLBACK(+[](GtkButton*,gpointer p){auto& s=*static_cast<Window*>(p);s.Send({"note",gtk_entry_get_text(GTK_ENTRY(s.note)),0,0,0,0});}),this);
    g_signal_connect(add,"clicked",G_CALLBACK(+[](GtkButton*,gpointer p){static_cast<Window*>(p)->Send({"clone","",0,0,0,0});}),this);
    g_signal_connect(slow,"toggled",G_CALLBACK(+[](GtkToggleButton* b,gpointer p){auto& s=*static_cast<Window*>(p);if(!s.rendering)s.Send({"rate","",gtk_toggle_button_get_active(b)?65536:0,0,0,0});}),this);
    g_signal_connect(bad,"clicked",G_CALLBACK(+[](GtkButton*,gpointer p){static_cast<Window*>(p)->Send({"broken","",0,0,0,0});}),this);
    g_signal_connect(export_button,"clicked",G_CALLBACK(+[](GtkButton*,gpointer p){static_cast<Window*>(p)->FileDialog(true);}),this);
    g_signal_connect(import_button,"clicked",G_CALLBACK(+[](GtkButton*,gpointer p){static_cast<Window*>(p)->FileDialog(false);}),this);
    g_signal_connect(window,"delete-event",G_CALLBACK((+[](GtkWidget*,GdkEvent*,gpointer p)->gboolean {auto& s=*static_cast<Window*>(p);s.closing=true;int x,y,w,h;gtk_window_get_position(GTK_WINDOW(s.window),&x,&y);gtk_window_get_size(GTK_WINDOW(s.window),&w,&h);if(s.ui)s.Send({"frame","",x,y,w,h});s.runtime.RequestStop();gtk_main_quit();return FALSE;})),this);
    gtk_widget_show_all(window);
  }
};
}
int RunNative(Options const& options) {
  gtk_init(nullptr,nullptr);Window app{options};app.Create();
  app.model=std::thread{[&] {Session session{options,app.runtime};app.result=session.Run([&](auto){g_idle_add(+[](gpointer p)->gboolean {auto& app=*static_cast<Window*>(p);if(!app.closing)app.Render();return G_SOURCE_REMOVE;},&app);});}};
  if(options.auto_close_ms)g_timeout_add(options.auto_close_ms,+[](gpointer p)->gboolean {gtk_window_close(GTK_WINDOW(static_cast<Window*>(p)->window));return G_SOURCE_REMOVE;},&app);
  gtk_main();app.runtime.RequestStop();app.runtime.AbandonUnconsumedPublicationsForTeardown();app.model.join();return app.result;
}
}
