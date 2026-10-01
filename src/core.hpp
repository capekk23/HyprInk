#pragma once

#include <json/json.h>
#include <pango/pango.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hyprink {
namespace fs = std::filesystem;
constexpr size_t max_text_bytes = 65536;
constexpr size_t max_state_bytes = 16 * 1024 * 1024;
struct Color { double r=1, g=1, b=1, a=1; };
struct Point { double x=0, y=0; };
struct Stroke { Color color; double size=4; std::vector<Point> points; };
struct Note { double x=0, y=0, w=260, h=64; std::string text; };
struct State { std::vector<Stroke> strokes; std::vector<Note> notes; };

// Pango cursor boundaries remove a complete grapheme, including combining marks.
inline void erase_last_grapheme(std::string& text) {
  if (text.empty()) return;
  if (!g_utf8_validate(text.data(), text.size(), nullptr)) throw std::runtime_error("Invalid UTF-8 text");
  const int count = g_utf8_strlen(text.data(), text.size());
  std::vector<PangoLogAttr> attrs(count + 1);
  pango_get_log_attrs(text.data(), static_cast<int>(text.size()), -1,
                      pango_language_get_default(), attrs.data(), count + 1);
  int previous = count - 1;
  while (previous > 0 && !attrs[previous].is_cursor_position) --previous;
  text.resize(g_utf8_offset_to_pointer(text.c_str(), previous) - text.c_str());
}
inline std::string clean_text(const std::string& input) {
  if (!g_utf8_validate(input.data(), input.size(), nullptr)) throw std::runtime_error("Invalid UTF-8 text");
  std::string out;
  for (const char* p=input.c_str(); *p; p=g_utf8_next_char(p)) {
    gunichar c=g_utf8_get_char(p);
    if (c=='\r') { if (p[1]!='\n') out+='\n'; }
    else if (c=='\t' || c=='\n' || !g_unichar_iscntrl(c)) out.append(p, g_utf8_next_char(p)-p);
  }
  return out;
}
inline Json::Value encode(const State& state) {
  Json::Value root(Json::objectValue);
  root["version"]=1;
  root["strokes"]=Json::Value(Json::arrayValue);
  root["notes"]=Json::Value(Json::arrayValue);
  for (const auto& s:state.strokes) {
    Json::Value v;
    v["color"]["r"]=s.color.r; v["color"]["g"]=s.color.g;
    v["color"]["b"]=s.color.b; v["color"]["a"]=s.color.a;
    v["size"]=s.size; v["points"]=Json::Value(Json::arrayValue);
    for (const auto& p:s.points) { Json::Value point; point["x"]=p.x; point["y"]=p.y; v["points"].append(point); }
    root["strokes"].append(v);
  }
  for (const auto& n:state.notes) {
    Json::Value v;
    v["x"]=n.x; v["y"]=n.y; v["w"]=n.w; v["h"]=n.h; v["text"]=n.text;
    root["notes"].append(v);
  }
  return root;
}
inline double number(const Json::Value& v, const char* key, double fallback) {
  if (!v.isMember(key)) return fallback;
  if (!v[key].isNumeric() || !std::isfinite(v[key].asDouble())) throw std::runtime_error("Invalid state number");
  const double x=v[key].asDouble();
  if (std::abs(x)>1000000) throw std::runtime_error("State coordinate out of range");
  return x;
}
inline State decode(const Json::Value& root) {
  if (!root.isObject() || !root["version"].isInt() || root["version"].asInt()!=1)
    throw std::runtime_error("Unsupported state format");
  State out;
  for (const auto* key:{"strokes", "notes"})
    if (!root[key].isNull() && !root[key].isArray()) throw std::runtime_error("Invalid state array");
  if (root["strokes"].size()>10000 || root["notes"].size()>10000) throw std::runtime_error("Too many items");
  size_t total_points=0;
  for (const auto& v:root["strokes"]) {
    if (!v.isObject() || !v["points"].isArray()) throw std::runtime_error("Invalid stroke");
    Stroke s;
    s.size=number(v,"size",4);
    if (s.size<=0 || s.size>1000) throw std::runtime_error("Invalid stroke width");
    const auto& c=v["color"];
    if (!c.isNull() && !c.isObject()) throw std::runtime_error("Invalid stroke color");
    s.color={number(c,"r",1),number(c,"g",1),number(c,"b",1),number(c,"a",1)};
    for (double channel:{s.color.r,s.color.g,s.color.b,s.color.a})
      if (channel<0 || channel>1) throw std::runtime_error("Invalid color channel");
    total_points+=v["points"].size();
    if (total_points>200000) throw std::runtime_error("Too many stroke points");
    for (const auto& p:v["points"]) {
      if (!p.isObject()) throw std::runtime_error("Invalid point");
      s.points.push_back({number(p,"x",0),number(p,"y",0)});
    }
    if (!s.points.empty()) out.strokes.push_back(std::move(s));
  }
  for (const auto& v:root["notes"]) {
    if (!v.isObject() || !v["text"].isString()) throw std::runtime_error("Invalid note");
    Note n{number(v,"x",0),number(v,"y",0),number(v,"w",260),number(v,"h",64),v["text"].asString()};
    if (n.w<32 || n.h<16 || n.text.size()>max_text_bytes || n.text.find('\0')!=std::string::npos ||
        !g_utf8_validate(n.text.data(),n.text.size(),nullptr)) throw std::runtime_error("Invalid note contents");
    out.notes.push_back(std::move(n));
  }
  return out;
}
inline std::string serialize(const State& state) {
  Json::StreamWriterBuilder writer; writer["indentation"]="";
  return Json::writeString(writer,encode(state));
}
class History {
public:
  void record(const State& before) {
    redo_.clear(); undo_.push_back(serialize(before)); trim(undo_);
  }
  bool undo(State& state) { return move(undo_,redo_,state); }
  bool redo(State& state) { return move(redo_,undo_,state); }
private:
  static State parse(const std::string& text) {
    Json::CharReaderBuilder b; Json::Value v; std::string errors;
    auto reader=std::unique_ptr<Json::CharReader>(b.newCharReader());
    if (!reader->parse(text.data(),text.data()+text.size(),&v,&errors)) throw std::runtime_error(errors);
    return decode(v);
  }
  static void trim(std::deque<std::string>& stack) {
    size_t bytes=0; for (const auto& s:stack) bytes+=s.size();
    while (stack.size()>100 || (bytes>32*1024*1024 && stack.size()>1)) {
      bytes-=stack.front().size(); stack.pop_front();
    }
  }
  static bool move(std::deque<std::string>& from,std::deque<std::string>& to,State& state) {
    if (from.empty()) return false;
    State next=parse(from.back()); to.push_back(serialize(state)); trim(to);
    from.pop_back(); state=std::move(next); return true;
  }
  std::deque<std::string> undo_,redo_;
};
// Same-directory temp file + fsync + rename: the old file survives failed writes.
inline void atomic_write(const fs::path& path,const std::string& contents) {
  if (contents.size()>max_state_bytes) throw std::runtime_error("State exceeds 16 MiB limit");
  fs::create_directories(path.parent_path());
  std::string temp=path.string()+".tmp.XXXXXX";
  int fd=mkstemp(temp.data());
  if (fd<0) throw std::runtime_error("Cannot create state temporary file: "+std::string(std::strerror(errno)));
  bool renamed=false;
  try {
    size_t offset=0;
    while (offset<contents.size()) {
      ssize_t n=write(fd,contents.data()+offset,contents.size()-offset);
      if (n<0 && errno==EINTR) continue;
      if (n<=0) throw std::runtime_error("Cannot write state");
      offset+=n;
    }
    if (fsync(fd)<0) throw std::runtime_error("Cannot sync state");
    const int closed=close(fd); fd=-1;
    if (closed<0) throw std::runtime_error("Cannot close state");
    if (rename(temp.c_str(),path.c_str())<0) throw std::runtime_error("Cannot replace state");
    renamed=true;
    int dir=open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY);
    if (dir>=0) { fsync(dir); close(dir); }
  } catch (...) { if (fd>=0) close(fd); if (!renamed) unlink(temp.c_str()); throw; }
}
inline State read_state(const fs::path& path) {
  if (fs::file_size(path)>max_state_bytes) throw std::runtime_error("State exceeds 16 MiB limit");
  std::ifstream file(path); if (!file) throw std::runtime_error("Cannot read state");
  Json::CharReaderBuilder b; b["rejectDupKeys"]=true; b["failIfExtra"]=true;
  Json::Value root; std::string errors;
  if (!Json::parseFromStream(b,file,&root,&errors)) throw std::runtime_error("Invalid state JSON");
  return decode(root);
}
class Store {
public:
  explicit Store(fs::path path):path_(std::move(path)) {}
  State load() {
    writable_=false;
    if (!fs::exists(path_)) { writable_=true; return {}; }
    try { State s=read_state(path_); writable_=true; return s; }
    catch (const std::exception&) {
      // Do not overwrite an unreadable file. Recover only from a valid backup.
      State backup=read_state(path_.string()+".bak");
      std::string corrupt=path_.string()+".corrupt.XXXXXX";
      int fd=mkstemp(corrupt.data());
      if (fd<0) throw std::runtime_error("Cannot preserve damaged state");
      close(fd);
      try { fs::copy_file(path_,corrupt,fs::copy_options::overwrite_existing); }
      catch (...) { unlink(corrupt.c_str()); throw; }
      recovered_=true; writable_=true; return backup;
    }
  }
  void save(const State& state) {
    if (!writable_) throw std::runtime_error("State could not be loaded; original file is preserved");
    const auto text=serialize(state);
    // Validate before touching either file. Never back up an invalid current file.
    decode(encode(state));
    if (fs::exists(path_) && !recovered_) atomic_write(path_.string()+".bak",serialize(read_state(path_)));
    atomic_write(path_,text); recovered_=false;
  }
  bool recovered() const { return recovered_; }
private:
  fs::path path_; bool writable_=false, recovered_=false;
};
} // namespace hyprink
