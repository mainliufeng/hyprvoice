#include "context.h"
#include "browser_text.h"
#include <algorithm>
#include <atspi/atspi.h>
#include <chrono>
#include <iostream>
#include <map>
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
  std::map<std::string, int> browser_routes;
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
  // Chromium also exposes focused objects in hidden native windows. Query
  // focus metadata server-side so long chat documents do not exhaust a full
  // tree walk, then require ancestry in an active frame and a web document.
  // No names, URLs, document text or password values are fetched here.
  bool browserRoute(AtspiAccessible *node, AtspiAccessible *app,
                    std::vector<int> &route, bool &is_protected,
                    bool *native_focus = nullptr) {
    auto root = ATSPI_OBJECT(app);
    if (!root->app || !root->app->bus_name || !root->path)
      return false;
    auto current = static_cast<AtspiAccessible *>(g_object_ref(node));
    bool document = false, active_frame = false, reached_root = false;
    for (int depth = 0; current && depth <= 32 && complete; ++depth) {
      if (!room(depth))
        break;
      atspi_accessible_clear_cache_single(current);
      auto object = ATSPI_OBJECT(current);
      if (!object->app || !object->app->bus_name || !object->path ||
          !root->app || !root->app->bus_name ||
          std::string_view(object->app->bus_name) != root->app->bus_name)
        break;
      auto key = std::string(object->app->bus_name) + object->path;
      auto [entry, inserted] = browser_routes.try_emplace(key, serial + 1);
      if (inserted)
        ++serial;
      route.push_back(entry->second);
      GError *error = nullptr;
      auto role = atspi_accessible_get_role(current, &error);
      auto states = atspi_accessible_get_state_set(current);
      if (error || !states) {
        fail("browser-ancestry");
        g_clear_error(&error);
        if (states)
          g_object_unref(states);
        break;
      }
      is_protected = is_protected || role == ATSPI_ROLE_PASSWORD_TEXT;
      document = document || role == ATSPI_ROLE_DOCUMENT_WEB;
      if (role == ATSPI_ROLE_FRAME) {
        bool active = atspi_state_set_contains(states, ATSPI_STATE_ACTIVE) &&
                      !atspi_state_set_contains(states, ATSPI_STATE_DEFUNCT);
        g_object_unref(states);
        // An inactive frame is never a path to the current web input.
        if (!active)
          break;
        active_frame = true;
      } else
        g_object_unref(states);
      if (std::string_view(object->path) == root->path) {
        reached_root = true;
        break;
      }
      auto parent = atspi_accessible_get_parent(current, &error);
      g_object_unref(current);
      current = parent;
      if (error) {
        fail("browser-parent");
        g_clear_error(&error);
      }
    }
    if (current)
      g_object_unref(current);
    std::reverse(route.begin(), route.end());
    if (native_focus)
      *native_focus = complete && reached_root && active_frame && !document;
    return complete && reached_root && document && active_frame;
  }
  void browser(AtspiAccessible *app) {
    // A normal Chromium launch builds web accessibility lazily. This standard
    // metadata request signals an assistive client; no browser flag, global
    // accessibility setting, restart or document value is required.
    GError *error = nullptr;
    auto attributes = atspi_accessible_get_attributes(app, &error);
    if (attributes)
      g_hash_table_unref(attributes);
    if (error) {
      fail("browser-metadata");
      g_clear_error(&error);
      return;
    }
    auto collection = atspi_accessible_get_collection_iface(app);
    if (!collection) {
      fail("browser-collection");
      return;
    }
    auto states = atspi_state_set_new(nullptr);
    atspi_state_set_add(states, ATSPI_STATE_FOCUSED);
    auto rule = atspi_match_rule_new(
        states, ATSPI_Collection_MATCH_ALL, nullptr, ATSPI_Collection_MATCH_ALL,
        nullptr, ATSPI_Collection_MATCH_ALL, nullptr,
        ATSPI_Collection_MATCH_ALL, false);
    for (int attempt = 0; attempt < 6; ++attempt) {
      auto matches = atspi_collection_get_matches(
          collection, rule, ATSPI_Collection_SORT_ORDER_CANONICAL, 65, true,
          &error);
      if (error || !matches || matches->len >= 65)
        fail("browser-focus");
      g_clear_error(&error);
      if (matches) {
        for (guint i = 0; i < matches->len; ++i) {
          auto node = g_array_index(matches, AtspiAccessible *, i);
          if (!node)
            fail("browser-focus-node");
          else if (complete) {
            std::vector<int> route;
            bool protected_node = false;
            bool native = false;
            bool web = browserRoute(node, app, route, protected_node, &native);
            if (web || native) {
              auto role = atspi_accessible_get_role(node, &error);
              auto current_states = atspi_accessible_get_state_set(node);
              if (error || !current_states)
                fail("browser-focus-state");
              else if (atspi_state_set_contains(current_states,
                                                ATSPI_STATE_FOCUSED)) {
                bool editable = atspi_state_set_contains(current_states,
                                                         ATSPI_STATE_EDITABLE);
                // Chromium may retain web focus while its address bar has
                // keyboard focus. Any active native editor makes the web
                // target ambiguous; never paste based on that retained focus.
                if (native && editable)
                  fail("browser-native-focus");
                if (web) {
                  protected_field = protected_field || protected_node;
                  focused.push_back(
                      {static_cast<AtspiAccessible *>(g_object_ref(node)),
                       route, role,
                       static_cast<bool>(atspi_state_set_contains(
                           current_states, ATSPI_STATE_EDITABLE)),
                       static_cast<bool>(atspi_state_set_contains(
                           current_states, ATSPI_STATE_DEFUNCT))});
                }
              }
              if (current_states)
                g_object_unref(current_states);
              g_clear_error(&error);
            }
          }
          if (node)
            g_object_unref(node);
        }
        g_array_free(matches, true);
      }
      bool editable =
          std::any_of(focused.begin(), focused.end(), [](const auto &node) {
            return node.editable && !node.defunct &&
                   (node.role == ATSPI_ROLE_ENTRY ||
                    node.role == ATSPI_ROLE_TEXT);
          });
      if (!complete || protected_field || editable || attempt == 5 ||
          std::chrono::steady_clock::now() >= until)
        break;
      // The first focus query may see only a lazily created document shell.
      // Re-query metadata within the same deadline, retaining no stale nodes.
      for (auto &entry : focused)
        g_object_unref(entry.node);
      focused.clear();
      g_usleep(20000);
    }
    g_object_unref(rule);
    g_object_unref(states);
    g_object_unref(collection);
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

std::vector<const FocusedNode *> FocusedLeaves(const TargetQuery &query) {
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
  return leaves;
}

bool ReadTargetText(AtspiAccessible *node, AtspiText *text, Json &snapshot,
                    bool &protected_field, bool browser = false,
                    std::chrono::steady_clock::time_point until = {},
                    std::string *content = nullptr) {
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
  if (browser)
    return ReadBrowserText(node, snapshot, protected_field, content, until);
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
      (target.at("toolkit") != "gtk" && target.at("toolkit") != "chromium"))
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
  if (target.at("toolkit") == "chromium") {
    const auto &version =
        target.at("editor_digest").get_ref<const std::string &>();
    if (version.size() != 64 ||
        version.find_first_not_of("0123456789abcdef") != std::string::npos)
      return false;
  }
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
  bool withinOriginal(AtspiAccessible *source) {
    auto node = static_cast<AtspiAccessible *>(g_object_ref(source));
    GError *error = nullptr;
    bool inside = false;
    for (int depth = 0; node && depth <= 32 && !error; ++depth) {
      atspi_accessible_clear_cache_single(node);
      auto object = ATSPI_OBJECT(node);
      if (!object->app || !object->app->bus_name || !object->path ||
          bus != object->app->bus_name)
        break;
      if (path == object->path) {
        inside = true;
        break;
      }
      auto parent = atspi_accessible_get_parent(node, &error);
      g_object_unref(node);
      node = parent;
    }
    if (node)
      g_object_unref(node);
    if (error)
      mark();
    g_clear_error(&error);
    return inside;
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
      if (original || withinOriginal(event->source))
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
            static_cast<guint>(pid)) {
          GError *toolkit_error = nullptr;
          char *toolkit =
              atspi_accessible_get_toolkit_name(app, &toolkit_error);
          char *lower = toolkit ? g_ascii_strdown(toolkit, -1) : nullptr;
          bool browser = lower && std::string_view(lower) == "chromium";
          bool known = !toolkit_error && lower;
          g_free(lower);
          g_free(toolkit);
          g_clear_error(&toolkit_error);
          if (!known) {
            // A failed toolkit query must not fall back to a generic tree
            // walk which could select a hidden browser window's context.
          } else if (!browser)
            q.walk(app);
          else {
            // Use the same focused web control as the local guard, never a
            // hidden browser window, address bar or unrelated chat history.
            TargetQuery query;
            query.until = q.until;
            query.browser(app);
            q.result["protected"] = query.protected_field;
            auto leaves = FocusedLeaves(query);
            if (query.complete && !query.protected_field &&
                leaves.size() == 1) {
              auto node = leaves.front()->node;
              auto text = atspi_accessible_get_text_iface(node);
              Json before, after;
              bool protected_field = false;
              GError *error = nullptr;
              std::string full;
              if (text && ReadTargetText(node, text, before, protected_field,
                                         true, query.until, &full)) {
                int caret = before.at("caret").get<int>();
                for (const auto &range : before.at("selections"))
                  caret = std::min(caret, range[0].get<int>());
                const char *start = g_utf8_offset_to_pointer(
                    full.c_str(), std::max(0, caret - q.max_chars));
                const char *end = g_utf8_offset_to_pointer(full.c_str(), caret);
                std::string value(start, end);
                std::vector<int> route;
                bool stable =
                    !error &&
                    ReadTargetText(node, text, after, protected_field, true,
                                   query.until) &&
                    before == after &&
                    query.browserRoute(node, app, route, protected_field) &&
                    route == leaves.front()->route && !protected_field;
                if (stable)
                  q.result = {{"available", true},
                              {"protected", false},
                              {"text", value},
                              {"caret", caret}};
              }
              q.result["protected"] = protected_field;
              g_clear_error(&error);
              if (text)
                g_object_unref(text);
            }
          }
        }
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
    if (first.at("toolkit") == "chromium" &&
        first.at("editor_digest") != second.at("editor_digest"))
      return false;
    return true;
  } catch (const Json::exception &) {
    return false;
  }
}

bool WindowOnlyInputTarget(const Json &target) {
  try {
    if (target.value("protected", false) || target.value("reliable", false) ||
        !target.value("selections", Json::array()).empty())
      return false;
    // Never downgrade a discovered editor with an unreadable/unstable text
    // snapshot or a positively identified non-editable/native browser control.
    auto control = target.value("control", Json::object());
    if (!control.value("bus", std::string()).empty())
      return false;
    auto reason = target.value("reason", std::string());
    const bool terminal = target.value("terminal_window", false) &&
                          (reason == "unsupported-role" || reason == "toolkit");
    return (reason.empty() || reason == "zero-focus" || reason == "incomplete" ||
            terminal) &&
           target.value("stage", std::string()) != "browser-native-focus";
  } catch (...) {
    return false;
  }
}

static Json ReadInputTargetOnce(int pid,
                                std::chrono::steady_clock::time_point until,
                                bool &saw_browser) {
  auto result = UnknownTarget();
  if (pid <= 0)
    return result;
  TargetQuery query;
  query.until = until;
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
        if (!error && process == static_cast<guint>(pid)) {
          char *toolkit = atspi_accessible_get_toolkit_name(app, &error);
          char *lower = toolkit ? g_ascii_strdown(toolkit, -1) : nullptr;
          bool browser =
              !error && lower && std::string_view(lower) == "chromium";
          saw_browser = saw_browser || browser;
          g_free(lower);
          g_free(toolkit);
          if (browser)
            query.browser(app);
          else if (!error)
            query.walk(app);
        }
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
  auto leaves = FocusedLeaves(query);
  if (query.complete && !query.protected_field && leaves.size() == 1) {
    const auto &entry = *leaves.front();
    GError *error = nullptr;
    // ToolkitName belongs to the Application interface on the application
    // root, not to a TextView's accessible object.
    auto application = atspi_accessible_get_application(entry.node, &error);
    char *toolkit = !error && application
                        ? atspi_accessible_get_toolkit_name(application, &error)
                        : nullptr;
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
        (result.at("toolkit") == "gtk" || result.at("toolkit") == "chromium") &&
        entry.editable && !entry.defunct &&
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
          ReadTargetText(entry.node, text, first, query.protected_field,
                         result.at("toolkit") == "chromium", query.until) &&
          std::chrono::steady_clock::now() < query.until &&
          ReadTargetText(entry.node, text, second, query.protected_field,
                         result.at("toolkit") == "chromium", query.until);
      bool stable = read && first == second &&
                    std::chrono::steady_clock::now() < query.until;
      if (stable && result.at("toolkit") == "chromium") {
        std::vector<int> route;
        stable = application &&
                 query.browserRoute(entry.node, application, route,
                                    query.protected_field) &&
                 route == entry.route;
      }
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
    if (application)
      g_object_unref(application);
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

Json ReadInputTarget(int pid) {
  auto until =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(1050);
  Json result = UnknownTarget();
  for (int attempt = 0; attempt < 3; ++attempt) {
    bool browser = false;
    result = ReadInputTargetOnce(pid, until, browser);
    if (!browser || result.value("reliable", false) ||
        result.value("protected", false) ||
        std::chrono::steady_clock::now() >= until)
      break;
    // Enabling Chromium's lazy web tree can replace its AT-SPI registration.
    // Re-enumerate the same PID rather than trusting the earlier bus object.
    g_usleep(20000);
  }
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
