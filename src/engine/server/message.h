#pragma once
#include "message_fwd.h"
#include "iltmessage.h"
#include "ltlink.h"
#include "packet.h"
#include "stringbank.h"
#include "position_reference.h"
#include <atomic>
#include <memory>
#include <utility>

class ServerMessage;
class CMsgObjectIO;
class LMessageImpl : public ILTMessage {
public:
    void SetObjectIO(std::shared_ptr<CMsgObjectIO> context) {
        m_pObjectIO=context.get(); owned_object_io_=std::move(context);
    }
    CMsgObjectIO* m_pObjectIO = nullptr;
    ServerMessage* m_pOwner = nullptr;
    uint32 m_Type = 0;
    HCLIENT m_hClient = nullptr;
    HOBJECT m_hSender = nullptr;
    HOBJECT m_hSendTo = nullptr;
    uint32 m_MessageID = 0;
    LTVector m_vPos{0, 0, 0};

    LTRESULT Release() override;
    LTRESULT ReadByteFL(uint8&) override;
    LTRESULT ReadWordFL(uint16&) override;
    LTRESULT ReadDWordFL(uint32&) override;
    LTRESULT ReadFloatFL(float&) override;
    LTRESULT ReadStringFL(char*, uint32) override;
    LTRESULT ReadHStringFL(HSTRING&) override;
    LTRESULT ReadHStringAsStringFL(char*, uint32) override;
    LTRESULT ReadRawFL(void*, uint32) override;
    LTRESULT ReadVectorFL(LTVector&) override;
    LTRESULT ReadCompVectorFL(LTVector&) override;
    LTRESULT ReadCompPosFL(LTVector&) override;
    LTRESULT ReadRotationFL(LTRotation&) override;
    LTRESULT ReadCompRotationFL(LTRotation&) override;
    LTRESULT ReadMessageFL(ILTMessage*&) override;
    LTRESULT ReadObjectFL(HOBJECT&) override;
    LTRESULT WriteByte(uint8) override;
    LTRESULT WriteWord(uint16) override;
    LTRESULT WriteDWord(uint32) override;
    LTRESULT WriteFloat(float) override;
    LTRESULT WriteString(char*) override;
    LTRESULT WriteVector(LTVector&) override;
    LTRESULT WriteCompVector(LTVector&) override;
    LTRESULT WriteCompPos(LTVector&) override;
    LTRESULT WriteRotation(LTRotation&) override;
    LTRESULT WriteCompRotation(LTRotation&) override;
    LTRESULT WriteRaw(void*, uint32) override;
    LTRESULT WriteMessage(ILTMessage&) override;
    LTRESULT WriteHString(HSTRING) override;
    LTRESULT WriteHStringFormatted(int, ...) override;
    LTRESULT WriteHStringArgList(int, va_list*) override;
    LTRESULT WriteStringAsHString(char*) override;
    LTRESULT WriteObject(HOBJECT) override;
    LTRESULT ResetPos() override;
    LTRESULT GetStatus(uint32&) override;

    // Internal callers use the same object as the SDK, without vtable views.
    virtual bool Ready() const { return m_pOwner != nullptr; }
    virtual LTRESULT Verify() const;
    virtual LTRESULT FlagError(uint32 status, LTRESULT result);
    LTRESULT ReadStatus();
    void EndWrite() { Release(); }
    void EndRead() { Release(); }
    void ResetRead() { ResetPos(); }
    LTRESULT ReadByte(uint8* p) { return p ? ReadByteFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadWord(uint16* p) { return p ? ReadWordFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadDWord(uint32* p) { return p ? ReadDWordFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadFloat(float* p) { return p ? ReadFloatFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadVector(LTVector* p) { return p ? ReadVectorFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadCompVector(LTVector* p) { return p ? ReadCompVectorFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadCompPos(LTVector* p) { return p ? ReadCompPosFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadRotation(LTRotation* p) { return p ? ReadRotationFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadHString(HSTRING* p) { return p ? ReadHStringFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadObject(HOBJECT* p) { return p ? ReadObjectFL(*p) : LT_INVALIDPARAMS; }
    LTRESULT ReadMessage(void** p);
    LTRESULT ReadMessageFL(LMessageImpl*& p);
    LTRESULT WriteMessage(LMessageImpl* p);
    LTRESULT WriteMessageHandle(void* p) { return WriteMessage(static_cast<LMessageImpl*>(p)); }
    LTRESULT WriteVector(LTVector* p) { return p ? WriteVector(*p) : LT_INVALIDPARAMS; }
    LTRESULT WriteCompVector(LTVector* p) { return p ? WriteCompVector(*p) : LT_INVALIDPARAMS; }
    LTRESULT WriteCompPos(LTVector* p) { return p ? WriteCompPos(*p) : LT_INVALIDPARAMS; }
    LTRESULT WriteRotation(LTRotation* p) { return p ? WriteRotation(*p) : LT_INVALIDPARAMS; }
private:
    // Retained/nested native messages can outlive the creating engine.
    // Legacy borrowed contexts still use m_pObjectIO directly.
    std::shared_ptr<CMsgObjectIO> owned_object_io_;
};

class CMsgObjectIO {
public:
    virtual ~CMsgObjectIO() = default;
    virtual LTRESULT ReadObject(LMessageImpl*, HOBJECT*) = 0;
    virtual LTRESULT WriteObject(LMessageImpl*, HOBJECT) = 0;
    virtual LTRESULT WriteFormattedHString(LMessageImpl*, int, va_list*) = 0;
    virtual void GetPosRef(CPosRef**) = 0;
};
class CAbstractIO {
public:
    virtual ~CAbstractIO() = default;
    virtual void AddRef();
    virtual void Release();
    virtual void _v0c();
    std::atomic<uint32> m_nRefCount{0};
};
class ServerMessage : public CAbstractIO, public CPacket {
public:
    ServerMessage();
    ServerMessage(const ServerMessage&) = delete;
    ServerMessage& operator=(const ServerMessage&) = delete;
    void Init(uint16 maxLen, uint16 growSize);
    bool ReadData(void*, uint32);
    bool WriteData(const void*, uint32);
    void WriteByte(uint8 value) { WriteType(value); }
    void WriteWord(uint16 value) { WriteType(value); }
    void WriteDWord(uint32 value) { WriteType(value); }
    void WriteFloat(float value) { WriteType(value); }
    void WriteString(char* text) { m_Write.WriteString(text); }
    uint8 ReadByte(uint8) { return ReadType(static_cast<uint8*>(nullptr)); }
    uint16 ReadWord(uint16*) { return ReadType(static_cast<uint16*>(nullptr)); }
    uint32 ReadDWord(uint32*) { return ReadType(static_cast<uint32*>(nullptr)); }
    uint8 ReadByteFromEnd(uint8);
    char* ReadStringInPlace();

    LMessageImpl m_Write;
    class NetConnection* m_pConnection = nullptr;
    LTLink m_BankLink;
    // Transitional source names refer to typed storage, never byte offsets.
    uint16& m_Unknown34 = m_DataLen;
    uint16& m_Unknown38 = m_Pos;
    uint16 m_Unknown3a = 1;
    uint32& m_MsgFlags = m_Flags;
    CMoArray<uint8>& m_PacketData = m_Data;
    uint8*& m_pData = m_Data.m_pArray;
    uint32& m_DataSize = m_Data.m_nElements;
};

void* msg_AllocPacket(CAbstractIO**, uint32 blockSize, uint32 growSize);
void lmsg_AfterRead(LMessageImpl*);
void lmsg_WriteCompVector(LMessageImpl*, LTVector*);
void lmsg_WriteCompRotation(LMessageImpl*, LTRotation*);
void lmsg_ReadCompRotation(LMessageImpl*, LTRotation*);
void lmsg_WriteCompPos(LMessageImpl*, LTVector*, CPosRef*);
void lmsg_ReadCompPos(LMessageImpl*, LTVector*, CPosRef*);

#define MSGFLAG_READERROR PACKETFLAG_READOVERFLOW
#define MT_TOOBJECT 0
#define MT_TOCLIENT 1
#define MT_SFX 2
#define MT_INSTANTSFX 3
#define MT_TOSERVER 4

class CMessageRef {
public:
    CMessageRef() = default;
    CMessageRef(const CMessageRef&) = delete;
    CMessageRef& operator=(const CMessageRef&) = delete;
    ~CMessageRef() { if (m_pMsg) m_pMsg->Release(); }
    ServerMessage* Init(void* packet) {
        auto* next = packet ? static_cast<ServerMessage*>(*static_cast<CAbstractIO**>(packet)) : nullptr;
        return Assign(next);
    }
    ServerMessage* Assign(ServerMessage* next) {
        if (next) next->AddRef();
        if (m_pMsg) m_pMsg->Release();
        return m_pMsg = next;
    }
    LTRESULT CopyFrom(const ServerMessage&, uint32 offset = 0);
    ServerMessage* m_pMsg = nullptr;
};
class CMsgCursor {
public:
    CMsgCursor() = default;
    ~CMsgCursor() { if (m_pMsg) m_pMsg->Release(); }
    ServerMessage* Init(ServerMessage*);
    ServerMessage* m_pMsg = nullptr;
};
