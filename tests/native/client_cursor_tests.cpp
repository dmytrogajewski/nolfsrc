#include "engine/client/client_cursor.h"
#include "platform/desktop.h"
#include "cursor_fixture.h"
#include <future>
#include <iostream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
void checks(const char* data) {
    nolf::platform::DesktopConfig config; config.width=config.height=128; config.vsync=false;
    auto desktop=std::make_unique<nolf::platform::Desktop>(config); auto provider=desktop->cursor_service();
    std::cout<<"desktop="<<desktop->driver()<<'\n';
    require(provider==desktop->cursor_service() && provider->available(),"native cursor service not retained/available");
    cursor_fixture::Files fixture; auto resources=std::make_shared<nolf::Resources>(fixture.root);
    nolf::ClientCursor cursor(resources,provider); CursorMode mode=CM_Hardware;
    require(cursor.GetCursorMode(mode)==LT_OK && mode==CM_None && cursor.IsCursorModeAvailable(CM_Hardware)==LT_YES,"native cursor initial mode/capability differs");
    HLTCURSOR handle=nullptr; require(cursor.LoadCursorBitmapResource(uint32(104),handle)==LT_OK && handle && handle->IsValid()==LT_YES && handle->GetData(),"native numeric cursor resource failed");
    require(cursor.SetCursor(handle)==LT_OK && cursor.SetCursorMode(CM_Hardware)==LT_OK && cursor.RefreshCursor()==LT_OK,"native cursor select/show/refresh failed");
    int token=1; handle->SetData(&token); require(handle->GetData()==&token && cursor.SetCursor(handle)==LT_OK,"cursor opaque SDK data interpreted as native handle");
    handle->SetData(nullptr); require(handle->IsValid()==LT_NO && cursor.IsValidCursor(handle)==LT_NO && cursor.SetCursor(handle)==LT_INVALIDPARAMS,"cleared cursor data still valid"); handle->SetData(&token);
    const auto foreign=reinterpret_cast<HLTCURSOR>(1); require(cursor.IsValidCursor(foreign)==LT_NO && cursor.SetCursor(foreign)==LT_INVALIDPARAMS && cursor.FreeCursor(foreign)==LT_INVALIDPARAMS,"foreign cursor handle dereferenced");
    { nolf::ClientCursor other(resources,provider); require(other.IsValidCursor(handle)==LT_NO && other.FreeCursor(handle)==LT_INVALIDPARAMS,"foreign client cursor accepted"); }
    require(cursor.RefreshCursor()==LT_OK,"cursor did not refresh after service switch");
    require(std::async(std::launch::async,[&] { return cursor.SetCursor(handle)==LT_NOTINITIALIZED && handle->IsValid()==LT_NO; }).get(),"cursor crossed owner thread");
    require(cursor.FreeCursor(handle)==LT_OK && handle->IsValid()==LT_NO && !handle->GetData() && cursor.FreeCursor(handle)==LT_INVALIDPARAMS,"cursor retirement left live native state");
    HLTCURSOR next=nullptr; require(cursor.LoadCursorBitmapResource("pointer",next)==LT_OK && next!=handle,"cursor name load or non-reuse failed"); require(cursor.FreeCursor(next)==LT_OK,"named cursor release failed");
    require(cursor.LoadCursorBitmapResource("#104",next)==LT_OK && cursor.FreeCursor(next)==LT_OK,"numeric string cursor resource failed");
    require(cursor.LoadCursorBitmapResource("../104",next)==LT_INVALIDPARAMS && !next && cursor.LoadCursorBitmapResource(uint32(999),next)==LT_MISSINGCURSORRESOURCE && !next,"invalid/missing resource succeeded");
    if (data) {
        auto legal=std::make_shared<nolf::Resources>(data); nolf::ClientCursor original(legal,provider);
        require(original.LoadCursorBitmapResource(uint32(104),next)==LT_OK && original.SetCursor(next)==LT_OK && original.SetCursorMode(CM_Hardware)==LT_OK,"original pointer resource alternatives not supported");
        require(original.FreeCursor(next)==LT_OK,"original pointer resource release failed"); std::cout<<"Original IDC_POINTER=104 selected through native SDL adapter\n";
    }
    require(cursor.LoadCursorBitmapResource(uint32(104),next)==LT_OK && cursor.SetCursor(next)==LT_OK,"cursor lifetime fixture failed");
    desktop.reset(); require(cursor.RefreshCursor()==LT_OK && cursor.FreeCursor(next)==LT_OK && cursor.SetCursorMode(CM_None)==LT_OK,"cursor/video lease did not survive window retirement");
}
}
int main(int argc,char** argv) { try { checks(argc>1 ? argv[1] : nullptr); std::cout<<"Native SDL/SDK cursor modes, resources, handles, selection, ownership and video lifetime passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
