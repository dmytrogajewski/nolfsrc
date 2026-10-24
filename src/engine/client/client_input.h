#pragma once
#include "platform/input.h"
#include "platform/window.h"
#include "iltclient.h"
#include <array>
#include <memory>
#include <span>

namespace nolf {
struct InputFunctions {
#define NOLF_INPUT_API(name) decltype(ILTClient::name) name=nullptr;
#include "client_input_members.inc"
#undef NOLF_INPUT_API
};
struct InputFrame {
    std::vector<unsigned> pressed,released;
    std::vector<std::string> console;
    std::vector<std::pair<std::string,float>> variables;
};
class ClientInput {
    struct State;
    std::shared_ptr<State> state_;
    static thread_local std::shared_ptr<State> current_;
public:
    explicit ClientInput(std::vector<platform::InputControl>);
    ~ClientInput();
    ClientInput(const ClientInput&)=delete;
    ClientInput& operator=(const ClientInput&)=delete;
    class Scope {
        std::shared_ptr<State> previous_,current_;
    public:
        explicit Scope(const ClientInput&);
        ~Scope();
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    };
    Scope activate() const { return Scope(*this); }
    static const InputFunctions& functions();
    static void install(ILTClient&);
    // Receives normalized events; returned callbacks are owned values that the
    // host dispatches after the input transaction completes.
    InputFrame frame(std::span<const platform::InputEvent>,std::optional<platform::GamepadState> = {});
    LTRESULT command(std::string_view,std::span<const std::string_view>);
    LTRESULT toggle(int,bool);
    std::string config() const;
    std::uint32_t logical_key(unsigned binding) const;
};
}
