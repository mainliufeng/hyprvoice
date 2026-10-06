#include "context.h"
#include <algorithm>
#include <atspi/atspi.h>
#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>
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

constexpr int MaxTargetCharacters = 65536;
Json UnknownTarget() {
  return {{"available", false},
          {"protected", false},
          {"reliable", false},
          {"toolkit", ""},
          {"control", {{"bus", ""}, {"path", ""}}},
          {"caret", -1},
          {"selections", Json::array()},
          {"selection_digests", Json::array()},
          {"characters", -1},
          {"digest", ""},
          {"reason", "zero-focus"},
          {"stage", ""}};
}

// A focused GTK entry may also have focused ancestors. Only the deepest
// focused descendant is authoritative; unrelated focused nodes are ambiguous.
struct FocusedNode {
  AtspiAccessible *node;
  std::vector<int> route;
  AtspiRole role;
  bool editable, defunct;
};
struct TargetQuery {
  int remaining = 256, serial = 0;
  bool complete = true, protected_field = false;
  const char *stage = "";
  void fail(const char *where) {
    complete = false;
    if (!*stage)
      stage = where;
  }
  std::chrono::steady_clock::time_point until =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(1050);
  std::vector<FocusedNode> focused;
  ~TargetQuery() {
    for (auto &entry : focused)
      g_object_unref(entry.node);
  }
  bool room(int depth) {
    if (remaining-- <= 0 || depth > 32 ||
        std::chrono::steady_clock::now() >= until) {
      fail("budget");
      return false;
    }
    return true;
  }
  void walk(AtspiAccessible *node, std::vector<int> route = {},
            bool protected_ancestor = false) {
    if (!node || !room(route.size())) {
      fail("node");
      return;
    }
    route.push_back(++serial);
    atspi_accessible_clear_cache_single(node);
    GError *error = nullptr;
    auto role = atspi_accessible_get_role(node, &error);
    if (error) {
      fail("node-role");
      g_clear_error(&error);
      return;
    }
    auto states = atspi_accessible_get_state_set(node);
    if (!states) {
      fail("node-states");
      return;
    }
    bool is_focused = atspi_state_set_contains(states, ATSPI_STATE_FOCUSED);
    bool editable = atspi_state_set_contains(states, ATSPI_STATE_EDITABLE);
    bool defunct = atspi_state_set_contains(states, ATSPI_STATE_DEFUNCT);
    g_object_unref(states);
    bool protected_node =
        protected_ancestor || role == ATSPI_ROLE_PASSWORD_TEXT;
    if (is_focused) {
      if (protected_node)
        protected_field = true;
      focused.push_back({static_cast<AtspiAccessible *>(g_object_ref(node)),
                         route, role, editable, defunct});
    }
    // Descendants of password controls are traversed only for focus/role
    // metadata. No Text interface or value is obtained anywhere in this walk.
    int count = atspi_accessible_get_child_count(node, &error);
    if (error || count < 0) {
      fail("node-children");
      g_clear_error(&error);
      return;
    }
    for (int i = 0; i < count; ++i) {
      if (!complete)
        break;
      auto child = atspi_accessible_get_child_at_index(node, i, &error);
      if (error || !child) {
        fail("node-child");
        g_clear_error(&error);
        break;
      }
      walk(child, route, protected_node);
      g_object_unref(child);
    }
  }
};

bool ReadTargetText(AtspiAccessible *node, AtspiText *text, Json &snapshot,
                    bool &protected_field) {
  GError *error = nullptr;
  atspi_accessible_clear_cache_single(node);
  auto role = atspi_accessible_get_role(node, &error);
  if (!error && role == ATSPI_ROLE_PASSWORD_TEXT)
    protected_field = true;
  auto states = atspi_accessible_get_state_set(node);
  bool eligible = !error && !protected_field && states &&
                  (role == ATSPI_ROLE_TEXT || role == ATSPI_ROLE_ENTRY) &&
                  atspi_state_set_contains(states, ATSPI_STATE_FOCUSED) &&
                  atspi_state_set_contains(states, ATSPI_STATE_EDITABLE) &&
                  !atspi_state_set_contains(states, ATSPI_STATE_DEFUNCT);
  if (states)
    g_object_unref(states);
  if (!eligible) {
    g_clear_error(&error);
    return false;
  }
  auto metadata = [&]() -> Json {
    int characters = atspi_text_get_character_count(text, &error);
    int caret = error ? -1 : atspi_text_get_caret_offset(text, &error);
    int selections = error ? -1 : atspi_text_get_n_selections(text, &error);
    if (error || characters < 0 || characters > MaxTargetCharacters ||
        caret < 0 || caret > characters || selections < 0 || selections > 64)
      return Json();
    Json ranges = Json::array();
    for (int i = 0; i < selections; ++i) {
      auto range = atspi_text_get_selection(text, i, &error);
      bool valid = !error && range && range->start_offset >= 0 &&
                   range->start_offset <= range->end_offset &&
                   range->end_offset <= characters;
      if (valid)
        ranges.push_back({range->start_offset, range->end_offset});
      g_free(range);
      if (!valid)
        return Json();
    }
    return {
        {"characters", characters}, {"caret", caret}, {"selections", ranges}};
  };
  auto before = metadata();
  int characters = before.is_object() ? before.at("characters").get<int>() : -1;
  char *value = characters >= 0
                    ? atspi_text_get_text(text, 0, characters, &error)
                    : nullptr;
  bool valid = !error && value && g_utf8_validate(value, -1, nullptr) &&
               g_utf8_strlen(value, -1) == characters;
  // Bracket each full-text read with caret/count/all-selection metadata. The
  // second whole snapshot also checks equal-length text edits via its digest.
  auto after = valid ? metadata() : Json();
  valid = valid && !error && before == after;
  if (valid) {
    char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, value, -1);
    valid = digest != nullptr;
    Json selected = Json::array();
    for (const auto &range : before.at("selections")) {
      const char *start = g_utf8_offset_to_pointer(value, range[0].get<int>());
      const char *end = g_utf8_offset_to_pointer(value, range[1].get<int>());
      char *part = g_compute_checksum_for_data(
          G_CHECKSUM_SHA256, reinterpret_cast<const guchar *>(start),
          end - start);
      valid = valid && part;
      if (part)
        selected.push_back(part);
      g_free(part);
    }
    if (valid) {
      snapshot = before;
      snapshot["digest"] = digest;
      snapshot["selection_digests"] = selected;
    }
    g_free(digest);
  }
  g_free(value);
  g_clear_error(&error);
  return valid;
}

bool ValidTarget(const Json &target) {
  if (!target.is_object() || target.at("reliable") != true ||
      target.at("available") != true || target.at("protected") != false ||
      target.at("toolkit") != "gtk")
    return false;
  const auto &control = target.at("control");
  if (!control.is_object() || !control.at("bus").is_string() ||
      !control.at("path").is_string() ||
      !control.at("bus").get_ref<const std::string &>().starts_with(':') ||
      !control.at("path").get_ref<const std::string &>().starts_with('/'))
    return false;
  if (!target.at("caret").is_number_integer() ||
      !target.at("characters").is_number_integer())
    return false;
  auto characters = target.at("characters").get<int64_t>();
  auto caret = target.at("caret").get<int64_t>();
  if (characters < 0 || characters > MaxTargetCharacters || caret < 0 ||
      caret > characters || !target.at("selections").is_array() ||
      target.at("selections").size() > 64 || !target.at("digest").is_string() ||
      !target.at("selection_digests").is_array() ||
      target.at("selection_digests").size() != target.at("selections").size())
    return false;
  const auto &digest = target.at("digest").get_ref<const std::string &>();
  if (digest.size() != 64 ||
      digest.find_first_not_of("0123456789abcdef") != std::string::npos)
    return false;
  for (auto &range : target.at("selections")) {
    if (!range.is_array() || range.size() != 2 ||
        !range[0].is_number_integer() || !range[1].is_number_integer())
      return false;
    auto start = range[0].get<int64_t>(), end = range[1].get<int64_t>();
    if (start < 0 || start > end || end > characters)
      return false;
  }
  for (const auto &part : target.at("selection_digests")) {
    if (!part.is_string())
      return false;
    const auto &value = part.get_ref<const std::string &>();
    if (value.size() != 64 ||
        value.find_first_not_of("0123456789abcdef") != std::string::npos)
      return false;
  }
  return true;
}

struct TargetWatch {
  std::string bus, path;
  bool changed = false, protected_field = false;
  void mark(bool is_protected = false) {
    bool notify = !changed || (is_protected && !protected_field);
    changed = true;
    protected_field = protected_field || is_protected;
    if (notify)
      std::cout
          << Json{{"changed", true}, {"protected", protected_field}}.dump()
          << std::endl;
  }
  void event(AtspiEvent *event) {
    if (!event || !event->source || !event->type)
      return;
    auto object = ATSPI_OBJECT(event->source);
    if (!object->app || !object->app->bus_name || !object->path ||
        bus != object->app->bus_name)
      return;
    bool original = path == object->path;
    std::string_view type(event->type);
    if (type.starts_with("object:text-changed") ||
        type.starts_with("object:text-caret-moved") ||
        type.starts_with("object:text-selection-changed")) {
      if (original)
        mark();
      return;
    }
    if (!type.starts_with("object:state-changed:focused"))
      return;
    if (original) {
      if (!event->detail1)
        mark();
      return;
    }
    if (!event->detail1)
      return;
    // The listener is scoped to the PID-verified application. Only role/state
    // and ancestry are examined; event.any_data may contain text and is
    // ignored.
    GError *error = nullptr;
    atspi_accessible_clear_cache_single(event->source);
    auto role = atspi_accessible_get_role(event->source, &error);
    auto states = atspi_accessible_get_state_set(event->source);
    bool editable =
        states && atspi_state_set_contains(states, ATSPI_STATE_EDITABLE);
    if (states)
      g_object_unref(states);
    bool is_protected = role == ATSPI_ROLE_PASSWORD_TEXT;
    auto node = static_cast<AtspiAccessible *>(g_object_ref(event->source));
    for (int depth = 0; node && !error && !is_protected && depth < 32;
         ++depth) {
      atspi_accessible_clear_cache_single(node);
      is_protected =
          atspi_accessible_get_role(node, &error) == ATSPI_ROLE_PASSWORD_TEXT;
      auto parent = !error && !is_protected
                        ? atspi_accessible_get_parent(node, &error)
                        : nullptr;
      g_object_unref(node);
      node = parent;
    }
    if (node)
      g_object_unref(node);
    if (error || editable || is_protected)
      mark(is_protected);
    g_clear_error(&error);
  }
};

void TargetEvent(AtspiEvent *event, void *data) {
  try {
    static_cast<TargetWatch *>(data)->event(event);
  } catch (...) {
    // No diagnostic payload: errors are a sticky uncertainty, not evidence of
    // an unchanged editor. In particular never inspect/log event text data.
    static_cast<TargetWatch *>(data)->mark();
  }
  if (event)
    g_boxed_free(ATSPI_TYPE_EVENT, event);
}
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

bool EquivalentInputTarget(const Json &first, const Json &second) {
  try {
    if (!ValidTarget(first) || !ValidTarget(second))
      return false;
    for (const char *key : {"toolkit", "control", "caret", "selections",
                            "characters", "digest", "selection_digests"})
      if (first.at(key) != second.at(key))
        return false;
    return true;
  } catch (const Json::exception &) {
    return false;
  }
}

Json ReadInputTarget(int pid) {
  auto result = UnknownTarget();
  if (pid <= 0)
    return result;
  TargetQuery query;
  atspi_set_timeout(80, 150);
  if (atspi_init() != 0) {
    result["reason"] = "incomplete";
    result["stage"] = "init";
    return result;
  }
  auto desktop = atspi_get_desktop(0);
  if (!desktop)
    query.fail("desktop");
  else {
    GError *error = nullptr;
    int count = atspi_accessible_get_child_count(desktop, &error);
    if (error || count < 0)
      query.fail("desktop-children");
    g_clear_error(&error);
    for (int i = 0; query.complete && i < count; ++i) {
      if (std::chrono::steady_clock::now() >= query.until) {
        query.fail("budget");
        break;
      }
      auto app = atspi_accessible_get_child_at_index(desktop, i, &error);
      if (!error && app) {
        auto process = atspi_accessible_get_process_id(app, &error);
        if (!error && process == static_cast<guint>(pid))
          query.walk(app);
      }
      if (error || !app)
        query.fail(app ? "app-pid" : "desktop-child");
      g_clear_error(&error);
      if (app)
        g_object_unref(app);
    }
    g_object_unref(desktop);
  }
  result["protected"] = query.protected_field;
  std::vector<const FocusedNode *> leaves;
  for (const auto &entry : query.focused) {
    bool ancestor = false;
    for (const auto &other : query.focused)
      if (entry.route.size() < other.route.size() &&
          std::equal(entry.route.begin(), entry.route.end(),
                     other.route.begin()))
        ancestor = true;
    if (!ancestor)
      leaves.push_back(&entry);
  }
  if (query.complete && !query.protected_field && leaves.size() == 1) {
    const auto &entry = *leaves.front();
    GError *error = nullptr;
    // ToolkitName belongs to the Application interface on the application
    // root, not to a TextView's accessible object.
    auto application = atspi_accessible_get_application(entry.node, &error);
    char *toolkit = !error && application
                        ? atspi_accessible_get_toolkit_name(application, &error)
                        : nullptr;
    if (application)
      g_object_unref(application);
    if (!error && toolkit) {
      char *lower = g_ascii_strdown(toolkit, -1);
      result["toolkit"] = lower;
      g_free(lower);
    }
    g_free(toolkit);
    result["reason"] = "toolkit";
    atspi_accessible_clear_cache_single(entry.node);
    auto current_role = error ? ATSPI_ROLE_INVALID
                              : atspi_accessible_get_role(entry.node, &error);
    if (!error && current_role == ATSPI_ROLE_PASSWORD_TEXT)
      query.protected_field = true;
    bool eligible =
        !error && !query.protected_field && current_role == entry.role &&
        result.at("toolkit") == "gtk" && entry.editable && !entry.defunct &&
        (entry.role == ATSPI_ROLE_TEXT || entry.role == ATSPI_ROLE_ENTRY);
    g_clear_error(&error);
    auto object = ATSPI_OBJECT(entry.node);
    eligible = eligible && object->app && object->app->bus_name && object->path;
    if (result.at("toolkit") == "gtk")
      result["reason"] = "unsupported-role";
    if (eligible) {
      result["control"] = {{"bus", object->app->bus_name},
                           {"path", object->path}};
      auto text = atspi_accessible_get_text_iface(entry.node);
      Json first, second;
      bool read =
          text &&
          ReadTargetText(entry.node, text, first, query.protected_field) &&
          std::chrono::steady_clock::now() < query.until &&
          ReadTargetText(entry.node, text, second, query.protected_field);
      bool stable = read && first == second &&
                    std::chrono::steady_clock::now() < query.until;
      result["reason"] = read ? "unstable" : "text-read";
      atspi_accessible_clear_cache_single(entry.node);
      auto role = atspi_accessible_get_role(entry.node, &error);
      if (!error && role == ATSPI_ROLE_PASSWORD_TEXT)
        query.protected_field = true;
      auto states = atspi_accessible_get_state_set(entry.node);
      stable = stable && states && !error && !query.protected_field &&
               (role == ATSPI_ROLE_TEXT || role == ATSPI_ROLE_ENTRY) &&
               atspi_state_set_contains(states, ATSPI_STATE_FOCUSED) &&
               atspi_state_set_contains(states, ATSPI_STATE_EDITABLE) &&
               !atspi_state_set_contains(states, ATSPI_STATE_DEFUNCT);
      if (stable) {
        result.update(second);
        result["available"] = true;
        result["reliable"] = true;
        result["reason"] = "";
      }
      if (states)
        g_object_unref(states);
      if (text)
        g_object_unref(text);
      g_clear_error(&error);
    }
  }
  result["protected"] = query.protected_field;
  if (query.protected_field)
    result["reason"] = "protected";
  else if (!query.complete) {
    result["reason"] = "incomplete";
    result["stage"] = query.stage;
  } else if (leaves.size() > 1)
    result["reason"] = "multiple-focus";
  atspi_exit();
  return result;
}

void WatchInputTarget(int pid, const Json &control) {
  TargetWatch watch;
  bool ready = false;
  AtspiEventListener *listener = nullptr;
  AtspiAccessible *application = nullptr;
  bool initialized = false;
  try {
    watch.bus = control.at("bus").get<std::string>();
    watch.path = control.at("path").get<std::string>();
    initialized = pid > 0 && watch.bus.starts_with(':') &&
                  watch.path.starts_with('/') && atspi_init() == 0;
    if (initialized) {
      atspi_set_timeout(80, 150);
      auto desktop = atspi_get_desktop(0);
      auto until =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(850);
      GError *error = nullptr;
      int count =
          desktop ? atspi_accessible_get_child_count(desktop, &error) : 0;
      for (int i = 0; !error && i < count && !application &&
                      std::chrono::steady_clock::now() < until;
           ++i) {
        auto app = atspi_accessible_get_child_at_index(desktop, i, &error);
        if (app && !error) {
          auto object = ATSPI_OBJECT(app);
          if (object->app && object->app->bus_name &&
              watch.bus == object->app->bus_name &&
              atspi_accessible_get_process_id(app, &error) ==
                  static_cast<guint>(pid) &&
              !error)
            application = static_cast<AtspiAccessible *>(g_object_ref(app));
        }
        if (app)
          g_object_unref(app);
      }
      g_clear_error(&error);
      if (desktop)
        g_object_unref(desktop);
      if (application) {
        listener = atspi_event_listener_new(TargetEvent, &watch, nullptr);
        ready = listener != nullptr;
        for (const char *type :
             {"object:state-changed:focused", "object:text-caret-moved",
              "object:text-changed", "object:text-selection-changed"}) {
          if (ready)
            ready = atspi_event_listener_register_with_app(
                        listener, type, nullptr, application, &error) &&
                    !error;
        }
        g_clear_error(&error);
      }
    }
  } catch (...) {
    ready = false;
  }
  std::cout << Json{{"ready", ready}}.dump() << std::endl;
  auto heartbeat = g_timeout_add(
      250,
      +[](gpointer) -> gboolean {
        std::cout << "{\"alive\":true}" << std::endl;
        return G_SOURCE_CONTINUE;
      },
      nullptr);
  // A failed subscription is explicitly reported and remains alive for parent
  // cleanup; process exit/dead pipes must also be fail-closed in the parent.
  if (initialized)
    atspi_event_main();
  else {
    auto loop = g_main_loop_new(nullptr, false);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
  }
  if (listener)
    g_object_unref(listener);
  g_source_remove(heartbeat);
  if (application)
    g_object_unref(application);
  if (initialized)
    atspi_exit();
}
} // namespace hv
