// Mouse capture is a window service; the engine provides its normalized axes.
#include "Stdafx.h"
#include "cshell/p1_modern.h"
#include "engine/client/client_window.h"
#include "platform/logging.h"

namespace {
bool captured=false, focused=true;
}
void P1Input_UseCursor(bool use_cursor)
{
    auto& window=nolf::client_window(*g_pLTClient);
    focused=window.focused();
    const auto take_input=!use_cursor;
    window.relative_mouse(take_input && window.focused());
    g_pLTClient->RunConsoleString("CursorCenter 0");
    captured=take_input;
}
void P1Input_GetAxisOffsets(float* offsets)
{
    if (!offsets) return;
    offsets[0]=offsets[1]=offsets[2]=0;
    if (!captured || !focused) return;
    g_pLTClient->GetAxisOffsets(offsets);
    if (!P1Config_OldMouseLook()) { offsets[0]*=.5f; offsets[1]*=.5f; }
}
void P1Input_OnWindowUnhooked()
{
    captured=false; focused=false;
    try { nolf::client_window(*g_pLTClient).relative_mouse(false); }
    catch (const std::exception& error) { nolf::platform::log("client input",error.what()); }
}
void P1Input_OnFocus(bool focus)
{
    nolf::client_window(*g_pLTClient).relative_mouse(captured && focus);
    focused=focus;
}
