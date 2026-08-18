#include <gtk/gtk.h>

#include <string>
#include <string_view>

namespace {

constexpr char kLocalUid[] = "linux-demo-local-uid";
constexpr char kJoinedLine[] = "* Linux joined";

struct ShellWidgets {
  GtkWidget* remote_entry{nullptr};
  GtkWidget* message_entry{nullptr};
  GtkTextBuffer* transcript{nullptr};
};

std::string Trim(std::string_view text) {
  auto const first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  auto const last = text.find_last_not_of(" \t\r\n");
  return std::string{text.substr(first, last - first + 1)};
}

void AppendTranscriptLine(GtkTextBuffer* buffer, std::string const& line) {
  GtkTextIter end{};
  gtk_text_buffer_get_end_iter(buffer, &end);
  auto const text = line + '\n';
  gtk_text_buffer_insert(buffer, &end, text.c_str(), -1);
  gtk_text_buffer_get_end_iter(buffer, &end);
  gtk_text_buffer_place_cursor(buffer, &end);
}

void OnAddClicked(GtkButton*, gpointer user_data) {
  auto* widgets = static_cast<ShellWidgets*>(user_data);
  auto const uid =
      Trim(gtk_entry_get_text(GTK_ENTRY(widgets->remote_entry)));
  if (uid.empty()) {
    return;
  }
  AppendTranscriptLine(widgets->transcript, "* Peer added: " + uid);
}

void OnSendClicked(GtkButton*, gpointer user_data) {
  auto* widgets = static_cast<ShellWidgets*>(user_data);
  auto const message =
      Trim(gtk_entry_get_text(GTK_ENTRY(widgets->message_entry)));
  if (message.empty()) {
    return;
  }
  AppendTranscriptLine(widgets->transcript, "Linux: " + message);
  gtk_entry_set_text(GTK_ENTRY(widgets->message_entry), "");
}

gboolean OnMessageActivate(GtkEntry*, gpointer user_data) {
  OnSendClicked(nullptr, user_data);
  return TRUE;
}

}  // namespace

int main(int argc, char** argv) {
  gtk_init(&argc, &argv);

  GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "App Traverse Chat");
  gtk_window_set_default_size(GTK_WINDOW(window), 560, 440);
  g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(root), 12);
  gtk_container_add(GTK_CONTAINER(window), root);

  GtkWidget* local_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* local_caption = gtk_label_new("Local UID:");
  GtkWidget* local_uid = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(local_uid), kLocalUid);
  gtk_editable_set_editable(GTK_EDITABLE(local_uid), FALSE);
  gtk_widget_set_can_focus(local_uid, FALSE);
  gtk_widget_set_hexpand(local_uid, TRUE);
  gtk_box_pack_start(GTK_BOX(local_row), local_caption, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(local_row), local_uid, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(root), local_row, FALSE, FALSE, 0);

  GtkWidget* remote_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* remote_caption = gtk_label_new("Remote UID:");
  GtkWidget* remote_entry = gtk_entry_new();
  GtkWidget* add_button = gtk_button_new_with_label("Add");
  gtk_widget_set_hexpand(remote_entry, TRUE);
  gtk_box_pack_start(GTK_BOX(remote_row), remote_caption, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(remote_row), remote_entry, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(remote_row), add_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(root), remote_row, FALSE, FALSE, 0);

  GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scrolled, TRUE);
  gtk_widget_set_hexpand(scrolled, TRUE);
  GtkWidget* transcript_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(transcript_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(transcript_view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(transcript_view), GTK_WRAP_WORD_CHAR);
  GtkTextBuffer* transcript =
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(transcript_view));
  gtk_container_add(GTK_CONTAINER(scrolled), transcript_view);
  gtk_box_pack_start(GTK_BOX(root), scrolled, TRUE, TRUE, 0);

  GtkWidget* message_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* message_entry = gtk_entry_new();
  GtkWidget* send_button = gtk_button_new_with_label("Send");
  gtk_widget_set_hexpand(message_entry, TRUE);
  gtk_box_pack_start(GTK_BOX(message_row), message_entry, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(message_row), send_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(root), message_row, FALSE, FALSE, 0);

  gtk_text_buffer_set_text(transcript, (std::string{kJoinedLine} + '\n').c_str(),
                           -1);

  ShellWidgets widgets{};
  widgets.remote_entry = remote_entry;
  widgets.message_entry = message_entry;
  widgets.transcript = transcript;

  g_signal_connect(add_button, "clicked", G_CALLBACK(OnAddClicked), &widgets);
  g_signal_connect(send_button, "clicked", G_CALLBACK(OnSendClicked),
                   &widgets);
  g_signal_connect(message_entry, "activate", G_CALLBACK(OnMessageActivate),
                   &widgets);

  gtk_widget_show_all(window);
  gtk_main();
  return 0;
}
