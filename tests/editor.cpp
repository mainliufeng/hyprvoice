// A real GTK text editor used as an isolated desktop acceptance surface.
#include <fstream>
#include <gtk/gtk.h>
#include <string>
struct Editor {
  GtkWidget *view;
  std::string output;
};
static void changed(GtkTextBuffer *b, gpointer data) {
  auto self = static_cast<Editor *>(data);
  GtkTextIter a, z;
  gtk_text_buffer_get_bounds(b, &a, &z);
  auto text = gtk_text_buffer_get_text(b, &a, &z, false);
  std::ofstream(self->output) << text;
  g_free(text);
}
int main(int argc, char **argv) {
  if (argc < 2)
    return 1;
  g_set_prgname("hyprvoice-test-editor");
  gtk_init();
  auto window = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(window), "Hyprvoice acceptance editor");
  gtk_window_set_default_size(GTK_WINDOW(window), 900, 480);
  auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_set_margin_top(box, 24);
  gtk_widget_set_margin_start(box, 24);
  gtk_widget_set_margin_end(box, 24);
  gtk_widget_set_margin_bottom(box, 24);
  gtk_window_set_child(GTK_WINDOW(window), box);
  auto label = gtk_label_new("Hyprvoice · 真实编辑器上屏测试");
  gtk_box_append(GTK_BOX(box), label);
  auto view = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
  gtk_widget_set_vexpand(view, true);
  gtk_box_append(GTK_BOX(box), view);
  Editor e{view, argv[1]};
  auto buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  g_signal_connect(buffer, "changed", G_CALLBACK(changed), &e);
  if (argc > 2)
    gtk_text_buffer_set_text(buffer, argv[2], -1);
  gtk_widget_grab_focus(view);
  gtk_window_present(GTK_WINDOW(window));
  // Let acceptance wait for the application's focus, not only compositor IPC.
  g_timeout_add(
      50,
      +[](gpointer data) -> gboolean {
        auto self = static_cast<Editor *>(data);
        auto window = GTK_WINDOW(gtk_widget_get_root(self->view));
        std::ofstream(self->output + ".focus")
            << (gtk_window_is_active(window) &&
                gtk_widget_has_focus(self->view));
        return G_SOURCE_CONTINUE;
      },
      &e);
  auto loop = g_main_loop_new(nullptr, false);
  g_signal_connect(window, "close-request",
                   G_CALLBACK(+[](GtkWindow *, gpointer p) -> gboolean {
                     g_main_loop_quit(static_cast<GMainLoop *>(p));
                     return false;
                   }),
                   loop);
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
}
