#include <gtkmm.h>
#include <gtk-layer-shell.h>
#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

struct Color {
  double r = 1.0;
  double g = 1.0;
  double b = 1.0;
  double a = 1.0;
};

struct Config {
  std::string app_toggle = "SUPER+N";
  std::string storage_path = "~/.local/share/hyprink";
  std::string background_mode = "transparent";
  Color black_color{0.0, 0.0, 0.0, 1.0};
  std::string layer = "bottom";
  unsigned int draw_button = 1;
  unsigned int delete_button = 2;
  unsigned int resize_button = 3;
  double delete_distance = 14.0;
  int stylus_size = 4;
  Color stylus_color{1.0, 1.0, 1.0, 1.0};
  std::string font = "monospace";
  int font_size = 16;
  Color text_color{1.0, 1.0, 1.0, 1.0};
  Color note_background{0.0, 0.0, 0.0, 0.6};
  Color note_border{1.0, 1.0, 1.0, 0.8};
  int border_width = 1;
  int padding = 8;
  int note_width = 260;
  int note_min_height = 64;
};

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct Stroke {
  Color color;
  double size = 4.0;
  std::vector<Point> points;
};

struct Note {
  double x = 0.0;
  double y = 0.0;
  double w = 260.0;
  double h = 64.0;
  std::string text;
};

static std::string expand_user(std::string path) {
  if (path.empty() || path[0] != '~') {
    return path;
  }

  const char* home = std::getenv("HOME");
  if (!home) {
    return path;
  }

  if (path.size() == 1) {
    return home;
  }

  if (path[1] == '/') {
    return std::string(home) + path.substr(1);
  }

  return path;
}

static std::string runtime_dir() {
  if (const char* runtime = std::getenv("XDG_RUNTIME_DIR")) {
    return runtime;
  }
  return "/tmp";
}

static std::string socket_path() {
  return runtime_dir() + "/hyprink.sock";
}

static std::string trim(const std::string& input) {
  const auto begin = input.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = input.find_last_not_of(" \t\r\n");
  return input.substr(begin, end - begin + 1);
}

static std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

static Color parse_color(const std::string& raw, Color fallback) {
  std::string value = trim(raw);
  if (value.empty()) {
    return fallback;
  }

  GdkRGBA rgba;
  if (gdk_rgba_parse(&rgba, value.c_str())) {
    return {rgba.red, rgba.green, rgba.blue, rgba.alpha};
  }

  if (value[0] == '#') {
    value.erase(value.begin());
  }

  if (value.size() != 6 && value.size() != 8) {
    return fallback;
  }

  try {
    const auto parsed = std::stoul(value, nullptr, 16);
    if (value.size() == 6) {
      return {
        static_cast<double>((parsed >> 16) & 0xff) / 255.0,
        static_cast<double>((parsed >> 8) & 0xff) / 255.0,
        static_cast<double>(parsed & 0xff) / 255.0,
        1.0
      };
    }

    return {
      static_cast<double>((parsed >> 24) & 0xff) / 255.0,
      static_cast<double>((parsed >> 16) & 0xff) / 255.0,
      static_cast<double>((parsed >> 8) & 0xff) / 255.0,
      static_cast<double>(parsed & 0xff) / 255.0
    };
  } catch (...) {
    return fallback;
  }
}

static int parse_int(const std::string& raw, int fallback) {
  try {
    return std::stoi(trim(raw));
  } catch (...) {
    return fallback;
  }
}

static double parse_double(const std::string& raw, double fallback) {
  try {
    return std::stod(trim(raw));
  } catch (...) {
    return fallback;
  }
}

static unsigned int parse_button(const std::string& raw, unsigned int fallback) {
  const std::string value = lower(trim(raw));
  if (value == "left" || value == "lmb" || value == "mouse1") {
    return 1;
  }
  if (value == "middle" || value == "mmb" || value == "mouse2") {
    return 2;
  }
  if (value == "right" || value == "rmb" || value == "mouse3") {
    return 3;
  }
  if (value.rfind("mouse", 0) == 0) {
    return static_cast<unsigned int>(std::max(1, parse_int(value.substr(5), static_cast<int>(fallback))));
  }
  return static_cast<unsigned int>(std::max(1, parse_int(value, static_cast<int>(fallback))));
}

static std::string config_value(
  const std::map<std::string, std::map<std::string, std::string>>& data,
  const std::string& section,
  const std::string& key,
  const std::string& fallback
) {
  const auto section_it = data.find(section);
  if (section_it == data.end()) {
    return fallback;
  }
  const auto key_it = section_it->second.find(key);
  if (key_it == section_it->second.end()) {
    return fallback;
  }
  return key_it->second;
}

static Config load_config(const std::string& explicit_path) {
  Config config;
  std::vector<std::string> candidates;

  if (!explicit_path.empty()) {
    candidates.push_back(explicit_path);
  }
  candidates.push_back("Project.conf");
  candidates.push_back("~/.config/hyprink/Project.conf");
  candidates.push_back("/usr/local/share/hyprink/Project.conf");
  candidates.push_back("/usr/share/hyprink/Project.conf");

  std::ifstream file;
  std::string selected;
  for (const auto& candidate : candidates) {
    selected = expand_user(candidate);
    file.open(selected);
    if (file.good()) {
      break;
    }
    file.close();
  }

  if (!file.good()) {
    return config;
  }

  std::map<std::string, std::map<std::string, std::string>> data;
  std::string section;
  std::string line;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = lower(trim(line.substr(1, line.size() - 2)));
      continue;
    }
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    data[section][lower(trim(line.substr(0, equals)))] = trim(line.substr(equals + 1));
  }

  config.app_toggle = config_value(data, "app", "apptoggle",
    config_value(data, "general", "toggle_key", config.app_toggle));
  config.storage_path = config_value(data, "app", "storagepath",
    config_value(data, "general", "storage_path", config.storage_path));
  config.background_mode = lower(config_value(data, "appearance", "backgroundmode",
    config_value(data, "background", "mode", config.background_mode)));
  config.black_color = parse_color(config_value(data, "appearance", "blackbackgroundcolor",
    config_value(data, "background", "black_color", "")), config.black_color);
  config.layer = lower(config_value(data, "app", "layer",
    config_value(data, "window", "layer", config.layer)));
  config.draw_button = parse_button(config_value(data, "controls", "drawbutton", ""), config.draw_button);
  config.delete_button = parse_button(config_value(data, "controls", "deletebutton", ""), config.delete_button);
  config.resize_button = parse_button(config_value(data, "controls", "resizebutton", ""), config.resize_button);
  config.delete_distance = parse_double(config_value(data, "controls", "deletedistance", ""), config.delete_distance);
  config.stylus_size = parse_int(config_value(data, "drawing", "stylussize",
    config_value(data, "stylus", "size", "")), config.stylus_size);
  config.stylus_color = parse_color(config_value(data, "drawing", "styluscolor",
    config_value(data, "stylus", "color", "")), config.stylus_color);
  config.font = config_value(data, "notes", "font", config.font);
  config.font_size = parse_int(config_value(data, "notes", "fontsize",
    config_value(data, "notes", "font_size", "")), config.font_size);
  config.text_color = parse_color(config_value(data, "notes", "textcolor",
    config_value(data, "notes", "text_color", "")), config.text_color);
  config.note_background = parse_color(config_value(data, "notes", "backgroundcolor",
    config_value(data, "notes", "background_color", "")), config.note_background);
  config.note_border = parse_color(config_value(data, "notes", "bordercolor",
    config_value(data, "notes", "border_color", "")), config.note_border);
  config.border_width = parse_int(config_value(data, "notes", "borderwidth",
    config_value(data, "notes", "border_width", "")), config.border_width);
  config.padding = parse_int(config_value(data, "notes", "padding", ""), config.padding);
  config.note_width = parse_int(config_value(data, "notes", "width", ""), config.note_width);
  config.note_min_height = parse_int(config_value(data, "notes", "minheight",
    config_value(data, "notes", "min_height", "")), config.note_min_height);

  config.stylus_size = std::max(1, config.stylus_size);
  config.font_size = std::max(8, config.font_size);
  config.border_width = std::max(0, config.border_width);
  config.padding = std::max(0, config.padding);
  config.note_width = std::max(80, config.note_width);
  config.note_min_height = std::max(32, config.note_min_height);
  config.delete_distance = std::max(1.0, config.delete_distance);
  return config;
}

static void set_source(const Cairo::RefPtr<Cairo::Context>& cr, const Color& color) {
  cr->set_source_rgba(color.r, color.g, color.b, color.a);
}

static Json::Value color_to_json(const Color& color) {
  Json::Value value;
  value["r"] = color.r;
  value["g"] = color.g;
  value["b"] = color.b;
  value["a"] = color.a;
  return value;
}

static Color color_from_json(const Json::Value& value, Color fallback) {
  if (!value.isObject()) {
    return fallback;
  }
  return {
    value.get("r", fallback.r).asDouble(),
    value.get("g", fallback.g).asDouble(),
    value.get("b", fallback.b).asDouble(),
    value.get("a", fallback.a).asDouble()
  };
}

static std::string active_workspace_key() {
  FILE* pipe = popen("hyprctl activeworkspace -j 2>/dev/null", "r");
  if (!pipe) {
    return "default";
  }

  std::string output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe)) {
    output += buffer;
  }
  pclose(pipe);

  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream stream(output);
  if (Json::parseFromStream(builder, stream, &root, &errors)) {
    if (root.isMember("id")) {
      return "workspace_" + root["id"].asString();
    }
    if (root.isMember("name")) {
      return "workspace_" + root["name"].asString();
    }
  }

  return "default";
}

class HyprInkWindow : public Gtk::Window {
public:
  explicit HyprInkWindow(Config config)
    : config_(std::move(config)) {
    set_title("HyprInk");
    set_name("hyprink");
    set_decorated(false);
    set_app_paintable(true);
    set_skip_taskbar_hint(true);
    set_skip_pager_hint(true);
    set_type_hint(Gdk::WINDOW_TYPE_HINT_UTILITY);
    set_default_size(800, 600);

    add_events(Gdk::BUTTON_PRESS_MASK |
               Gdk::BUTTON_RELEASE_MASK |
               Gdk::POINTER_MOTION_MASK |
               Gdk::KEY_PRESS_MASK |
               Gdk::FOCUS_CHANGE_MASK);
    set_can_focus(true);

    if (auto screen = get_screen()) {
      if (auto visual = screen->get_rgba_visual()) {
        gtk_widget_set_visual(GTK_WIDGET(gobj()), visual->gobj());
      }
    }

    gtk_layer_init_for_window(gobj());
    gtk_layer_set_namespace(gobj(), "hyprink");
    gtk_layer_set_layer(gobj(), layer_from_config());
    gtk_layer_set_keyboard_mode(gobj(), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_exclusive_zone(gobj(), -1);
    gtk_layer_set_anchor(gobj(), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(gobj(), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_anchor(gobj(), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(gobj(), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);

    storage_dir_ = expand_user(config_.storage_path);
    current_workspace_key_ = active_workspace_key();
    activation_workspace_key_ = current_workspace_key_;
    load_state();
    Glib::signal_timeout().connect([this] {
      if (is_visible()) {
        current_workspace_key_ = active_workspace_key();
        update_input_mode();
        cursor_visible_ = !cursor_visible_;
        if (editing_note_ >= 0) {
          queue_draw();
        }
      }
      return true;
    }, 500);
    show_all();
  }

  void toggle() {
    if (is_visible()) {
      finish_edit();
      hide();
      return;
    }

    current_workspace_key_ = active_workspace_key();
    activation_workspace_key_ = current_workspace_key_;
    update_input_mode();
    show_all();
  }

protected:
  bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override {
    if (config_.background_mode == "black") {
      set_source(cr, config_.black_color);
      cr->paint();
    } else {
      cr->set_operator(Cairo::OPERATOR_CLEAR);
      cr->paint();
      cr->set_operator(Cairo::OPERATOR_OVER);
    }

    cr->set_line_cap(Cairo::LINE_CAP_ROUND);
    cr->set_line_join(Cairo::LINE_JOIN_ROUND);

    for (const auto& stroke : strokes_) {
      draw_stroke(cr, stroke);
    }

    if (drawing_) {
      draw_stroke(cr, current_stroke_);
    }

    for (size_t i = 0; i < notes_.size(); ++i) {
      draw_note(cr, notes_[i], static_cast<int>(i) == editing_note_);
    }

    return true;
  }

  bool on_button_press_event(GdkEventButton* event) override {
    if (!can_edit_current_workspace()) {
      cancel_pointer_actions();
      return true;
    }

    if (event->button == config_.delete_button) {
      delete_at(event->x, event->y);
      return true;
    }

    if (event->button == config_.resize_button) {
      const int hit = note_at(event->x, event->y);
      if (hit >= 0) {
        editing_note_ = -1;
        resizing_note_ = hit;
        resize_start_x_ = event->x;
        resize_start_y_ = event->y;
        resize_start_w_ = notes_[hit].w;
        resize_start_h_ = notes_[hit].h;
        return true;
      }
      return false;
    }

    if (event->button != config_.draw_button) {
      return false;
    }

    const int hit = note_at(event->x, event->y);

    if (event->type == GDK_2BUTTON_PRESS) {
      if (hit >= 0) {
        begin_edit(hit);
        return true;
      }
      return false;
    }

    press_x_ = event->x;
    press_y_ = event->y;
    last_x_ = event->x;
    last_y_ = event->y;
    moved_ = false;

    if (hit >= 0) {
      editing_note_ = -1;
      dragging_note_ = hit;
      drag_dx_ = event->x - notes_[hit].x;
      drag_dy_ = event->y - notes_[hit].y;
      queue_draw();
      return true;
    }

    drawing_ = true;
    current_stroke_ = Stroke{config_.stylus_color, static_cast<double>(config_.stylus_size), {{event->x, event->y}}};
    return true;
  }

  bool on_motion_notify_event(GdkEventMotion* event) override {
    if (!can_edit_current_workspace()) {
      cancel_pointer_actions();
      return true;
    }

    if (dragging_note_ >= 0) {
      notes_[dragging_note_].x = event->x - drag_dx_;
      notes_[dragging_note_].y = event->y - drag_dy_;
      clamp_note(notes_[dragging_note_]);
      queue_draw();
      return true;
    }

    if (resizing_note_ >= 0) {
      auto& note = notes_[resizing_note_];
      note.w = std::max<double>(config_.note_width / 2.0, resize_start_w_ + event->x - resize_start_x_);
      note.h = std::max<double>(config_.note_min_height, resize_start_h_ + event->y - resize_start_y_);
      clamp_note(note);
      queue_draw();
      return true;
    }

    if (!drawing_) {
      return false;
    }

    const double distance = std::hypot(event->x - press_x_, event->y - press_y_);
    if (distance > 3.0) {
      moved_ = true;
    }

    if (moved_ && std::hypot(event->x - last_x_, event->y - last_y_) >= 1.0) {
      current_stroke_.points.push_back({event->x, event->y});
      last_x_ = event->x;
      last_y_ = event->y;
      queue_draw();
    }

    return true;
  }

  bool on_button_release_event(GdkEventButton* event) override {
    if (!can_edit_current_workspace()) {
      cancel_pointer_actions();
      return true;
    }

    if (event->button == config_.resize_button) {
      if (resizing_note_ >= 0) {
        resizing_note_ = -1;
        save();
        return true;
      }
      return false;
    }

    if (event->button != config_.draw_button) {
      return false;
    }

    if (dragging_note_ >= 0) {
      dragging_note_ = -1;
      save();
      return true;
    }

    if (!drawing_) {
      return false;
    }

    drawing_ = false;
    if (moved_ && current_stroke_.points.size() > 1) {
      strokes_.push_back(current_stroke_);
      save();
    } else {
      Note note;
      note.x = press_x_;
      note.y = press_y_;
      note.w = config_.note_width;
      note.h = config_.note_min_height;
      clamp_note(note);
      notes_.push_back(note);
      begin_edit(static_cast<int>(notes_.size() - 1));
    }

    queue_draw();
    return true;
  }

  bool on_key_press_event(GdkEventKey* event) override {
    if (!can_edit_current_workspace()) {
      return true;
    }

    if (editing_note_ < 0 || editing_note_ >= static_cast<int>(notes_.size())) {
      if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_q) {
        finish_edit();
        hide();
        return true;
      }
      return false;
    }

    auto& note = notes_[editing_note_];

    if (event->keyval == GDK_KEY_Escape) {
      finish_edit();
      return true;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_Return) {
      finish_edit();
      return true;
    }

    if (event->keyval == GDK_KEY_BackSpace) {
      if (!note.text.empty()) {
        note.text.pop_back();
        update_note_size(note);
        save();
        queue_draw();
      }
      return true;
    }

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
      note.text.push_back('\n');
      update_note_size(note);
      save();
      queue_draw();
      return true;
    }

    const gunichar unicode = gdk_keyval_to_unicode(event->keyval);
    if (unicode != 0 && !g_unichar_iscntrl(unicode)) {
      gchar utf8[7] = {0};
      const int len = g_unichar_to_utf8(unicode, utf8);
      note.text.append(utf8, len);
      update_note_size(note);
      save();
      queue_draw();
      return true;
    }

    return true;
  }

  bool on_delete_event(GdkEventAny* event) override {
    (void)event;
    finish_edit();
    save();
    hide();
    return true;
  }

private:
  GtkLayerShellLayer layer_from_config() const {
    if (config_.layer == "background") {
      return GTK_LAYER_SHELL_LAYER_BACKGROUND;
    }
    if (config_.layer == "bottom") {
      return GTK_LAYER_SHELL_LAYER_BOTTOM;
    }
    if (config_.layer == "top") {
      return GTK_LAYER_SHELL_LAYER_TOP;
    }
    return GTK_LAYER_SHELL_LAYER_OVERLAY;
  }

  void draw_note(const Cairo::RefPtr<Cairo::Context>& cr, Note& note, bool editing) {
    update_note_size(note);

    constexpr double radius = 4.0;
    const double x = note.x;
    const double y = note.y;
    const double w = note.w;
    const double h = note.h;

    cr->begin_new_sub_path();
    cr->arc(x + w - radius, y + radius, radius, -M_PI / 2.0, 0);
    cr->arc(x + w - radius, y + h - radius, radius, 0, M_PI / 2.0);
    cr->arc(x + radius, y + h - radius, radius, M_PI / 2.0, M_PI);
    cr->arc(x + radius, y + radius, radius, M_PI, 3.0 * M_PI / 2.0);
    cr->close_path();
    set_source(cr, config_.note_background);
    cr->fill_preserve();

    Color border = config_.note_border;
    if (editing) {
      border.a = 1.0;
    }
    set_source(cr, border);
    cr->set_line_width(editing ? std::max(2, config_.border_width + 1) : config_.border_width);
    cr->stroke();

    std::string display_text = note.text;
    if (editing && cursor_visible_) {
      display_text += "|";
    }
    if (display_text.empty()) {
      display_text = " ";
    }

    auto layout = create_pango_layout(display_text);
    Pango::FontDescription font;
    font.set_family(config_.font);
    font.set_absolute_size(config_.font_size * PANGO_SCALE);
    layout->set_font_description(font);
    layout->set_width(static_cast<int>((note.w - config_.padding * 2) * PANGO_SCALE));
    layout->set_wrap(Pango::WRAP_WORD_CHAR);
    cr->move_to(note.x + config_.padding, note.y + config_.padding);
    set_source(cr, config_.text_color);
    layout->show_in_cairo_context(cr);
  }

  void draw_stroke(const Cairo::RefPtr<Cairo::Context>& cr, const Stroke& stroke) {
    if (stroke.points.size() < 2) {
      return;
    }

    set_source(cr, stroke.color);
    cr->set_line_width(stroke.size);
    cr->move_to(stroke.points.front().x, stroke.points.front().y);
    for (size_t i = 1; i < stroke.points.size(); ++i) {
      cr->line_to(stroke.points[i].x, stroke.points[i].y);
    }
    cr->stroke();
  }

  void update_note_size(Note& note) {
    auto layout = create_pango_layout(note.text.empty() ? " " : note.text);
    Pango::FontDescription font;
    font.set_family(config_.font);
    font.set_absolute_size(config_.font_size * PANGO_SCALE);
    layout->set_font_description(font);
    layout->set_width(static_cast<int>((note.w - config_.padding * 2) * PANGO_SCALE));
    layout->set_wrap(Pango::WRAP_WORD_CHAR);
    int text_w = 0;
    int text_h = 0;
    layout->get_pixel_size(text_w, text_h);
    note.h = std::max<double>(note.h, std::max<double>(config_.note_min_height, text_h + config_.padding * 2));
  }

  int note_at(double x, double y) {
    for (int i = static_cast<int>(notes_.size()) - 1; i >= 0; --i) {
      update_note_size(notes_[i]);
      const auto& note = notes_[i];
      if (x >= note.x && x <= note.x + note.w && y >= note.y && y <= note.y + note.h) {
        return i;
      }
    }
    return -1;
  }

  static double point_to_segment_distance(Point p, Point a, Point b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared == 0.0) {
      return std::hypot(p.x - a.x, p.y - a.y);
    }

    const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length_squared, 0.0, 1.0);
    const Point projection{a.x + t * dx, a.y + t * dy};
    return std::hypot(p.x - projection.x, p.y - projection.y);
  }

  int stroke_at(double x, double y) const {
    const Point point{x, y};
    for (int i = static_cast<int>(strokes_.size()) - 1; i >= 0; --i) {
      const auto& stroke = strokes_[i];
      if (stroke.points.size() < 2) {
        continue;
      }

      const double threshold = config_.delete_distance + stroke.size / 2.0;
      for (size_t j = 1; j < stroke.points.size(); ++j) {
        if (point_to_segment_distance(point, stroke.points[j - 1], stroke.points[j]) <= threshold) {
          return i;
        }
      }
    }

    return -1;
  }

  void delete_at(double x, double y) {
    if (const int note = note_at(x, y); note >= 0) {
      notes_.erase(notes_.begin() + note);
      editing_note_ = -1;
      save();
      queue_draw();
      return;
    }

    if (const int stroke = stroke_at(x, y); stroke >= 0) {
      strokes_.erase(strokes_.begin() + stroke);
      save();
      queue_draw();
    }
  }

  void begin_edit(int index) {
    editing_note_ = index;
    cursor_visible_ = true;
    gtk_layer_set_keyboard_mode(gobj(), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    grab_focus();
    update_note_size(notes_[index]);
    queue_draw();
  }

  void finish_edit() {
    remove_empty_notes();
    editing_note_ = -1;
    gtk_layer_set_keyboard_mode(gobj(), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    save();
    queue_draw();
  }

  void clamp_note(Note& note) {
    const auto width = std::max(1, get_allocated_width());
    const auto height = std::max(1, get_allocated_height());
    note.x = std::clamp(note.x, 0.0, std::max(0.0, static_cast<double>(width) - note.w));
    note.y = std::clamp(note.y, 0.0, std::max(0.0, static_cast<double>(height) - note.h));
  }

  bool can_edit_current_workspace() {
    current_workspace_key_ = active_workspace_key();
    return current_workspace_key_ == activation_workspace_key_;
  }

  void update_input_mode() {
    const bool active = current_workspace_key_ == activation_workspace_key_;
    if (active) {
      gtk_widget_input_shape_combine_region(GTK_WIDGET(gobj()), nullptr);
    } else {
      cairo_region_t* empty = cairo_region_create();
      gtk_widget_input_shape_combine_region(GTK_WIDGET(gobj()), empty);
      cairo_region_destroy(empty);
      if (editing_note_ >= 0) {
        finish_edit();
      }
      cancel_pointer_actions();
    }
  }

  void cancel_pointer_actions() {
    drawing_ = false;
    dragging_note_ = -1;
    resizing_note_ = -1;
  }

  fs::path state_file() const {
    return fs::path(storage_dir_) / "state.json";
  }

  void load_state() {
    if (loaded_) {
      return;
    }

    strokes_.clear();
    notes_.clear();
    loaded_ = true;

    std::ifstream file(state_file());
    if (!file.good()) {
      queue_draw();
      return;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
      std::cerr << "hyprink: failed to load state: " << errors << "\n";
      queue_draw();
      return;
    }

    for (const auto& value : root["strokes"]) {
      Stroke stroke;
      stroke.color = color_from_json(value["color"], config_.stylus_color);
      stroke.size = value.get("size", config_.stylus_size).asDouble();
      for (const auto& point : value["points"]) {
        stroke.points.push_back({point.get("x", 0).asDouble(), point.get("y", 0).asDouble()});
      }
      if (!stroke.points.empty()) {
        strokes_.push_back(std::move(stroke));
      }
    }

    for (const auto& value : root["notes"]) {
      Note note;
      note.x = value.get("x", 0).asDouble();
      note.y = value.get("y", 0).asDouble();
      note.w = value.get("w", config_.note_width).asDouble();
      note.h = value.get("h", config_.note_min_height).asDouble();
      note.text = value.get("text", "").asString();
      notes_.push_back(std::move(note));
    }

    queue_draw();
  }

  void save() {
    if (!loaded_) {
      return;
    }

    remove_empty_notes();

    std::error_code ec;
    fs::create_directories(storage_dir_, ec);
    if (ec) {
      std::cerr << "hyprink: failed to create storage dir: " << ec.message() << "\n";
      return;
    }

    Json::Value root;
    root["version"] = 1;

    for (const auto& stroke : strokes_) {
      Json::Value value;
      value["color"] = color_to_json(stroke.color);
      value["size"] = stroke.size;
      for (const auto& point : stroke.points) {
        Json::Value point_value;
        point_value["x"] = point.x;
        point_value["y"] = point.y;
        value["points"].append(point_value);
      }
      root["strokes"].append(value);
    }

    for (const auto& note : notes_) {
      Json::Value value;
      value["x"] = note.x;
      value["y"] = note.y;
      value["w"] = note.w;
      value["h"] = note.h;
      value["text"] = note.text;
      root["notes"].append(value);
    }

    std::ofstream file(state_file());
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    file << Json::writeString(builder, root);
  }

  void remove_empty_notes() {
    for (int i = static_cast<int>(notes_.size()) - 1; i >= 0; --i) {
      if (!trim(notes_[i].text).empty()) {
        continue;
      }

      notes_.erase(notes_.begin() + i);
      if (editing_note_ == i) {
        editing_note_ = -1;
      } else if (editing_note_ > i) {
        --editing_note_;
      }
      if (dragging_note_ == i) {
        dragging_note_ = -1;
      } else if (dragging_note_ > i) {
        --dragging_note_;
      }
      if (resizing_note_ == i) {
        resizing_note_ = -1;
      } else if (resizing_note_ > i) {
        --resizing_note_;
      }
    }
  }

  Config config_;
  std::string storage_dir_;
  std::string current_workspace_key_;
  std::string activation_workspace_key_;
  bool loaded_ = false;

  std::vector<Stroke> strokes_;
  std::vector<Note> notes_;

  bool drawing_ = false;
  bool moved_ = false;
  Stroke current_stroke_;
  double press_x_ = 0.0;
  double press_y_ = 0.0;
  double last_x_ = 0.0;
  double last_y_ = 0.0;

  int dragging_note_ = -1;
  int resizing_note_ = -1;
  int editing_note_ = -1;
  bool cursor_visible_ = true;
  double drag_dx_ = 0.0;
  double drag_dy_ = 0.0;
  double resize_start_x_ = 0.0;
  double resize_start_y_ = 0.0;
  double resize_start_w_ = 0.0;
  double resize_start_h_ = 0.0;
};

class IpcServer {
public:
  explicit IpcServer(HyprInkWindow& window) : window_(window) {}

  ~IpcServer() {
    stop();
  }

  bool start() {
    const auto path = socket_path();
    unlink(path.c_str());

    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
      std::cerr << "hyprink: socket failed: " << std::strerror(errno) << "\n";
      return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      std::cerr << "hyprink: bind failed: " << std::strerror(errno) << "\n";
      close(fd_);
      fd_ = -1;
      return false;
    }

    chmod(path.c_str(), 0600);

    if (listen(fd_, 8) < 0) {
      std::cerr << "hyprink: listen failed: " << std::strerror(errno) << "\n";
      close(fd_);
      fd_ = -1;
      return false;
    }

    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
  }

  void stop() {
    running_ = false;
    if (fd_ >= 0) {
      shutdown(fd_, SHUT_RDWR);
      close(fd_);
      fd_ = -1;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    unlink(socket_path().c_str());
  }

private:
  static gboolean toggle_idle(gpointer data) {
    static_cast<HyprInkWindow*>(data)->toggle();
    return G_SOURCE_REMOVE;
  }

  void run() {
    while (running_) {
      const int client = accept(fd_, nullptr, nullptr);
      if (client < 0) {
        if (running_) {
          std::cerr << "hyprink: accept failed: " << std::strerror(errno) << "\n";
        }
        continue;
      }

      char buffer[64] = {0};
      const ssize_t len = read(client, buffer, sizeof(buffer) - 1);
      close(client);

      if (len <= 0) {
        continue;
      }

      const std::string command(buffer, static_cast<size_t>(len));
      if (command.find("toggle") != std::string::npos) {
        g_idle_add(toggle_idle, &window_);
      }
    }
  }

  HyprInkWindow& window_;
  int fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

static bool send_toggle() {
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const auto path = socket_path();
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(fd);
    return false;
  }

  const char command[] = "toggle\n";
  const ssize_t written = write(fd, command, sizeof(command) - 1);
  close(fd);
  return written == static_cast<ssize_t>(sizeof(command) - 1);
}

int main(int argc, char** argv) {
  bool toggle = false;
  std::string config_path;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--toggle") {
      toggle = true;
      continue;
    }
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: hyprink [--toggle] [--config PATH]\n";
      return 0;
    }
  }

  if (toggle && send_toggle()) {
    return 0;
  }

  auto app = Gtk::Application::create("io.github.hyprink.HyprInk", Gio::APPLICATION_NON_UNIQUE);
  app->hold();
  auto window = std::make_unique<HyprInkWindow>(load_config(config_path));
  IpcServer ipc(*window);
  ipc.start();

  return app->run(*window);
}
