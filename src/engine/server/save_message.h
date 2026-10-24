#pragma once
#include "message.h"
#include "iltstream.h"

enum class SaveMessageDirection { Read, Write };
// A borrowed SDK message for one object's game data. The caller owns the
// bounded stream and object-reference adapter throughout the game callback.
// Shared message value codecs call these guarded raw operations virtually.
class SaveMessage final : public LMessageImpl {
public:
    SaveMessage(ILTStream&, SaveMessageDirection, CMsgObjectIO* = nullptr);
    bool Ready() const override { return true; }
    LTRESULT Verify() const override;
    LTRESULT FlagError(uint32, LTRESULT) override;
    LTRESULT Release() override { return LT_OK; }
    LTRESULT ResetPos() override { return LT_OK; }
    LTRESULT GetStatus(uint32&) override;
    LTRESULT ReadRawFL(void*, uint32) override;
    LTRESULT WriteRaw(void*, uint32) override;
    LTRESULT WriteString(char*) override;
    LTRESULT ReadStringFL(char*, uint32) override;
private:
    ILTStream& stream_;
    SaveMessageDirection direction_;
    uint32 status_ = 0;
};
