// Client display policy. Native window operations belong to the platform service.
#include "Stdafx.h"
#include "ClientUtilities.h"
#include "GameClientShell.h"
#include "InterfaceMgr.h"
#include "cshell/p1_modern.h"
#include "engine/client/client_window.h"
#include <stdexcept>

namespace {
bool want_desktop = false;
uint32 tried_width = 0, tried_height = 0;
}
int P1Window_GetMode()
{
    if (GetConsoleInt("Windowed",0) != 1) return P1_WINDOW_FULLSCREEN;
    return GetConsoleInt("WindowMode",P1_WINDOW_WINDOWED) == P1_WINDOW_BORDERLESS ? P1_WINDOW_BORDERLESS : P1_WINDOW_WINDOWED;
}
void P1Window_SetMode(int mode)
{
    if (mode < P1_WINDOW_FULLSCREEN || mode > P1_WINDOW_BORDERLESS) throw std::invalid_argument("invalid client window mode");
    WriteConsoleInt("Windowed",mode != P1_WINDOW_FULLSCREEN ? 1 : 0);
    WriteConsoleInt("WindowMode",mode);
}
void P1Window_DesktopSize(int* width,int* height)
{
    if (!width || !height) return;
    const auto mode = nolf::client_window(*g_pLTClient).desktop_mode();
    *width=mode.width; *height=mode.height;
}
void P1Window_Update()
{
    if (!want_desktop) return;
    want_desktop=false;
    RMode mode;
    if (g_pLTClient->GetRenderMode(&mode) != LT_OK) return;
    int width=0,height=0;
    P1Window_DesktopSize(&width,&height);
    if (mode.m_Width == tried_width && mode.m_Height == tried_height) return;
    tried_width=mode.m_Width; tried_height=mode.m_Height;
    mode.m_Width=uint32(width); mode.m_Height=uint32(height);
    g_pInterfaceMgr->SetSwitchingRenderModes(LTTRUE);
    const auto result=g_pLTClient->SetRenderMode(&mode);
    g_pInterfaceMgr->SetSwitchingRenderModes(LTFALSE);
    if (result != LT_OK) return;
    g_pInterfaceMgr->ScreenDimsChanged();
    WriteConsoleInt("ScreenWidth",width); WriteConsoleInt("ScreenHeight",height);
}
void P1Window_Apply()
{
    RMode mode;
    if (g_pLTClient->GetRenderMode(&mode) != LT_OK || !mode.m_Width || !mode.m_Height) return;
    auto& window=nolf::client_window(*g_pLTClient);
    const auto placement=P1Window_GetMode();
    window.window_mode(static_cast<nolf::platform::WindowMode>(placement),int(mode.m_Width),int(mode.m_Height));
    want_desktop=false;
    if (placement == P1_WINDOW_BORDERLESS) {
        const auto desktop=window.desktop_mode();
        want_desktop=mode.m_Width != uint32(desktop.width) || mode.m_Height != uint32(desktop.height);
    } else tried_width=tried_height=0;
}
void P1_OnClientEvent(uint32 event)
{
    switch (event) {
        case LTEVENT_RENDERINIT:
            P1Window_Apply();
            if (g_pGameClientShell) g_pGameClientShell->GetScreenTintMgr()->P1Invalidate();
            break;
        case LTEVENT_GAINEDFOCUS: P1Window_Apply(); P1Input_OnFocus(true); break;
        case LTEVENT_LOSTFOCUS: P1Input_OnFocus(false); break;
    }
}
