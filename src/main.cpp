#include <gtkmm.h>
#include <glib-unix.h>
#include <sys/file.h>
#include "core.hpp"
#include <gtk-layer-shell.h>
#include <json/json.h>

#include <algorithm>
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
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

using hyprink::Color;
using hyprink::Point;
using hyprink::Stroke;
using hyprink::Note;
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
  if (explicit_path.empty()) {
    if (const char* config_home = std::getenv("XDG_CONFIG_HOME"))
      candidates.push_back(std::string(config_home) + "/hyprink/Project.conf");
    else candidates.push_back("~/.config/hyprink/Project.conf");
    candidates.push_back("Project.conf");
  }
  if (explicit_path.empty()) {
    candidates.push_back("/usr/local/share/hyprink/Project.conf");
    candidates.push_back("/usr/share/hyprink/Project.conf");
  }

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
    if (!explicit_path.empty()) throw std::runtime_error("Cannot read config: " + explicit_path);
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

  config.stylus_size = std::clamp(config.stylus_size, 1, 100);
  config.font_size = std::clamp(config.font_size, 8, 96);
  config.border_width = std::clamp(config.border_width, 0, 20);
  config.padding = std::clamp(config.padding, 0, 30);
  config.note_width = std::max(80, config.note_width);
  config.note_min_height = std::max(32, config.note_min_height);
  config.delete_distance = std::max(1.0, config.delete_distance);
  if (!std::isfinite(config.delete_distance)) config.delete_distance = 14;
  return config;
}

static void set_source(const Cairo::RefPtr<Cairo::Context>& cr, const Color& color) {
  cr->set_source_rgba(color.r, color.g, color.b, color.a);
}

static std::string active_workspace_key() {
  gchar* stdout_text = nullptr;
  gchar* stderr_text = nullptr;
  gchar* args[] = {const_cast<gchar*>("hyprctl"), const_cast<gchar*>("activeworkspace"), const_cast<gchar*>("-j"), nullptr};
  const gboolean ok = g_spawn_sync(nullptr, args, nullptr, G_SPAWN_SEARCH_PATH,
                                  nullptr, nullptr, &stdout_text, &stderr_text, nullptr, nullptr);
  const std::string output = stdout_text ? stdout_text : "";
  g_free(stdout_text); g_free(stderr_text);
  if (!ok) return "default";

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
  friend class HyprInkTest;
public:
  HyprInkWindow(Config config, const Glib::RefPtr<Gdk::Monitor>& monitor, fs::path file)
    : config_(std::move(config)), store_(std::move(file)) {
    set_title("HyprInk"); set_name("hyprink"); set_decorated(false);
    set_app_paintable(true); set_skip_taskbar_hint(true); set_skip_pager_hint(true);
    add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK |
               Gdk::KEY_PRESS_MASK | Gdk::KEY_RELEASE_MASK | Gdk::FOCUS_CHANGE_MASK);
    set_can_focus(true);
    if (auto screen=get_screen()) if (auto visual=screen->get_rgba_visual())
      gtk_widget_set_visual(GTK_WIDGET(gobj()),visual->gobj());
    gtk_layer_init_for_window(gobj());
    gtk_layer_set_namespace(gobj(),"hyprink");
    gtk_layer_set_monitor(gobj(),monitor->gobj());
    gtk_layer_set_exclusive_zone(gobj(),-1);
    for (auto edge:{GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT,
                   GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM})
      gtk_layer_set_anchor(gobj(),edge,TRUE);
    im_=gtk_im_multicontext_new();
    g_signal_connect(im_,"commit",G_CALLBACK(+[](GtkIMContext*,gchar* text,gpointer self) {
      static_cast<HyprInkWindow*>(self)->append_text(text);
    }),this);
    try { state_=store_.load(); if (store_.recovered()) message_="Recovered backup; damaged file preserved"; }
    catch (const std::exception& e) { message_=std::string("Load failed: ")+e.what(); std::cerr<<"hyprink: "<<message_<<'\n'; }
    cursor_timer_=Glib::signal_timeout().connect([this] {
      if (editing_note_>=0 && edit_mode_) { cursor_visible_=!cursor_visible_; queue_draw(); }
      return true;
    },500);
    show_all(); update_input_mode();
  }
  ~HyprInkWindow() override { cursor_timer_.disconnect(); g_object_unref(im_); }
  bool editing() const { return edit_mode_; }
  void set_edit_mode(bool edit) {
    cancel_pointer_actions(); finish_edit(); edit_mode_=edit;
    if (edit) show_all();
    update_input_mode(); queue_draw();
  }
  void hide_notes() { set_edit_mode(false); hide(); }
  void show_notes() { show_all(); update_input_mode(); }
  void flush() { cancel_pointer_actions(); finish_edit(); }

protected:
  void on_realize() override {
    Gtk::Window::on_realize();
    gtk_im_context_set_client_window(im_,get_window()->gobj()); update_input_mode();
  }
  void on_unrealize() override {
    gtk_im_context_set_client_window(im_,nullptr); Gtk::Window::on_unrealize();
  }
  bool on_focus_in_event(GdkEventFocus* e) override {
    gtk_im_context_focus_in(im_); return Gtk::Window::on_focus_in_event(e);
  }
  bool on_focus_out_event(GdkEventFocus* e) override {
    gtk_im_context_focus_out(im_); return Gtk::Window::on_focus_out_event(e);
  }
  void on_size_allocate(Gtk::Allocation& allocation) override {
    Gtk::Window::on_size_allocate(allocation);
    // Do not rescale the saved model: keep coordinates in logical pixels. Clamp
    // notes only once the compositor has supplied the actual monitor size.
    if (allocation.get_width()>1 && allocation.get_height()>1)
      for (auto& note:state_.notes) clamp_note(note);
  }
  bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override {
    cr->set_operator(Cairo::OPERATOR_CLEAR); cr->paint(); cr->set_operator(Cairo::OPERATOR_OVER);
    // A solid backdrop is an edit aid; passive mode always leaves the desktop visible.
    if (edit_mode_ && config_.background_mode=="black") { set_source(cr,config_.black_color); cr->paint(); }
    cr->set_line_cap(Cairo::LINE_CAP_ROUND); cr->set_line_join(Cairo::LINE_JOIN_ROUND);
    for (const auto& stroke:state_.strokes) draw_stroke(cr,stroke);
    if (drawing_) draw_stroke(cr,current_stroke_);
    for (size_t i=0;i<state_.notes.size();++i) draw_note(cr,state_.notes[i],static_cast<int>(i)==editing_note_);
    if (edit_mode_) draw_toolbar(cr);
    return true;
  }
  bool on_button_press_event(GdkEventButton* e) override {
    if (!edit_mode_) return false;
    if (e->y>=8 && e->y<=42 && e->x>=8 && e->x<488) {
      if (e->button!=1) return true;
      const int button=static_cast<int>((e->x-8)/96);
      if (button==0 || button==1) { cancel_pointer_actions(); finish_edit(); tool_draw_=button==1; queue_draw(); }
      else if (button==2) undo(false);
      else if (button==3) undo(true);
      else set_edit_mode(false);
      return true;
    }
    if (e->button==config_.delete_button) { finish_edit(); delete_at(e->x,e->y); return true; }
    const int hit=note_at(e->x,e->y);
    if (e->type==GDK_2BUTTON_PRESS || e->type==GDK_3BUTTON_PRESS) {
      cancel_pointer_actions(); if (hit>=0) begin_edit(hit); return true;
    }
    if (e->button==config_.resize_button) {
      finish_edit();
      const int resize_hit=note_at(e->x,e->y);
      if (resize_hit>=0) { pointer_before_=state_; resizing_note_=resize_hit; press_x_=e->x; press_y_=e->y;
        resize_start_w_=state_.notes[resize_hit].w; resize_start_h_=state_.notes[resize_hit].h; }
      return true;
    }
    if (e->button!=config_.draw_button) return false;
    // Clicking a note under edit keeps it selected; dragging another finishes it.
    if (editing_note_>=0 && hit==editing_note_) return true;
    finish_edit();
    const int current_hit=note_at(e->x,e->y);
    press_x_=last_x_=e->x; press_y_=last_y_=e->y; moved_=false; pointer_before_=state_;
    if (current_hit>=0) {
      dragging_note_=current_hit; drag_dx_=e->x-state_.notes[current_hit].x;
      drag_dy_=e->y-state_.notes[current_hit].y;
    } else if (tool_draw_) {
      drawing_=true; current_stroke_={config_.stylus_color,static_cast<double>(config_.stylus_size),{{e->x,e->y}}};
    } else {
      history_.record(state_); pointer_before_.reset();
      Note n; n.x=e->x; n.y=e->y; n.w=config_.note_width; n.h=config_.note_min_height;
      clamp_note(n); state_.notes.push_back(n); begin_edit(static_cast<int>(state_.notes.size()-1));
    }
    return true;
  }
  bool on_motion_notify_event(GdkEventMotion* e) override {
    if (!edit_mode_) return false;
    if (std::hypot(e->x-press_x_,e->y-press_y_)>3) moved_=true;
    if (dragging_note_>=0 && moved_) {
      auto& n=state_.notes[dragging_note_]; n.x=e->x-drag_dx_; n.y=e->y-drag_dy_; clamp_note(n); queue_draw();
    } else if (resizing_note_>=0 && moved_) {
      auto& n=state_.notes[resizing_note_];
      n.w=std::max(80.0,resize_start_w_+e->x-press_x_); n.h=std::max<double>(config_.note_min_height,resize_start_h_+e->y-press_y_);
      clamp_note(n); queue_draw();
    } else if (drawing_ && std::hypot(e->x-last_x_,e->y-last_y_)>=1) {
      current_stroke_.points.push_back({e->x,e->y}); last_x_=e->x; last_y_=e->y; queue_draw();
    }
    return true;
  }
  bool on_button_release_event(GdkEventButton* e) override {
    if (!edit_mode_) return false;
    if ((resizing_note_>=0 && e->button!=config_.resize_button) ||
        ((dragging_note_>=0 || drawing_) && e->button!=config_.draw_button)) return false;
    if (drawing_) state_.strokes.push_back(current_stroke_);
    if (pointer_before_ && hyprink::serialize(*pointer_before_)!=hyprink::serialize(state_)) {
      history_.record(*pointer_before_); save();
    }
    const int clicked=dragging_note_; const bool click=!moved_;
    pointer_before_.reset(); drawing_=false; dragging_note_=resizing_note_=-1;
    if (clicked>=0 && click) begin_edit(clicked);
    queue_draw(); return true;
  }
  bool on_key_press_event(GdkEventKey* e) override {
    if (!edit_mode_) return false;
    const bool control=e->state&GDK_CONTROL_MASK;
    const auto key=gdk_keyval_to_lower(e->keyval);
    if (control && key==GDK_KEY_z) { undo(e->state&GDK_SHIFT_MASK); return true; }
    if (control && key==GDK_KEY_y) { undo(true); return true; }
    if (control && key==GDK_KEY_q) { set_edit_mode(false); return true; }
    if (key==GDK_KEY_Escape) { set_edit_mode(false); return true; }
    if (editing_note_<0) {
      if (key==GDK_KEY_F1 || key==GDK_KEY_F2) { tool_draw_=key==GDK_KEY_F2; queue_draw(); }
      return true;
    }
    if (control && (key==GDK_KEY_Return || key==GDK_KEY_KP_Enter)) { finish_edit(); return true; }
    auto& note=state_.notes[editing_note_];
    if (control && (key==GDK_KEY_c || key==GDK_KEY_x)) {
      Gtk::Clipboard::get()->set_text(note.text);
      if (key==GDK_KEY_x && !note.text.empty()) { history_.record(state_); note.text.clear(); save(); queue_draw(); }
      return true;
    }
    if (control && key==GDK_KEY_v) { append_text(Gtk::Clipboard::get()->wait_for_text()); return true; }
    if (key==GDK_KEY_BackSpace) {
      gtk_im_context_reset(im_);
      if (!note.text.empty()) { history_.record(state_); hyprink::erase_last_grapheme(note.text); save(); queue_draw(); }
      return true;
    }
    if (key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) { append_text("\n"); return true; }
    if (control || (e->state&GDK_MOD1_MASK)) return true;
    gtk_im_context_filter_keypress(im_,e); return true;
  }
  bool on_key_release_event(GdkEventKey* e) override {
    if (edit_mode_ && editing_note_>=0) gtk_im_context_filter_keypress(im_,e);
    return edit_mode_;
  }
  bool on_delete_event(GdkEventAny*) override { hide_notes(); return true; }
private:
  void append_text(const std::string& input) {
    if (!edit_mode_ || editing_note_<0) return;
    try {
      const auto text=hyprink::clean_text(input);
      auto& note=state_.notes[editing_note_];
      if (text.empty()) return;
      if (note.text.size()+text.size()>hyprink::max_text_bytes) { message_="Note exceeds 64 KiB limit"; queue_draw(); return; }
      history_.record(state_); note.text+=text; update_note_size(note); save(); queue_draw();
    } catch (const std::exception& e) { message_=e.what(); queue_draw(); }
  }
  void undo(bool redo) {
    gtk_im_context_reset(im_); cancel_pointer_actions(); editing_note_=-1;
    if (redo ? history_.redo(state_) : history_.undo(state_)) { save(); queue_draw(); }
  }
  void begin_edit(int index) { editing_note_=index; cursor_visible_=true; gtk_im_context_reset(im_); grab_focus(); queue_draw(); }
  void finish_edit() {
    gtk_im_context_reset(im_);
    if (editing_note_>=0 && trim(state_.notes[editing_note_].text).empty())
      state_.notes.erase(state_.notes.begin()+editing_note_);
    editing_note_=-1; save(); queue_draw();
  }
  void cancel_pointer_actions() {
    if (pointer_before_) state_=*pointer_before_;
    pointer_before_.reset(); drawing_=false; dragging_note_=resizing_note_=-1;
  }
  GtkLayerShellLayer layer_from_config() const {
    if (config_.layer=="background") return GTK_LAYER_SHELL_LAYER_BACKGROUND;
    if (config_.layer=="top") return GTK_LAYER_SHELL_LAYER_TOP;
    if (config_.layer=="overlay") return GTK_LAYER_SHELL_LAYER_OVERLAY;
    return GTK_LAYER_SHELL_LAYER_BOTTOM;
  }
  void update_input_mode() {
    gtk_layer_set_layer(gobj(),edit_mode_?GTK_LAYER_SHELL_LAYER_OVERLAY:layer_from_config());
    gtk_layer_set_keyboard_mode(gobj(),edit_mode_?GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE:GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    if (edit_mode_) { gtk_widget_input_shape_combine_region(GTK_WIDGET(gobj()),nullptr); grab_focus(); }
    else { cairo_region_t* empty=cairo_region_create(); gtk_widget_input_shape_combine_region(GTK_WIDGET(gobj()),empty); cairo_region_destroy(empty); }
  }
  void save() {
    try { store_.save(state_); if (!store_.recovered()) message_.clear(); }
    catch (const std::exception& e) { message_=std::string("Not saved: ")+e.what(); }
  }
  void draw_toolbar(const Cairo::RefPtr<Cairo::Context>& cr) {
    const char* labels[]={"Notes (F1)","Draw (F2)","Undo","Redo","Done (Esc)"};
    for (int i=0;i<5;++i) {
      cr->set_source_rgba((i==0&&!tool_draw_)||(i==1&&tool_draw_)?0.18:0.08,0.13,0.19,0.96);
      cr->rectangle(8+i*96,8,94,34); cr->fill();
      auto layout=create_pango_layout(labels[i]); Pango::FontDescription font("Sans 11"); layout->set_font_description(font);
      cr->set_source_rgb(1,1,1); cr->move_to(15+i*96,16); layout->show_in_cairo_context(cr);
    }
    if (!message_.empty()) {
      auto layout=create_pango_layout(message_); layout->set_width(std::max(1,get_allocated_width()-16)*PANGO_SCALE); layout->set_wrap(Pango::WRAP_WORD_CHAR);
      cr->set_source_rgb(1,0.5,0.3); cr->move_to(8,48); layout->show_in_cairo_context(cr);
    }
  }
  void draw_note(const Cairo::RefPtr<Cairo::Context>& cr, Note& note, bool editing) {
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
    if (stroke.points.empty()) return;
    if (stroke.points.size() == 1) {
      set_source(cr, stroke.color);
      cr->arc(stroke.points[0].x, stroke.points[0].y, stroke.size/2, 0, 2*M_PI);
      cr->fill(); return;
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
    clamp_note(note);
  }

  int note_at(double x, double y) {
    for (int i = static_cast<int>(state_.notes.size()) - 1; i >= 0; --i) {
      update_note_size(state_.notes[i]);
      const auto& note = state_.notes[i];
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
    for (int i = static_cast<int>(state_.strokes.size()) - 1; i >= 0; --i) {
      const auto& stroke = state_.strokes[i];
      if (stroke.points.empty()) continue;
      if (stroke.points.size()==1 && std::hypot(x-stroke.points[0].x,y-stroke.points[0].y)<=config_.delete_distance+stroke.size/2) return i;

      const double threshold = config_.delete_distance + stroke.size / 2.0;
      for (size_t j = 1; j < stroke.points.size(); ++j) {
        if (point_to_segment_distance(point, stroke.points[j - 1], stroke.points[j]) <= threshold) {
          return i;
        }
      }
    }

    return -1;
  }

  void clamp_note(Note& n) {
    const double w=std::max(1,get_allocated_width()), h=std::max(1,get_allocated_height());
    if (w<=1 || h<=1) return;
    n.w=std::clamp(n.w,32.0,std::max(32.0,w)); n.h=std::clamp(n.h,16.0,std::max(16.0,h));
    n.x=std::clamp(n.x,0.0,std::max(0.0,w-n.w)); n.y=std::clamp(n.y,0.0,std::max(0.0,h-n.h));
  }
  void delete_at(double x,double y) {
    if (int i=note_at(x,y);i>=0) { history_.record(state_); state_.notes.erase(state_.notes.begin()+i); }
    else if (int s=stroke_at(x,y);s>=0) { history_.record(state_); state_.strokes.erase(state_.strokes.begin()+s); }
    else return;
    save(); queue_draw();
  }
  Config config_; hyprink::Store store_; hyprink::State state_; hyprink::History history_;
  std::optional<hyprink::State> pointer_before_;
  GtkIMContext* im_=nullptr; sigc::connection cursor_timer_;
  bool edit_mode_=false, tool_draw_=false, drawing_=false, moved_=false, cursor_visible_=true;
  Stroke current_stroke_; int dragging_note_=-1,resizing_note_=-1,editing_note_=-1;
  double press_x_=0,press_y_=0,last_x_=0,last_y_=0,drag_dx_=0,drag_dy_=0,resize_start_w_=0,resize_start_h_=0;
  std::string message_;
};

static std::string safe_name(std::string name) {
  for (char& c:name) if (!std::isalnum(static_cast<unsigned char>(c)) && c!='-' && c!='_') c='_';
  return name;
}
static std::string monitor_name(GdkMonitor* monitor,int index) {
  // Connector names from Hyprland survive resolution/scaling/rearrangement.
  GdkRectangle geometry; gdk_monitor_get_geometry(monitor,&geometry);
  gchar* out=nullptr; gchar* err=nullptr;
  gchar* args[]={const_cast<gchar*>("hyprctl"),const_cast<gchar*>("monitors"),const_cast<gchar*>("-j"),nullptr};
  g_spawn_sync(nullptr,args,nullptr,G_SPAWN_SEARCH_PATH,nullptr,nullptr,&out,&err,nullptr,nullptr);
  std::string json=out?out:""; g_free(out); g_free(err);
  Json::Value monitors; Json::CharReaderBuilder b; std::string errors; std::istringstream stream(json);
  if (Json::parseFromStream(b,stream,&monitors,&errors) && monitors.isArray())
    for (const auto& m:monitors)
      if (m["x"].isInt() && m["y"].isInt() && m["x"].asInt()==geometry.x && m["y"].asInt()==geometry.y && m["name"].isString())
        return safe_name(m["name"].asString());
  const char* model=gdk_monitor_get_model(monitor);
  return safe_name(std::string(model?model:"monitor")+"-"+std::to_string(index));
}
class Desktop {
public:
  Desktop(const Glib::RefPtr<Gtk::Application>& app,Config config):app_(app),config_(std::move(config)),display_(Gdk::Display::get_default()) {
    refresh();
    added_=display_->signal_monitor_added().connect([this](const Glib::RefPtr<Gdk::Monitor>&){ refresh_safely(); });
    removed_=display_->signal_monitor_removed().connect([this](const Glib::RefPtr<Gdk::Monitor>&){ refresh_safely(); });
    workspace_=active_workspace_key();
    poll_=Glib::signal_timeout().connect([this] {
      if (any_editing()) {
        const auto now=active_workspace_key();
        if (now!=workspace_) { for(auto& e:windows_) e.window->set_edit_mode(false); workspace_=now; }
      }
      return true;
    },500);
  }
  ~Desktop() { poll_.disconnect(); added_.disconnect(); removed_.disconnect(); flush(); }
  void command(const std::string& command) {
    if (command=="quit") { flush(); app_->quit(); return; }
    if (command=="hide") { for (auto& e:windows_) e.window->hide_notes(); return; }
    if (command=="show") { for (auto& e:windows_) e.window->show_notes(); return; }
    if (command=="toggle" || command=="edit") {
      const bool was_editing=any_editing();
      for (auto& e:windows_) e.window->set_edit_mode(false);
      if (command=="edit" || !was_editing) {
        workspace_=active_workspace_key();
        if (auto* window=focused_window()) window->set_edit_mode(true);
      }
    }
  }
  void flush() { for(auto& e:windows_) e.window->flush(); }
private:
  struct Entry { Glib::RefPtr<Gdk::Monitor> monitor; std::unique_ptr<HyprInkWindow> window; std::string name; };
  bool any_editing() const { for(const auto& e:windows_) if(e.window->editing()) return true; return false; }
  HyprInkWindow* focused_window() {
    gchar* out=nullptr; gchar* err=nullptr;
    gchar* args[]={const_cast<gchar*>("hyprctl"),const_cast<gchar*>("monitors"),const_cast<gchar*>("-j"),nullptr};
    g_spawn_sync(nullptr,args,nullptr,G_SPAWN_SEARCH_PATH,nullptr,nullptr,&out,&err,nullptr,nullptr);
    std::string json=out?out:""; g_free(out); g_free(err);
    Json::Value monitors; Json::CharReaderBuilder builder; std::string errors; std::istringstream stream(json);
    if(Json::parseFromStream(builder,stream,&monitors,&errors) && monitors.isArray())
      for(const auto& m:monitors) if(m["focused"].isBool() && m["focused"].asBool() && m["name"].isString())
        for(auto& e:windows_) if(e.name==safe_name(m["name"].asString())) return e.window.get();
    int x=0,y=0; GdkScreen* screen=nullptr;
    if (auto* seat=gdk_display_get_default_seat(display_->gobj()))
      if (auto* pointer=gdk_seat_get_pointer(seat)) gdk_device_get_position(pointer,&screen,&x,&y);
    auto* monitor=gdk_display_get_monitor_at_point(display_->gobj(),x,y);
    for(auto& e:windows_) if(e.monitor->gobj()==monitor) return e.window.get();
    return windows_.empty()?nullptr:windows_[0].window.get();
  }
  void refresh_safely() {
    try { refresh(); } catch(const std::exception& e) { std::cerr<<"hyprink: monitor update failed: "<<e.what()<<'\n'; }
  }
  void refresh() {
    std::vector<Glib::RefPtr<Gdk::Monitor>> monitors;
    for(int i=0;i<display_->get_n_monitors();++i) monitors.push_back(display_->get_monitor(i));
    windows_.erase(std::remove_if(windows_.begin(),windows_.end(),[&](Entry& e) {
      if(std::find(monitors.begin(),monitors.end(),e.monitor)!=monitors.end()) return false;
      e.window->flush(); app_->remove_window(*e.window); return true;
    }),windows_.end());
    const fs::path storage=expand_user(config_.storage_path);
    for(size_t i=0;i<monitors.size();++i) {
      auto monitor=monitors[i];
      if(std::any_of(windows_.begin(),windows_.end(),[&](const Entry& e){ return e.monitor==monitor; })) continue;
      const auto name=monitor_name(monitor->gobj(),static_cast<int>(i));
      const auto file=storage/("state-"+name+".json");
      // Copy the legacy single-screen state once; keep the original as a backup.
      if (i==0 && !fs::exists(file) && fs::exists(storage/"state.json")) {
        try { hyprink::atomic_write(file,hyprink::serialize(hyprink::read_state(storage/"state.json"))); }
        catch(const std::exception& e) { throw std::runtime_error(std::string("Legacy state migration failed; original preserved: ")+e.what()); }
      }
      auto window=std::make_unique<HyprInkWindow>(config_,monitor,file);
      app_->add_window(*window); windows_.push_back({monitor,std::move(window),name});
    }
  }
  Glib::RefPtr<Gtk::Application> app_; Config config_; Glib::RefPtr<Gdk::Display> display_;
  std::vector<Entry> windows_; sigc::connection added_,removed_,poll_; std::string workspace_;
};

static fs::path runtime_path() {
  fs::path path;
  if(const char* runtime=std::getenv("XDG_RUNTIME_DIR")) path=runtime;
  else {
    path=fs::path("/tmp")/("hyprink-"+std::to_string(getuid()));
    if (g_mkdir_with_parents(path.c_str(),0700)<0) throw std::runtime_error("Cannot create private runtime directory");
  }
  std::error_code error; fs::create_directories(path,error);
  struct stat st{};
  if(error || lstat(path.c_str(),&st)<0 || !S_ISDIR(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&0077))
    throw std::runtime_error("Runtime directory must be owned by you and have mode 0700");
  return path;
}
static std::string socket_path() {
  const char* display=std::getenv("WAYLAND_DISPLAY");
  return (runtime_path()/("hyprink-"+safe_name(display?display:"default")+".sock")).string();
}
static sockaddr_un socket_address(const std::string& path) {
  sockaddr_un address{}; address.sun_family=AF_UNIX;
  if(path.size()>=sizeof(address.sun_path)) throw std::runtime_error("Runtime socket path is too long");
  std::memcpy(address.sun_path,path.c_str(),path.size()+1); return address;
}
static bool send_command(const std::string& command) {
  const int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);
  if(fd<0) throw std::runtime_error("Cannot create command socket");
  const auto address=socket_address(socket_path());
  const auto n=sendto(fd,command.data(),command.size(),MSG_NOSIGNAL,reinterpret_cast<const sockaddr*>(&address),sizeof(address));
  close(fd); return n==static_cast<ssize_t>(command.size());
}
class IpcServer {
public:
  explicit IpcServer(std::function<void(const std::string&)> callback):callback_(std::move(callback)),path_(socket_path()) {
    const auto address=socket_address(path_);
    lock_=open((path_+".lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock_<0 || flock(lock_,LOCK_EX|LOCK_NB)<0) { if(lock_>=0) close(lock_); lock_=-1; throw std::runtime_error("HyprInk is already running (try --toggle)"); }
    fd_=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    unlink(path_.c_str());
    if(fd_<0 || bind(fd_,reinterpret_cast<const sockaddr*>(&address),sizeof(address))<0) {
      if(fd_>=0) close(fd_);
      close(lock_); fd_=lock_=-1; throw std::runtime_error("Cannot bind command socket");
    }
    chmod(path_.c_str(),0600);
    source_=g_unix_fd_add(fd_,G_IO_IN,+[](gint fd,GIOCondition,gpointer self)->gboolean {
      char text[64]; const auto n=recv(fd,text,sizeof(text),0);
      if(n>0 && n<static_cast<ssize_t>(sizeof(text))) {
        try { static_cast<IpcServer*>(self)->callback_(std::string(text,n)); }
        catch(const std::exception& e) { std::cerr<<"hyprink: "<<e.what()<<'\n'; }
      }
      return G_SOURCE_CONTINUE;
    },this);
  }
  ~IpcServer() { if(source_) g_source_remove(source_); if(fd_>=0) { close(fd_); unlink(path_.c_str()); } if(lock_>=0) close(lock_); }
private:
  std::function<void(const std::string&)> callback_; std::string path_; int fd_=-1,lock_=-1; guint source_=0;
};

int main(int argc,char** argv) {
  std::string command,config_path;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--help" || arg=="-h") {
      std::cout<<"HyprInk "<<HYPRINK_VERSION<<"\nUsage: hyprink [--toggle|--edit|--show|--hide|--quit] [--config PATH]\n"
        <<"Default: show saved notes with mouse/keyboard pass-through. --toggle toggles editing.\n"; return 0;
    }
    if(arg=="--version") { std::cout<<HYPRINK_VERSION<<'\n'; return 0; }
    if(arg=="--config" && i+1<argc) { config_path=argv[++i]; continue; }
    if(arg=="--toggle" || arg=="--edit" || arg=="--show" || arg=="--hide" || arg=="--quit") {
      if(!command.empty()) { std::cerr<<"Choose only one command\n"; return 2; }
      command=arg.substr(2); continue;
    }
    std::cerr<<"Unknown or incomplete option: "<<arg<<'\n'; return 2;
  }
  try {
    if(send_command(command.empty()?"show":command)) return 0;
    if(command=="quit" || command=="hide") return 0;
    const auto config=load_config(config_path);
    auto app=Gtk::Application::create("io.github.hyprink.HyprInk",Gio::APPLICATION_NON_UNIQUE);
    if(!gtk_layer_is_supported()) throw std::runtime_error("A Wayland compositor with layer-shell support is required");
    app->register_application();
    app->signal_activate().connect([] {});
    app->hold();
    std::unique_ptr<Desktop> desktop;
    IpcServer ipc([&](const std::string& action) { if(desktop) desktop->command(action); });
    desktop=std::make_unique<Desktop>(app,config);
    if(!command.empty()) desktop->command(command);
    app->signal_shutdown().connect([&] { desktop->flush(); });
    auto stop=+[](gpointer data)->gboolean { static_cast<Desktop*>(data)->command("quit"); return G_SOURCE_CONTINUE; };
    const guint term=g_unix_signal_add(SIGTERM,stop,desktop.get());
    const guint interrupt=g_unix_signal_add(SIGINT,stop,desktop.get());
    const int result=app->run();
    g_source_remove(term); g_source_remove(interrupt);
    return result;
  } catch(const std::exception& e) { std::cerr<<"hyprink: "<<e.what()<<'\n'; return 1; }
}
