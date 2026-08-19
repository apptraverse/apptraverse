#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <gtk/gtk.h>

#include "linux_runtime.h"

namespace {

struct IdleJob {
  enum class Kind { kUid, kTranscript, kError };

  GtkWidget* window{nullptr};
  GtkWidget* local_uid{nullptr};
  GtkWidget* transcript_view{nullptr};
  GtkTextBuffer* transcript{nullptr};
  std::atomic<bool>* alive{nullptr};
  Kind kind{Kind::kTranscript};
  std::string text;
};

gboolean DispatchIdle(gpointer data) {
  auto* job = static_cast<IdleJob*>(data);
  if (job->alive != nullptr && job->alive->load(std::memory_order::acquire)) {
    if (job->kind == IdleJob::Kind::kUid && job->local_uid != nullptr) {
      gtk_entry_set_text(GTK_ENTRY(job->local_uid), job->text.c_str());
    } else if (job->kind == IdleJob::Kind::kTranscript &&
               job->transcript != nullptr) {
      gtk_text_buffer_set_text(job->transcript, job->text.c_str(), -1);
      GtkTextIter end{};
      gtk_text_buffer_get_end_iter(job->transcript, &end);
      gtk_text_buffer_place_cursor(job->transcript, &end);
      if (job->transcript_view != nullptr) {
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(job->transcript_view), &end,
                                     0.0, FALSE, 0.0, 0.0);
      }
    } else if (job->kind == IdleJob::Kind::kError) {
      GtkWidget* dialog = gtk_message_dialog_new(
          job->window != nullptr ? GTK_WINDOW(job->window) : nullptr,
          GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s",
          job->text.c_str());
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
    }
  }
  delete job;
  return G_SOURCE_REMOVE;
}

std::string Trim(std::string_view text) {
  auto const first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  auto const last = text.find_last_not_of(" \t\r\n");
  return std::string{text.substr(first, last - first + 1)};
}

std::filesystem::path DefaultStateDir() {
  if (char const* xdg = std::getenv("XDG_DATA_HOME");
      xdg != nullptr && xdg[0] != '\0') {
    return std::filesystem::path{xdg} / "apptraverse" / "single-client-chat";
  }
  if (char const* home = std::getenv("HOME");
      home != nullptr && home[0] != '\0') {
    return std::filesystem::path{home} / ".local" / "share" / "apptraverse" /
           "single-client-chat";
  }
  return std::filesystem::path{"state"};
}

std::filesystem::path ParseStateDir(int* argc, char*** argv) {
  auto path = DefaultStateDir();
  int in_count = *argc;
  char** in_argv = *argv;
  int out = 1;
  for (int i = 1; i < in_count; ++i) {
    if (std::string_view{in_argv[i]} == "--state-dir") {
      if (i + 1 >= in_count) {
        std::cerr << "Missing value for --state-dir\n";
        std::exit(1);
      }
      path = in_argv[++i];
      continue;
    }
    in_argv[out++] = in_argv[i];
  }
  in_argv[out] = nullptr;
  *argc = out;
  return path;
}

struct ShellWidgets {
  GtkWidget* window{nullptr};
  GtkWidget* local_uid{nullptr};
  GtkWidget* remote_entry{nullptr};
  GtkWidget* message_entry{nullptr};
  GtkWidget* transcript_view{nullptr};
  GtkTextBuffer* transcript{nullptr};
  std::atomic<bool> alive{true};
  apptraverse::linux_host::LinuxRuntime* runtime{nullptr};
  std::thread* core_thread{nullptr};
};

void PostIdle(ShellWidgets* widgets, IdleJob::Kind kind, std::string text) {
  auto* job = new IdleJob{};
  job->window = widgets->window;
  job->local_uid = widgets->local_uid;
  job->transcript_view = widgets->transcript_view;
  job->transcript = widgets->transcript;
  job->alive = &widgets->alive;
  job->kind = kind;
  job->text = std::move(text);
  g_idle_add(DispatchIdle, job);
}

void OnAddClicked(GtkButton*, gpointer user_data) {
  auto* widgets = static_cast<ShellWidgets*>(user_data);
  if (widgets->runtime == nullptr) {
    return;
  }
  auto const uid =
      Trim(gtk_entry_get_text(GTK_ENTRY(widgets->remote_entry)));
  if (uid.empty()) {
    return;
  }
  auto const local =
      Trim(gtk_entry_get_text(GTK_ENTRY(widgets->local_uid)));
  if (!local.empty() && uid == local) {
    return;
  }
  if (widgets->runtime->QueueAddPeer(uid)) {
    gtk_entry_set_text(GTK_ENTRY(widgets->remote_entry), "");
  }
}

void OnSendClicked(GtkButton*, gpointer user_data) {
  auto* widgets = static_cast<ShellWidgets*>(user_data);
  if (widgets->runtime == nullptr) {
    return;
  }
  auto const message =
      Trim(gtk_entry_get_text(GTK_ENTRY(widgets->message_entry)));
  if (message.empty()) {
    return;
  }
  if (widgets->runtime->QueueSend(message)) {
    gtk_entry_set_text(GTK_ENTRY(widgets->message_entry), "");
  }
}

gboolean OnMessageActivate(GtkEntry*, gpointer user_data) {
  OnSendClicked(nullptr, user_data);
  return TRUE;
}

void OnDestroy(GtkWidget*, gpointer user_data) {
  auto* widgets = static_cast<ShellWidgets*>(user_data);
  widgets->alive.store(false, std::memory_order::release);
  if (widgets->runtime != nullptr) {
    widgets->runtime->Stop();
  }
  if (widgets->core_thread != nullptr && widgets->core_thread->joinable()) {
    widgets->core_thread->join();
  }
  gtk_main_quit();
}

}  // namespace

int main(int argc, char** argv) {
  auto const state_dir = ParseStateDir(&argc, &argv);
  gtk_init(&argc, &argv);

  GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "App Traverse Chat");
  gtk_window_set_default_size(GTK_WINDOW(window), 720, 520);

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(root), 12);
  gtk_container_add(GTK_CONTAINER(window), root);

  GtkWidget* local_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* local_caption = gtk_label_new("Local UID:");
  GtkWidget* local_uid = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(local_uid), "");
  gtk_editable_set_editable(GTK_EDITABLE(local_uid), FALSE);
  gtk_widget_set_can_focus(local_uid, TRUE);
  gtk_widget_set_hexpand(local_uid, TRUE);
  gtk_box_pack_start(GTK_BOX(local_row), local_caption, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(local_row), local_uid, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(root), local_row, FALSE, FALSE, 0);

  GtkWidget* remote_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* remote_caption = gtk_label_new("Remote UID:");
  GtkWidget* remote_entry = gtk_entry_new();
  GtkWidget* add_button = gtk_button_new_with_label("Add participant");
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

  ShellWidgets widgets{};
  widgets.window = window;
  widgets.local_uid = local_uid;
  widgets.remote_entry = remote_entry;
  widgets.message_entry = message_entry;
  widgets.transcript_view = transcript_view;
  widgets.transcript = transcript;

  apptraverse::linux_host::UiSink sink{};
  sink.post_local_uid = [&widgets](std::string uid) {
    PostIdle(&widgets, IdleJob::Kind::kUid, std::move(uid));
  };
  sink.post_transcript = [&widgets](std::string text) {
    PostIdle(&widgets, IdleJob::Kind::kTranscript, std::move(text));
  };
  sink.post_error = [&widgets](std::string text) {
    PostIdle(&widgets, IdleJob::Kind::kError, std::move(text));
  };

  apptraverse::linux_host::LinuxRuntime runtime{state_dir.string(),
                                               std::move(sink)};
  widgets.runtime = &runtime;
  std::thread core_thread{[&runtime]() { runtime.Run(); }};
  widgets.core_thread = &core_thread;

  g_signal_connect(window, "destroy", G_CALLBACK(OnDestroy), &widgets);
  g_signal_connect(add_button, "clicked", G_CALLBACK(OnAddClicked), &widgets);
  g_signal_connect(send_button, "clicked", G_CALLBACK(OnSendClicked),
                   &widgets);
  g_signal_connect(message_entry, "activate", G_CALLBACK(OnMessageActivate),
                   &widgets);

  gtk_widget_show_all(window);
  gtk_main();
  return 0;
}
