#include "engine/client/client_input.h"
#include "engine/portable/string.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <deque>
#include <map>
#include <thread>

namespace nolf {
namespace {
struct Inactive {};
template<class F> LTRESULT sdk(F&& fn) noexcept {
    try { return fn(); } catch (const Inactive&) { return LT_NOTINITIALIZED; }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (...) { return LT_INVALIDPARAMS; }
}
template<std::size_t N> void copy(char (&to)[N],std::string_view from) { const auto size=std::min(N-1,from.size()); std::copy_n(from.data(),size,to); to[size]=0; }
bool same(std::string_view a,std::string_view b) { return ascii_compare(a,b)==0; }
std::string_view untag(std::string_view value) { return value.starts_with("##") ? value.substr(2) : value; }
unsigned device(std::string_view name) { name=untag(name); if (same(name,"keyboard")) return 1; if (same(name,"mouse")) return 2; if (same(name,"gamepad") || same(name,"Joystick 1")) return 4; return 0; }
float number(std::string_view text) { float result=0; if (text.starts_with('+')) text.remove_prefix(1); const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result); if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(result)) throw std::invalid_argument("invalid input number"); return result; }
int integer(std::string_view text) { int result=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result); if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) throw std::invalid_argument("invalid input action code"); return result; }
std::string quote(std::string_view text) { std::string result="\""; for (unsigned char c : text) { if (c=='%' || c=='\"' || c=='(' || c==')' || c==';' || c=='!' || c<32 || c==127) result+='%'; result+=char(c); } result+='\"'; return result; }
struct Action { std::string name; int code=0; };
struct Binding { std::string name; float low=0,high=0; };
struct Control {
    platform::InputControl info;
    std::vector<Binding> bindings;
    float value=0,scale=1,range_min=0,range_max=0,center_offset=0;
    bool press=false,pulse=false;
};
struct BindingList { std::vector<std::unique_ptr<DeviceBinding>> rows; std::vector<std::unique_ptr<GameAction>> actions; };
struct ObjectList { std::vector<std::unique_ptr<DeviceObject>> rows; };
}
struct ClientInput::State {
    const std::thread::id owner=std::this_thread::get_id();
    std::vector<Control> controls;
    std::map<std::string,Action> actions;
    std::array<bool,256> commands{},previous{},toggle{},latched{},raw_previous{};
    std::array<float,3> axes{};
    unsigned available=3,enabled=3,tracking=0,capacity=32;
    bool input=true,focused=true,overflow=false;
    std::deque<DeviceInput> events;
    std::map<DeviceBinding*,BindingList> binding_lists;
    std::map<DeviceObject*,ObjectList> object_lists;
    void thread() const { if (owner!=std::this_thread::get_id()) throw Inactive{}; }
    static State& active() { if (!current_) throw Inactive{}; current_->thread(); return *current_; }
    Control* find(unsigned dev,unsigned code) { for (auto& control : controls) if (control.info.device==dev && control.info.code==code) return &control; return nullptr; }
    Control* find(std::string_view dev,std::string_view name) {
        const auto type=device(dev); const auto stripped=untag(name);
        for (auto& control : controls) if (control.info.device==type) {
            const auto& info=control.info;
            if (same(info.name,stripped)) return &control;
            if (type==2 && name.starts_with("##") && info.type==CONTROLTYPE_BUTTON && stripped==std::to_string(info.code+3)) return &control;
        }
        for (auto& control : controls) if (control.info.device==type && same(control.info.label,stripped)) return &control;
        return nullptr;
    }
    const Action* action(std::string_view name) const { for (const auto& [key,value] : actions) if (same(key,name)) return &value; return nullptr; }
    void clear() {
        for (auto& control : controls) { control.value=0; control.press=false; control.pulse=false; }
        commands.fill(false); axes.fill(0); latched.fill(false); raw_previous.fill(false); events.clear(); overflow=false;
    }
    void event(unsigned dev,unsigned code,float value) {
        auto* control=find(dev,code); if (!control || !std::isfinite(value)) return;
        if (control->info.relative ? value==0 : value==control->value) return;
        if (control->info.relative) control->value=std::clamp(control->value+value,-1e6f,1e6f);
        else { if (value && !control->value) { control->press=true; control->pulse=true; } control->value=value; }
        if (tracking&dev) {
            if (events.size()>=capacity) { events.pop_front(); overflow=true; }
            DeviceInput input{}; input.m_DeviceType=dev; input.m_ControlType=control->info.type; input.m_KeyCode=control->info.logical_key;
            copy(input.m_DeviceName,control->info.device_name); copy(input.m_ControlName,control->info.name);
            input.m_InputValue=std::bit_cast<uint32>(static_cast<std::int32_t>(std::clamp(value,-1e6f,1e6f))); events.push_back(input);
        }
    }
    float scaled(const Control& control) const {
        float result=control.value;
        if (control.range_min!=0 || control.range_max!=0) {
            const auto low=double(control.info.minimum),high=double(control.info.maximum);
            const auto center=(high-low)*.5+low+control.center_offset;
            const auto middle=(double(control.range_max)-control.range_min)*.5+control.range_min;
            const auto in_low=control.center_offset && result>center ? center : low;
            const auto in_high=control.center_offset && result<=center ? center : high;
            const auto out_low=control.center_offset && result>center ? middle : control.range_min;
            const auto out_high=control.center_offset && result<=center ? middle : control.range_max;
            result=float(out_low+(result-in_low)*(out_high-out_low)/(in_high-in_low));
        }
        return std::clamp(result*control.scale,-1e9f,1e9f);
    }
};
thread_local std::shared_ptr<ClientInput::State> ClientInput::current_;
ClientInput::ClientInput(std::vector<platform::InputControl> controls) : state_(std::make_shared<State>()) {
    if (controls.size()>1024) throw std::invalid_argument("too many native input controls");
    for (auto& info : controls) {
        if ((info.device!=1 && info.device!=2 && info.device!=4) || info.name.empty() || info.name.size()>=64 || info.device_name.size()>=100 || !std::isfinite(info.minimum) || !std::isfinite(info.maximum) || info.minimum>=info.maximum || state_->find(info.device,info.code)) throw std::invalid_argument("invalid native input control");
        Control control; control.info=std::move(info); state_->controls.push_back(std::move(control));
    }
}
ClientInput::~ClientInput()=default;
ClientInput::Scope::Scope(const ClientInput& service) : previous_(ClientInput::current_),current_(service.state_) { current_->thread(); ClientInput::current_=current_; }
ClientInput::Scope::~Scope() { ClientInput::current_=std::move(previous_); }
LTRESULT ClientInput::command(std::string_view name,std::span<const std::string_view> args) {
    return sdk([&]() -> LTRESULT {
        const auto self=state_; self->thread();
        if (same(name,"AddAction")) {
            if (args.size()!=2 || args[0].empty() || args[0].size()>=MAX_ACTIONNAME_LEN) return LT_INVALIDPARAMS;
            const auto code=integer(args[1]); if (code>255 || code < -65536) return LT_INVALIDPARAMS;
            if (!self->action(args[0]) && self->actions.size()>=1024) return LT_OUTOFMEMORY;
            std::string key(args[0]); if (const auto* existing=self->action(key)) key=existing->name;
            self->actions[key]=Action{key,code}; return LT_OK;
        }
        if (same(name,"EnableDevice")) { if (args.size()!=1) return LT_INVALIDPARAMS; const auto type=device(args[0]); if (!(type&self->available)) return LT_NOTFOUND; self->enabled|=type; return LT_OK; }
        if (args.size()<2) return LT_INVALIDPARAMS;
        auto* control=self->find(args[0],args[1]); if (!control) return LT_NOTFOUND;
        if (same(name,"rangebind")) {
            if (args.size()<5 || (args.size()-2)%3 || (args.size()-2)/3>32) return LT_INVALIDPARAMS;
            std::vector<Binding> bindings;
            for (std::size_t i=2;i<args.size();i+=3) {
                const auto low=number(args[i]),high=number(args[i+1]); const auto action=args[i+2];
                if (action.size()>256 || low>high) return LT_INVALIDPARAMS;
                if (!action.empty()) { if (action.front()!='*' && !self->action(action)) return LT_NOTFOUND; bindings.push_back({std::string(action),low,high}); }
            }
            control->bindings=std::move(bindings); self->clear(); return LT_OK;
        }
        if (same(name,"scale") || same(name,"rangescale")) {
            if (args.size()!=(same(name,"scale") ? 3u : 6u)) return LT_INVALIDPARAMS;
            const auto scale=std::clamp(number(args[2]),-500.0f,500.0f);
            if (args.size()==6) {
                const auto low=number(args[3]),high=number(args[4]),offset=number(args[5]);
                const double half=(double(control->info.maximum)-control->info.minimum)*.5;
                if (std::abs(low)>1e6f || std::abs(high)>1e6f || std::abs(double(offset))>=half) return LT_INVALIDPARAMS;
                control->range_min=low; control->range_max=high; control->center_offset=offset;
            }
            control->scale=scale; return LT_OK;
        }
        return LT_NOTFOUND;
    });
}
LTRESULT ClientInput::toggle(int code,bool enabled) { return sdk([&]() -> LTRESULT { state_->thread(); if (code<0 || code>255) return LT_INVALIDPARAMS; state_->toggle[code]=enabled; state_->latched[code]=false; state_->commands[code]=false; return LT_OK; }); }
std::uint32_t ClientInput::logical_key(unsigned binding) const { state_->thread(); const auto* control=state_->find(1,binding); return control ? control->info.logical_key : 0; }
InputFrame ClientInput::frame(std::span<const platform::InputEvent> events,std::optional<platform::GamepadState> pad) {
    const auto self=state_; self->thread(); if (events.size()>65536) throw std::invalid_argument("too many input events");
    self->commands.fill(false); self->axes.fill(0);
    for (auto& control : self->controls) { if (control.info.relative) control.value=0; control.press=false; control.pulse=false; }
    for (const auto& event : events) {
        using platform::InputType;
        if (event.type==InputType::focus) { self->focused=event.pressed; if (!self->focused) self->clear(); }
        if (!self->focused) continue;
        if (event.type==InputType::key && !event.repeat) self->event(1,unsigned(event.code),event.pressed ? 128 : 0);
        if (event.type==InputType::mouse_button) {
            unsigned button=event.code==1 ? 0 : event.code==3 ? 1 : event.code==2 ? 2 : event.code==4 ? 3 : event.code==5 ? 4 : 99;
            self->event(2,button,event.pressed ? 128 : 0);
        }
        if (event.type==InputType::mouse_motion) { self->event(2,256,event.x); self->event(2,257,event.y); }
        if (event.type==InputType::wheel) self->event(2,258,event.y*120);
    }
    self->available=3|(pad ? 4u : 0u);
    if (self->focused) {
        const std::array<float,6> axes=pad ? std::array<float,6>{pad->left_x,-pad->left_y,pad->right_trigger*2-1,pad->right_x,-pad->right_y,pad->left_trigger*2-1} : std::array<float,6>{};
        for (unsigned i=0;i<axes.size();++i) self->event(4,256+i,pad ? std::clamp(axes[i],-1.0f,1.0f)*32767 : 0);
        for (unsigned i=0;i<16;++i) self->event(4,i,pad && (pad->buttons&(1u<<i)) ? 128 : 0);
    }
    InputFrame result; std::array<bool,256> raw{},pulse{}; std::map<std::string,float> variables;
    for (const auto& [key,action] : self->actions) if (action.code < -3) variables[key]=0;
    if (self->input && self->focused && !self->tracking) for (const auto& control : self->controls) {
        if (!(control.info.device&self->enabled) || !(control.info.device&self->available)) continue;
        const auto value=self->scaled(control);
        for (const auto& binding : control.bindings) {
            if (binding.name.starts_with('*')) { if (control.press) result.console.push_back(binding.name.substr(1)); continue; }
            const auto* action=self->action(binding.name); if (!action) continue;
            const auto code=action->code;
            if (code<0) {
                if (control.info.type==CONTROLTYPE_KEY || control.info.type==CONTROLTYPE_BUTTON) continue;
                if (code>=-3) self->axes[std::size_t(-code-1)]=std::clamp(self->axes[std::size_t(-code-1)]+value,-1e9f,1e9f);
                else variables[action->name]=std::clamp(variables[action->name]+value,-1e9f,1e9f);
            } else {
                const bool unrestricted=binding.low==0 && binding.high==0;
                raw[code]=raw[code] || (unrestricted ? value!=0 : value>=binding.low && value<=binding.high);
                pulse[code]=pulse[code] || (unrestricted && control.pulse);
            }
        }
    }
    for (unsigned i=0;i<256;++i) {
        const bool down=raw[i] || pulse[i];
        if (self->toggle[i] && down && !self->raw_previous[i] && self->input && !self->tracking) self->latched[i]=!self->latched[i];
        self->commands[i]=self->input && self->focused && !self->tracking && (self->toggle[i] ? self->latched[i] : down);
        self->raw_previous[i]=raw[i];
        if (self->commands[i]!=self->previous[i]) (self->commands[i] ? result.pressed : result.released).push_back(i);
    }
    for (const auto& entry : variables) result.variables.push_back(entry);
    self->previous=self->commands;
    return result;
}
std::string ClientInput::config() const {
    const auto self=state_; self->thread(); std::string result;
    for (const auto& [key,action] : self->actions) result+="AddAction "+quote(action.name)+" "+std::to_string(action.code)+"\n";
    for (unsigned type : {1u,2u,4u}) if (self->enabled&type) {
        const std::string name=type==1 ? "Keyboard" : type==2 ? "Mouse" : "Joystick 1";
        result+="EnableDevice "+quote(name)+"\n";
        for (const auto& control : self->controls) if (control.info.device==type && !control.bindings.empty()) {
            const auto trigger=quote(name)+" "+quote(control.info.name); result+="rangebind "+trigger;
            for (const auto& bind : control.bindings) result+=" "+std::to_string(bind.low)+" "+std::to_string(bind.high)+" "+quote(bind.name);
            result+='\n';
            if (control.range_min || control.range_max || control.center_offset) result+="rangescale "+trigger+" "+std::to_string(control.scale)+" "+std::to_string(control.range_min)+" "+std::to_string(control.range_max)+" "+std::to_string(control.center_offset)+"\n";
            else if (control.scale!=1) result+="scale "+trigger+" "+std::to_string(control.scale)+"\n";
        }
    }
    return result;
}
const InputFunctions& ClientInput::functions() {
    static const InputFunctions functions=[] {
        InputFunctions api;
        api.GetAxisOffsets=+[](LTFLOAT* offsets) { if (!offsets) return; std::fill_n(offsets,3,0); sdk([&]() -> LTRESULT { const auto& self=State::active(); std::copy(self.axes.begin(),self.axes.end(),offsets); return LT_OK; }); };
        api.IsCommandOn=+[](int code) -> LTBOOL { LTBOOL result=LTFALSE; sdk([&]() -> LTRESULT { auto& self=State::active(); if (code>=0 && code<256) result=self.commands[code] ? LTTRUE : LTFALSE; return LT_OK; }); return result; };
        api.SetInputState=+[](LTBOOL enabled) { sdk([&]() -> LTRESULT { auto& self=State::active(); self.input=enabled!=0; self.clear(); return LT_OK; }); };
        api.ClearInput=+[]() -> LTRESULT { return sdk([&]() -> LTRESULT { State::active().clear(); return LT_OK; }); };
        api.GetDeviceName=+[](uint32 type,char* output,uint32 size) -> LTRESULT { if (output && size) output[0]=0; return sdk([&]() -> LTRESULT {
            auto& self=State::active(); if (!output || !size) return LT_INVALIDPARAMS; if (!(type&self.available)) return LT_NOTFOUND;
            for (const auto& control : self.controls) if (control.info.device==type) { const auto& name=control.info.device_name; if (size<=name.size()) return LT_INVALIDPARAMS; std::copy(name.begin(),name.end(),output); output[name.size()]=0; return LT_OK; }
            return LT_NOTFOUND;
        }); };
        api.IsDeviceEnabled=+[](char* name,LTBOOL* enabled) -> LTRESULT { if (enabled) *enabled=LTFALSE; return sdk([&]() -> LTRESULT { auto& self=State::active(); if (!name || !enabled) return LT_INVALIDPARAMS; *enabled=(device(name)&self.enabled&self.available) ? LTTRUE : LTFALSE; return LT_OK; }); };
        api.EnableDevice=+[](char* name) -> LTRESULT { return sdk([&]() -> LTRESULT { auto& self=State::active(); if (!name) return LT_INVALIDPARAMS; const auto type=device(name); if (!(type&self.available)) return LT_NOTFOUND; self.enabled|=type; return LT_OK; }); };
        api.StartDeviceTrack=+[](uint32 devices,uint32 capacity) -> LTRESULT { return sdk([&]() -> LTRESULT { auto& self=State::active(); if (!devices || (devices&~7u) || !capacity || capacity>MAX_INPUT_BUFFER_SIZE) return LT_INVALIDPARAMS; self.clear(); self.tracking=devices; self.capacity=capacity; return LT_OK; }); };
        api.EndDeviceTrack=+[]() -> LTRESULT { return sdk([&]() -> LTRESULT { auto& self=State::active(); self.tracking=0; self.clear(); return LT_OK; }); };
        api.TrackDevice=+[](DeviceInput* output,uint32* count) -> LTRESULT { const auto capacity=count ? *count : 0; if (count) *count=0; return sdk([&]() -> LTRESULT {
            auto& self=State::active(); if (!count || (capacity && !output)) return LT_INVALIDPARAMS; if (!self.tracking) return LT_NOTINITIALIZED;
            const bool overflow=self.overflow || self.events.size()>capacity; const auto size=std::min<std::size_t>(capacity,self.events.size());
            for (std::size_t i=0;i<size;++i) { output[i]=self.events.front(); self.events.pop_front(); } *count=static_cast<uint32>(size); self.overflow=false; return overflow ? LT_INPUTBUFFEROVERFLOW : LT_OK;
        }); };
        api.GetDeviceObjects=+[](uint32 types) -> DeviceObject* { DeviceObject* result=nullptr; sdk([&]() -> LTRESULT {
            auto& self=State::active(); if (self.object_lists.size()>=64) return LT_OUTOFMEMORY; ObjectList list; DeviceObject* previous=nullptr;
            for (const auto& control : self.controls) if (control.info.device&types&self.available) {
                auto row=std::make_unique<DeviceObject>(); const auto& info=control.info; row->m_DeviceType=info.device; row->m_ObjectType=info.type; row->m_RangeLow=info.minimum; row->m_RangeHigh=info.maximum; row->m_KeyCode=info.logical_key;
                copy(row->m_DeviceName,info.device_name); copy(row->m_ObjectName,info.name); copy(row->m_DisplayName,info.label); row->m_pNext=nullptr;
                if (previous) previous->m_pNext=row.get();
                previous=row.get(); list.rows.push_back(std::move(row));
            }
            if (!list.rows.empty()) { const auto head=list.rows.front().get(); self.object_lists.emplace(head,std::move(list)); result=head; } return LT_OK;
        }); return result; };
        api.FreeDeviceObjects=+[](DeviceObject* head) { sdk([&]() -> LTRESULT { State::active().object_lists.erase(head); return LT_OK; }); };
        api.GetDeviceBindings=+[](uint32 types) -> DeviceBinding* { DeviceBinding* result=nullptr; sdk([&]() -> LTRESULT {
            auto& self=State::active(); if (self.binding_lists.size()>=64) return LT_OUTOFMEMORY; BindingList list; DeviceBinding* previous=nullptr;
            for (const auto& control : self.controls) if ((control.info.device&types&self.available) && !control.bindings.empty()) {
                auto row=std::make_unique<DeviceBinding>(); copy(row->strDeviceName,control.info.device_name); copy(row->strTriggerName,control.info.name);
                row->nScale=control.scale; row->nRangeScaleMin=control.range_min; row->nRangeScaleMax=control.range_max; row->nRangeScalePreCenterOffset=control.center_offset;
                GameAction* tail=nullptr;
                for (const auto& binding : control.bindings) if (const auto* action=self.action(binding.name)) {
                    auto node=std::make_unique<GameAction>(); node->nActionCode=action->code; node->nRangeLow=binding.low; node->nRangeHigh=binding.high; copy(node->strActionName,action->name);
                    if (tail) tail->pNext=node.get(); else row->pActionHead=node.get(); tail=node.get(); list.actions.push_back(std::move(node));
                }
                if (previous) previous->pNext=row.get();
                previous=row.get(); list.rows.push_back(std::move(row));
            }
            if (!list.rows.empty()) { const auto head=list.rows.front().get(); self.binding_lists.emplace(head,std::move(list)); result=head; } return LT_OK;
        }); return result; };
        api.FreeDeviceBindings=+[](DeviceBinding* head) { sdk([&]() -> LTRESULT { State::active().binding_lists.erase(head); return LT_OK; }); };
        return api;
    }(); return functions;
}
void ClientInput::install(ILTClient& client) {
    const auto& api=functions();
#define NOLF_INPUT_API(name) client.name=api.name;
#include "client_input_members.inc"
#undef NOLF_INPUT_API
}
}
