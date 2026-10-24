#pragma once
#include "platform/window.h"

class ILTClient;
namespace nolf {
// The native engine supplies its owned service through the SDK hook table.
// Callers retain the engine/game callback lease for the duration of a call.
platform::Window& client_window(ILTClient&);
}
