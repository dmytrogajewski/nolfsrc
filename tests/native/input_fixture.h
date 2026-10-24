#pragma once
#include "engine/client/client_input.h"
#include "engine/portable/command_reader.h"
#include "engine/portable/string.h"
#include <stdexcept>

namespace input_fixture {
inline void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
inline std::vector<nolf::platform::InputControl> controls() {
    std::vector<nolf::platform::InputControl> result;
    for (const auto code : {1u,17u,57u}) result.push_back({1,9,code,code==17 ? unsigned('W') : code==57 ? 32u : 27u,"Keyboard",std::to_string(code),std::to_string(code),0,255,false});
    for (unsigned i=0;i<3;++i) result.push_back({2,i+1,256+i,0,"Mouse",std::string(1,char('x'+i))+"-axis","axis",-32768,32767,true});
    for (unsigned i=0;i<3;++i) result.push_back({2,8,i,0,"Mouse","button "+std::to_string(i),"Button",0,255,false});
    result.push_back({4,1,256,0,"Joystick 1","x-axis","X axis",-32768,32767,false});
    result.push_back({4,8,0,0,"Joystick 1","button 0","Button",0,255,false}); return result;
}
inline bool input_command(std::string_view name) { return nolf::ascii_compare(name,"addaction")==0 || nolf::ascii_compare(name,"rangebind")==0 || nolf::ascii_compare(name,"scale")==0 || nolf::ascii_compare(name,"rangescale")==0 || nolf::ascii_compare(name,"enabledevice")==0; }
inline unsigned configure(nolf::ClientInput& input,std::string_view text,bool only_input=false) {
    nolf::CommandReader reader(text); unsigned count=0;
    while (const auto row=reader.next()) {
        if (row->empty() || (only_input && !input_command(row->front()))) continue;
        std::vector<std::string_view> args; for (std::size_t i=1;i<row->size();++i) args.push_back((*row)[i]);
        const auto code=input.command(row->front(),args);
        if (code!=LT_OK) throw std::runtime_error("input command failed: "+row->front()+" result="+std::to_string(code));
        ++count;
    }
    return count;
}
inline nolf::platform::InputEvent key(unsigned code,bool down) { nolf::platform::InputEvent event{}; event.type=nolf::platform::InputType::key; event.code=int(code); event.pressed=down; return event; }
}
