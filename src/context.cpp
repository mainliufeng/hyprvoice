#include "context.h"
#include <algorithm>
#include <atspi/atspi.h>
#include <chrono>
namespace hv {
namespace {
struct Query {
  int remaining = 256, max_chars;
  std::chrono::steady_clock::time_point until;
  Json result = {{"available", false}, {"protected", false}, {"text", ""}};
  void walk(AtspiAccessible *node, int depth = 0) {
    if (!node || result.value("available", false) || remaining-- <= 0 ||
        depth > 32 || std::chrono::steady_clock::now() >= until)
      return;
    // Never fetch the value of a password control, including its descendants.
    auto role = atspi_accessible_get_role(node, nullptr);
    auto states = atspi_accessible_get_state_set(node);
    bool focused =
        states && atspi_state_set_contains(states, ATSPI_STATE_FOCUSED);
    bool editable =
        states && atspi_state_set_contains(states, ATSPI_STATE_EDITABLE);
    if (states)
      g_object_unref(states);
    if (role == ATSPI_ROLE_PASSWORD_TEXT) {
      if (focused)
        result["protected"] = true;
      return;
    }
    if (focused && editable) {
      auto text = atspi_accessible_get_text_iface(node);
      if (text) {
        GError *error = nullptr;
        int caret = atspi_text_get_caret_offset(text, &error);
        // When text is selected, insertion/replacement starts at its beginning.
        if (!error && atspi_text_get_n_selections(text, nullptr) > 0) {
          auto range = atspi_text_get_selection(text, 0, nullptr);
          if (range) {
            caret = std::min(caret, range->start_offset);
            g_free(range);
          }
        }
        char *value = nullptr;
        if (!error && caret >= 0)
          value = atspi_text_get_text(text, std::max(0, caret - max_chars),
                                      caret, &error);
        if (!error && value && g_utf8_validate(value, -1, nullptr)) {
          result = {{"available", true},
                    {"protected", false},
                    {"text", value},
                    {"caret", caret}};
        }
        g_clear_error(&error);
        g_free(value);
        g_object_unref(text);
      }
      return;
    }
    int count = atspi_accessible_get_child_count(node, nullptr);
    for (int i = 0; i < count && remaining > 0; ++i) {
      if (std::chrono::steady_clock::now() >= until)
        break;
      auto child = atspi_accessible_get_child_at_index(node, i, nullptr);
      walk(child, depth + 1);
      if (child)
        g_object_unref(child);
      if (result.value("available", false))
        break;
    }
  }
};
} // namespace
Json ReadInputContext(int pid, int max_chars) {
  Query q{256, std::clamp(max_chars, 1, 2048),
          std::chrono::steady_clock::now() + std::chrono::milliseconds(850)};
  atspi_set_timeout(80, 150);
  if (atspi_init() != 0)
    return q.result;
  auto desktop = atspi_get_desktop(0);
  if (desktop) {
    int count = atspi_accessible_get_child_count(desktop, nullptr);
    for (int i = 0; i < count && std::chrono::steady_clock::now() < q.until;
         ++i) {
      auto app = atspi_accessible_get_child_at_index(desktop, i, nullptr);
      if (app) {
        if (atspi_accessible_get_process_id(app, nullptr) ==
            static_cast<guint>(pid))
          q.walk(app);
        g_object_unref(app);
      }
      if (q.result.value("available", false) ||
          q.result.value("protected", false))
        break;
    }
    g_object_unref(desktop);
  }
  atspi_exit();
  return q.result;
}
} // namespace hv
