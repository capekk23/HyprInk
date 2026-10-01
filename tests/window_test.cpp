// Exercise real window event handlers under a layer-shell compositor.
#define main hyprink_application_main
#include "../src/main.cpp"
#undef main
#include <iostream>
class HyprInkTest {
public:
  static void run(HyprInkWindow& w) {
    auto check=[](bool yes,const char* text) { if(!yes) throw std::runtime_error(text); };
    auto press=[&](double x,double y,unsigned button=1) {
      GdkEventButton e{}; e.type=GDK_BUTTON_PRESS; e.x=x; e.y=y; e.button=button;
      w.on_button_press_event(&e);
    };
    auto release=[&](double x,double y,unsigned button=1) {
      GdkEventButton e{}; e.type=GDK_BUTTON_RELEASE; e.x=x; e.y=y; e.button=button;
      w.on_button_release_event(&e);
    };
    auto key=[&](guint value,guint modifiers=0) {
      GdkEventKey e{}; e.type=GDK_KEY_PRESS; e.keyval=value; e.state=modifiers; w.on_key_press_event(&e);
    };
    w.set_edit_mode(true);
    press(180,180); release(180,180);
    check(w.editing_note_==0 && w.state_.notes.size()==1,"New blank note was removed");
    key(GDK_KEY_BackSpace);
    check(w.editing_note_==0,"Backspace on blank note ended editing");
    g_signal_emit_by_name(w.im_,"commit","Příliš žluťoučký kůň 🧭");
    check(w.state_.notes[0].text=="Příliš žluťoučký kůň 🧭","IME commit damaged Czech text");
    key(GDK_KEY_BackSpace); check(w.state_.notes[0].text=="Příliš žluťoučký kůň ","Backspace split emoji");
    Gtk::Clipboard::get()->set_text("\r\nPaste ✅");
    key(GDK_KEY_v,GDK_CONTROL_MASK);
    check(w.state_.notes[0].text=="Příliš žluťoučký kůň \nPaste ✅","Paste failed");
    key(GDK_KEY_z,GDK_CONTROL_MASK); check(w.state_.notes[0].text=="Příliš žluťoučký kůň ","Paste undo failed");
    key(GDK_KEY_z,GDK_CONTROL_MASK|GDK_SHIFT_MASK); check(w.state_.notes[0].text.find("Paste")!=std::string::npos,"Paste redo failed");
    key(GDK_KEY_F2); press(500,250);
    GdkEventMotion move{}; move.x=600; move.y=350; w.on_motion_notify_event(&move); release(600,350);
    check(w.state_.strokes.size()==1,"Stroke was not committed");
    key(GDK_KEY_z,GDK_CONTROL_MASK); check(w.state_.strokes.empty(),"Stroke undo failed");
    key(GDK_KEY_y,GDK_CONTROL_MASK); check(w.state_.strokes.size()==1,"Stroke redo failed");
    press(500,250,2); release(500,250,2); check(w.state_.strokes.empty(),"Erase failed");
    key(GDK_KEY_z,GDK_CONTROL_MASK); check(w.state_.strokes.size()==1,"Erase undo failed");
    press(300,200); move.x=400; move.y=300; w.on_motion_notify_event(&move); release(400,300);
    check(w.state_.notes[0].x>180,"Drag failed");
    const auto saved=w.state_.notes[0].x;
    press(saved+20,w.state_.notes[0].y+20); move.x=750; move.y=400; w.on_motion_notify_event(&move);
    w.set_edit_mode(false); check(w.state_.notes[0].x==saved,"Cancelled drag changed saved note");
    check(gtk_layer_get_keyboard_mode(w.gobj())==GTK_LAYER_SHELL_KEYBOARD_MODE_NONE,"Passive mode holds keyboard");
    check(gtk_layer_get_layer(w.gobj())==GTK_LAYER_SHELL_LAYER_BOTTOM,"Passive mode uses wrong layer");
    w.set_edit_mode(true); check(gtk_layer_get_layer(w.gobj())==GTK_LAYER_SHELL_LAYER_OVERLAY,"Edit mode not on overlay");
    check(gtk_layer_get_keyboard_mode(w.gobj())==GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE,"Edit keyboard not captured");
    w.state_.notes[0].text="HyprInk\nPříliš žluťoučký kůň\n\nDesktop notes + drawing\nCtrl+Z / Ctrl+Shift+Z\nCtrl+V to paste";
    w.queue_draw();
  }
};
int main() {
  if (!std::getenv("WAYLAND_DISPLAY") && !std::getenv("DISPLAY")) return 77;
  char temp[]="/tmp/hyprink-window-XXXXXX"; const auto directory=fs::path(mkdtemp(temp));
  try {
    auto app=Gtk::Application::create("io.github.hyprink.WindowTest",Gio::APPLICATION_NON_UNIQUE);
    if(!gtk_layer_is_supported()) { fs::remove_all(directory); return 77; }
    app->register_application(); app->signal_activate().connect([] {}); app->hold();
    Config config; config.storage_path=directory.string();
    const auto display=Gdk::Display::get_default();
    HyprInkWindow window(config,display->get_monitor(0),directory/"state.json"); app->add_window(window);
    int result=0;
    Glib::signal_timeout().connect([&] {
      try { HyprInkTest::run(window); std::cout<<"Window regression scenarios passed\n"; }
      catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; result=1; app->quit(); }
      return false;
    },500);
    Glib::signal_timeout().connect([&] {
      if(const char* target=std::getenv("HYPRINK_SCREENSHOT")) {
        gchar* args[]={const_cast<gchar*>("grim"),const_cast<gchar*>("-o"),const_cast<gchar*>("HEADLESS-1"),const_cast<gchar*>(target),nullptr};
        g_spawn_sync(nullptr,args,nullptr,G_SPAWN_SEARCH_PATH,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
      }
      window.flush(); app->quit(); return false;
    },1200);
    app->run();
    if(result==0) {
      const auto saved=hyprink::read_state(directory/"state.json");
      if(saved.notes.size()!=1 || saved.strokes.size()!=1 || saved.notes[0].text.find("žluťoučký")==std::string::npos) {
        std::cerr<<"Restart state did not retain notes and drawing\n"; result=1;
      }
    }
    fs::remove_all(directory); return result;
  } catch(const std::exception& e) { fs::remove_all(directory); std::cerr<<e.what()<<'\n'; return 1; }
}
