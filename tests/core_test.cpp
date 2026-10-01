#include "core.hpp"
#include <iostream>
#include <functional>
#include <cstdlib>
using namespace hyprink;
static int checks=0;
static void check(bool yes,const char* message) { ++checks; if(!yes) throw std::runtime_error(message); }
template<class F> static void throws(F f,const char* message) { bool raised=false; try{ f(); }catch(const std::exception&){raised=true;} check(raised,message); }
static std::string read_file(const fs::path& p) { std::ifstream f(p); return std::string(std::istreambuf_iterator<char>(f),{}); }
int main() {
  char temp[]="/tmp/hyprink-test-XXXXXX"; const auto directory=fs::path(mkdtemp(temp));
  try {
    std::string text="Příliš žluťoučký kůň 🧭";
    erase_last_grapheme(text); check(text=="Příliš žluťoučký kůň ","Emoji deletion damaged text");
    text="ku\xCC\x8A"; erase_last_grapheme(text); check(text=="k","Combining mark deletion split grapheme");
    text="family 👨‍👩‍👧‍👦"; erase_last_grapheme(text); check(text=="family ","ZWJ emoji deletion split grapheme");
    erase_last_grapheme(text); check(text=="family","ASCII deletion failed");
    text=""; erase_last_grapheme(text); check(text.empty(),"Empty deletion failed");
    check(clean_text("line\r\nnext\rthird\t\x01")=="line\nnext\nthird\t","Clipboard newline/control normalization failed");
    throws([&]{clean_text(std::string("\xff",1));},"Invalid clipboard UTF-8 accepted");
    State state; state.notes.push_back({10,20,260,64,"Příliš žluťoučký kůň"});
    state.strokes.push_back({{1,0.5,0,1},4,{{1,2},{3,4}}});
    check(serialize(decode(encode(state)))==serialize(state),"State did not round trip");
    State blank=state; blank.notes.push_back({10,20,260,64,""});
    check(decode(encode(blank)).notes.size()==2,"Saving removed an actively edited empty note");
    auto bad=encode(state); bad["notes"][0]["w"]="oops";
    throws([&]{decode(bad);},"Malformed geometry accepted");
    bad=encode(state); bad["version"]=2; throws([&]{decode(bad);},"Future format silently overwritten");
    bad=encode(state); bad["notes"][0]["text"]=std::string("\xff",1); throws([&]{decode(bad);},"Invalid saved UTF-8 accepted");
    bad=encode(state); bad["notes"][0]["text"]=std::string("a\0b",3); throws([&]{decode(bad);},"Embedded NUL accepted");
    bad=encode(state); bad["strokes"][0]["color"]["a"]=5; throws([&]{decode(bad);},"Bad alpha accepted");
    History history; State edits=state;
    history.record(edits); edits.notes[0].text+="!";
    history.record(edits); edits.strokes.clear();
    check(history.undo(edits) && edits.strokes.size()==1 && edits.notes[0].text.back()=='!',"Undo lost an unrelated text edit");
    check(history.undo(edits) && edits.notes[0].text==state.notes[0].text,"Text undo failed");
    check(history.redo(edits) && edits.notes[0].text.back()=='!',"Redo failed");
    history.record(edits); edits.notes.clear(); check(!history.redo(edits),"New edit did not invalidate redo");
    History bounded; State counter;
    for(int i=0;i<110;++i) { bounded.record(counter); counter.notes={{0,0,260,64,std::to_string(i)}}; }
    int count=0; while(bounded.undo(counter)) ++count; check(count==100,"History did not respect bound");
    const auto path=directory/"state.json";
    Store store(path); check(store.load().notes.empty(),"First run state is not empty");
    store.save(state); State next=state; next.notes[0].text="Updated"; store.save(next);
    check(read_state(path).notes[0].text=="Updated","New state was not saved");
    check(read_state(path.string()+".bak").notes[0].text==state.notes[0].text,"Backup is not previous valid state");
    struct stat st{}; stat(path.c_str(),&st); check((st.st_mode&0777)==0600,"State is not private");
    atomic_write(path,"{damaged"); Store recovering(path); auto recovered=recovering.load();
    check(recovering.recovered() && recovered.notes[0].text==state.notes[0].text,"Valid backup not recovered");
    bool preserved=false; for(auto& file:fs::directory_iterator(directory))
      if(file.path().filename().string().find(".corrupt.")!=std::string::npos && read_file(file.path())=="{damaged") preserved=true;
    check(preserved,"Damaged state was not preserved");
    recovering.save(recovered); check(read_state(path).notes[0].text==state.notes[0].text,"Recovery could not save");
    const auto blocked_path=directory/"blocked.json"; atomic_write(blocked_path,"bad"); Store blocked(blocked_path);
    throws([&]{blocked.load();},"Broken file without backup was accepted");
    throws([&]{blocked.save(state);},"Broken original was overwritten"); check(read_file(blocked_path)=="bad","Broken original changed");
    const auto before=read_file(path);
    throws([&]{atomic_write(path,std::string(max_state_bytes+1,'x'));},"Oversized state accepted");
    check(read_file(path)==before,"Rejected write damaged previous state");
    State invalid=state; invalid.notes[0].w=-1; throws([&]{store.save(invalid);},"Invalid model was saved");
    check(read_file(path)==before,"Invalid model damaged state");
    atomic_write(directory/"readonly-target", "good");
    fs::create_directory(directory/"cannot-replace.json");
    throws([&]{atomic_write(directory/"cannot-replace.json","new");},"Failed rename did not raise");
    for(const auto& e:fs::directory_iterator(directory)) check(e.path().filename().string().find(".tmp.")==std::string::npos,"Failed write leaked temporary file");
    fs::remove_all(directory); std::cout<<checks<<" checks passed\n"; return 0;
  } catch(const std::exception& e) { fs::remove_all(directory); std::cerr<<e.what()<<'\n'; return 1; }
}
