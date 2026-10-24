#pragma once
#include "engine/portable/resources.h"
#include "platform/cursors.h"
#include "iltcursor.h"
#include "ltcodes.h"
#include <memory>

namespace nolf {
// The SDK owns logical instances; only CursorProvider owns native handles.
class ClientCursor final : public ILTCursor {
    struct State;
    std::unique_ptr<State> state_;
public:
    ClientCursor(std::shared_ptr<const Resources>,std::shared_ptr<platform::CursorProvider>);
    ~ClientCursor();
    ClientCursor(const ClientCursor&)=delete;
    ClientCursor& operator=(const ClientCursor&)=delete;
    LTRESULT SetCursorMode(CursorMode) override;
    LTRESULT GetCursorMode(CursorMode&) override;
    LTRESULT IsCursorModeAvailable(CursorMode) override;
    LTRESULT LoadCursorBitmapResource(const char*,HLTCURSOR&) override;
    LTRESULT LoadCursorBitmapResource(uint32,HLTCURSOR&) override;
    LTRESULT FreeCursor(HLTCURSOR) override;
    LTRESULT SetCursor(HLTCURSOR) override;
    LTRESULT IsValidCursor(HLTCURSOR) override;
    LTRESULT RefreshCursor() override;
private:
    LTRESULT load(std::string_view,HLTCURSOR&);
};
}
