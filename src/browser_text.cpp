#include "browser_text.h"
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace hv {
namespace {
constexpr int MaxCharacters = 65536;
struct Unref {
  template <class T> void operator()(T *value) const {
    if (value)
      g_object_unref(value);
  }
};
template <class T> using Object = std::unique_ptr<T, Unref>;
std::string Hash(const std::string &text) {
  char *value = g_compute_checksum_for_data(
      G_CHECKSUM_SHA256, reinterpret_cast<const guchar *>(text.data()),
      text.size());
  std::string result = value ? value : "";
  g_free(value);
  return result;
}
struct Node;
struct Link {
  int start, end;
  std::shared_ptr<Node> child;
};
struct Node {
  Object<AtspiAccessible> accessible;
  Object<AtspiText> text;
  std::string identity, raw;
  Json metadata;
  AtspiRole role;
  bool root = false;
  int base = 0;
  std::vector<int> offsets;
  std::vector<Link> links;
};
struct Fragment {
  std::string text;
  int characters = 0, caret = -1;
  Json selections = Json::array();
};
struct Reader {
  bool &protected_field;
  std::chrono::steady_clock::time_point until;
  std::string bus;
  std::map<std::string, std::shared_ptr<Node>> nodes;
  std::set<std::string> visiting;
  std::set<std::string> expanded;
  Object<AtspiDocument> document;
  GError *error = nullptr;
  Reader(bool &field, std::chrono::steady_clock::time_point deadline,
         std::string owner)
      : protected_field(field), until(deadline), bus(std::move(owner)) {}
  ~Reader() { g_clear_error(&error); }
  bool room(int depth = 0) {
    return !error && depth <= 16 && nodes.size() <= 128 &&
           std::chrono::steady_clock::now() < until;
  }
  Json metadata(AtspiAccessible *node, AtspiText *text, bool root) {
    atspi_accessible_clear_cache_single(node);
    auto role = atspi_accessible_get_role(node, &error);
    if (!error && role == ATSPI_ROLE_PASSWORD_TEXT)
      protected_field = true;
    if (error || protected_field || !text || !room())
      return Json();
    int count = atspi_text_get_character_count(text, &error);
    int caret = !error ? atspi_text_get_caret_offset(text, &error) : -1;
    int selected = !error ? atspi_text_get_n_selections(text, &error) : -1;
    if (error || count < 0 || count > MaxCharacters ||
        caret < (root ? 0 : -1) || caret > count || selected < 0 ||
        selected > 1)
      return Json();
    Json ranges = Json::array();
    for (int i = 0; i < selected; ++i) {
      auto range = atspi_text_get_selection(text, i, &error);
      bool valid = !error && range && range->start_offset >= 0 &&
                   range->start_offset <= range->end_offset &&
                   range->end_offset <= count;
      if (valid && range->start_offset != range->end_offset)
        ranges.push_back({range->start_offset, range->end_offset});
      g_free(range);
      if (!valid)
        return Json();
    }
    return {{"role", role},
            {"characters", count},
            {"caret", caret},
            {"selections", ranges}};
  }
  bool descendant(AtspiAccessible *child, AtspiAccessible *parent) {
    const auto wanted = ATSPI_OBJECT(parent);
    Object<AtspiAccessible> current(
        static_cast<AtspiAccessible *>(g_object_ref(child)));
    for (int depth = 0; current && room(depth); ++depth) {
      atspi_accessible_clear_cache_single(current.get());
      auto object = ATSPI_OBJECT(current.get());
      if (!object->app || !object->app->bus_name || !object->path ||
          bus != object->app->bus_name)
        return false;
      if (std::string_view(object->path) == wanted->path)
        return depth > 0;
      auto next = atspi_accessible_get_parent(current.get(), &error);
      current.reset(next);
    }
    return false;
  }
  std::shared_ptr<Node> collect(AtspiAccessible *accessible, int depth = 0) {
    if (!accessible || !room(depth))
      return {};
    auto object = ATSPI_OBJECT(accessible);
    if (!object->app || !object->app->bus_name || !object->path ||
        bus != object->app->bus_name)
      return {};
    std::string identity = bus + object->path;
    if (visiting.contains(identity))
      return {};
    if (auto entry = nodes.find(identity); entry != nodes.end())
      return entry->second;
    auto role = atspi_accessible_get_role(accessible, &error);
    if (!error && role == ATSPI_ROLE_PASSWORD_TEXT)
      protected_field = true;
    if (error || protected_field)
      return {};
    auto node = std::make_shared<Node>();
    node->accessible.reset(
        static_cast<AtspiAccessible *>(g_object_ref(accessible)));
    node->text.reset(atspi_accessible_get_text_iface(accessible));
    node->identity = identity;
    node->role = role;
    node->root = depth == 0;
    node->metadata = metadata(accessible, node->text.get(), depth == 0);
    if (!node->metadata.is_object())
      return {};
    node->role = static_cast<AtspiRole>(node->metadata.at("role").get<int>());
    nodes.emplace(identity, node);
    visiting.insert(identity);
    Object<AtspiHypertext> hyper(
        atspi_accessible_get_hypertext_iface(accessible));
    if (hyper) {
      int count = atspi_hypertext_get_n_links(hyper.get(), &error);
      if (error || count < 0 || count > 128)
        return {};
      for (int i = 0; i < count; ++i) {
        Object<AtspiHyperlink> link(
            atspi_hypertext_get_link(hyper.get(), i, &error));
        if (!link || error)
          return {};
        int start = atspi_hyperlink_get_start_index(link.get(), &error);
        int end =
            !error ? atspi_hyperlink_get_end_index(link.get(), &error) : -1;
        Object<AtspiAccessible> child(
            !error ? atspi_hyperlink_get_object(link.get(), 0, &error)
                   : nullptr);
        if (error || !child || start < 0 || start > end ||
            end > node->metadata.at("characters").get<int>() ||
            !descendant(child.get(), accessible))
          return {};
        auto part = collect(child.get(), depth + 1);
        if (!part)
          return {};
        node->links.push_back({start, end, part});
      }
    }
    visiting.erase(identity);
    return node;
  }
  bool read() {
    // Inspect every linked node's role before fetching any editor text. In
    // particular, an embedded password cannot be hidden by a normal parent.
    for (auto &[identity, node] : nodes) {
      if (!room() || node->metadata != metadata(node->accessible.get(),
                                                node->text.get(), node->root))
        return false;
      for (const auto &link : node->links)
        if (!descendant(link.child->accessible.get(), node->accessible.get()))
          return false;
      char *raw = atspi_text_get_text(
          node->text.get(), 0, node->metadata.at("characters").get<int>(),
          &error);
      bool valid =
          !error && raw && g_utf8_validate(raw, -1, nullptr) &&
          g_utf8_strlen(raw, -1) == node->metadata.at("characters").get<int>();
      if (valid)
        node->raw = raw;
      g_free(raw);
      if (!valid || node->metadata != metadata(node->accessible.get(),
                                               node->text.get(), node->root))
        return false;
    }
    return true;
  }
  bool findDocument(AtspiAccessible *root) {
    Object<AtspiAccessible> current(
        static_cast<AtspiAccessible *>(g_object_ref(root)));
    for (int depth = 0; current && depth <= 32 && room(); ++depth) {
      auto object = ATSPI_OBJECT(current.get());
      if (!object->app || !object->app->bus_name ||
          bus != object->app->bus_name)
        return false;
      if (atspi_accessible_get_role(current.get(), &error) ==
          ATSPI_ROLE_DOCUMENT_WEB) {
        document.reset(atspi_accessible_get_document_iface(current.get()));
        return !error && document;
      }
      current.reset(atspi_accessible_get_parent(current.get(), &error));
    }
    return false;
  }
  Json documentSelection() {
    if (!document || !room())
      return Json();
    // Chromium's Text.GetSelection can double-convert UTF-16 offsets for
    // emoji. Document.GetTextSelections exposes the real endpoints. Only
    // endpoint metadata is read; both endpoints must be in this editor's
    // already role-checked graph, never elsewhere in the document.
    auto ranges = atspi_document_get_text_selections(document.get(), &error);
    Json result = Json::array();
    bool valid = !error && ranges && ranges->len <= 1;
    if (valid)
      for (guint i = 0; i < ranges->len; ++i) {
        const auto &range = g_array_index(ranges, AtspiTextSelection, i);
        Json entry = Json::array();
        for (auto [endpoint, offset] :
             {std::pair{range.start_object, range.start_offset},
              std::pair{range.end_object, range.end_offset}}) {
          if (!endpoint) {
            valid = false;
            break;
          }
          auto object = ATSPI_OBJECT(endpoint);
          if (!object->app || !object->app->bus_name || !object->path ||
              bus != object->app->bus_name) {
            valid = false;
            break;
          }
          auto found = nodes.find(bus + object->path);
          if (found == nodes.end() || offset < 0 ||
              offset > found->second->metadata.at("characters").get<int>()) {
            valid = false;
            break;
          }
          entry.push_back({found->first, offset});
        }
        entry.push_back(static_cast<bool>(range.start_is_active));
        result.push_back(entry);
      }
    if (ranges)
      g_array_free(ranges, true);
    return valid ? result : Json();
  }
  bool expand(const std::shared_ptr<Node> &node, Fragment &result,
              int depth = 0, int base = 0) {
    if (!room(depth) || !expanded.insert(node->identity).second)
      return false;
    node->base = base;
    int count = node->metadata.at("characters");
    std::vector<int> offsets(count + 1, 0);
    const char *cursor = node->raw.c_str();
    bool previous_paragraph = false;
    std::vector<int> child_carets;
    for (int i = 0; i < count; ++i) {
      const char *next = g_utf8_next_char(cursor);
      if (g_utf8_get_char(cursor) != 0xfffc) {
        offsets[i] = result.characters;
        result.text.append(cursor, next - cursor);
        ++result.characters;
        previous_paragraph = false;
      } else {
        auto found = std::find_if(node->links.begin(), node->links.end(),
                                  [i](const Link &link) {
                                    return link.start == i && link.end == i + 1;
                                  });
        if (found == node->links.end())
          return false;
        bool paragraph = found->child->role == ATSPI_ROLE_PARAGRAPH;
        if (previous_paragraph && paragraph) {
          result.text += "\n\n";
          result.characters += 2;
        }
        Fragment part;
        if (!expand(found->child, part, depth + 1, base + result.characters))
          return false;
        offsets[i] = result.characters;
        if (part.caret >= 0)
          child_carets.push_back(result.characters + part.caret);
        result.text += part.text;
        result.characters += part.characters;
        previous_paragraph = paragraph;
      }
      if (result.characters > MaxCharacters)
        return false;
      offsets[i + 1] = result.characters;
      cursor = next;
    }
    int caret = node->metadata.at("caret");
    if (child_carets.size() > 1)
      return false;
    result.caret = !child_carets.empty() ? child_carets.front()
                   : caret >= 0          ? offsets[caret]
                                         : -1;
    node->offsets = std::move(offsets);
    return true;
  }
};
} // namespace

bool ReadBrowserText(AtspiAccessible *node, Json &snapshot,
                     bool &protected_field, std::string *content,
                     std::chrono::steady_clock::time_point until) {
  auto object = ATSPI_OBJECT(node);
  if (!object->app || !object->app->bus_name)
    return false;
  Reader reader{protected_field, until, object->app->bus_name};
  auto root = reader.collect(node);
  if (!root || !reader.findDocument(node))
    return false;
  auto selection = reader.documentSelection();
  if (!selection.is_array() || !reader.read())
    return false;
  Fragment fragment;
  if (!reader.expand(root, fragment) || fragment.caret < 0 ||
      fragment.caret > fragment.characters)
    return false;
  if (selection != reader.documentSelection())
    return false;
  if (!selection.empty()) {
    auto endpoint = [&](const Json &value) {
      const auto &part = reader.nodes.at(value[0].get<std::string>());
      return part->offsets.empty()
                 ? -1
                 : part->base + part->offsets.at(value[1].get<int>());
    };
    int start = endpoint(selection[0][0]), end = endpoint(selection[0][1]);
    if (start < 0 || start >= end || end > fragment.characters)
      return false;
    fragment.selections = Json::array({{start, end}});
  } else {
    for (const auto &[identity, part] : reader.nodes)
      if (!part->metadata.at("selections").empty())
        return false;
  }
  Json geometry = Json::array(), selected = Json::array();
  for (const auto &[identity, part] : reader.nodes) {
    auto entry = part->metadata;
    entry["control"] = identity;
    entry["digest"] = Hash(part->raw);
    entry["links"] = Json::array();
    for (const auto &link : part->links)
      entry["links"].push_back({link.start, link.end, link.child->identity});
    geometry.push_back(entry);
  }
  geometry.push_back({{"document_selection", selection}});
  for (const auto &range : fragment.selections) {
    const char *start =
        g_utf8_offset_to_pointer(fragment.text.c_str(), range[0].get<int>());
    const char *end =
        g_utf8_offset_to_pointer(fragment.text.c_str(), range[1].get<int>());
    selected.push_back(Hash(std::string(start, end)));
  }
  snapshot = {{"characters", fragment.characters},
              {"caret", fragment.caret},
              {"selections", fragment.selections},
              {"selection_digests", selected},
              {"digest", Hash(fragment.text)},
              {"editor_digest", Hash(geometry.dump())}};
  if (content)
    *content = std::move(fragment.text);
  return reader.room() && !protected_field;
}
} // namespace hv
