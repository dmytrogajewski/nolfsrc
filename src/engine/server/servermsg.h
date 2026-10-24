#pragma once
#include "message.h"
#include "iltserver.h"
#include "commonphysics.h"
#include "servermodel.h"
#include "servercommon.h"
#include "serverlightanim.h"
#include "ilttransform.h"

class CLTServer : public ILTServer {
public:
    explicit CLTServer(CServerMgr*);
    ~CLTServer() override = default;
	LTRESULT GetNumClassProps(const HCLASS hClass, uint32 &count) override;
	LTRESULT GetClassProp(const HCLASS hClass, const uint32 iProp, ClassPropInfo &info) override;
	LTRESULT GetClassName(const HCLASS hClass, char *pName, uint32 maxNameBytes) override;
	LTRESULT GetClientPing(HCLIENT hClient, float &ping) override;
	LTRESULT GetNetFlags(HOBJECT hObj, uint32 &flags) override;
	LTRESULT SetNetFlags(HOBJECT hObj, uint32 flags) override;

	LTRESULT GetHPolyObject(const HPOLY hPoly, HOBJECT &hObject) override;
	LTRESULT FindNamedObjects(char *pName, BaseObjArray<HOBJECT> &objArray, uint32 *nTotalFound) override;
	LTRESULT FindWorldModelObjectIntersections(HOBJECT hWorldModel, LTVector vNewPos,
	                                           LTRotation rNewRot, BaseObjArray<HOBJECT> &objArray) override;

	LTRESULT GetWorldBox(LTVector &min, LTVector &max) override;
	LTRESULT ThreadLoadFile(char *pFilename, uint32 type) override;
	LTRESULT UnloadFile(char *pFilename, uint32 type) override;
	LTRESULT OpenFile(char *pFilename, ILTStream **pStream) override;

	LTRESULT ThreadLoadModel(char *pFilename);    // 0x1002ec9a
	LTRESULT ThreadLoadTexture(char *pFilename);  // 0x1002ed9c
	LTRESULT UnloadModel(char *pFilename);        // 0x1002ee11
	LTRESULT UnloadTexture(char *pFilename);      // 0x1002ee9d

	LTRESULT GetClientData(HCLIENT hClient, void *&pData, uint32 &nLength) override;
	LTRESULT SetObjectFilenames(HOBJECT hObj, char *pFilename, char *pSkinName) override;
	LTRESULT GetObjectName(HOBJECT hObject, char *pName, uint32 nameBufSize) override;
	LTRESULT GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags) override;
	LTRESULT GetModelAnimUserDims(HOBJECT hObject, LTVector *pDims, HMODELANIM hAnim) override;

	LTRESULT EndMessage2(HMESSAGEWRITE hMessage, uint32 flags) override;
	LTRESULT EndMessage(HMESSAGEWRITE hMessage) override;
	LTRESULT SetObjectSFXMessage(HOBJECT hObject, ILTMessage &msg) override;
	LTRESULT SendToObject(ILTMessage &msg, uint32 msgID, HOBJECT hSender,
	                      HOBJECT hSendTo, uint32 flags) override;
	LTRESULT SendToServer(ILTMessage &msg, uint32 msgID, HOBJECT hSender, uint32 flags) override;
	LTRESULT SendToClient(ILTMessage &msg, uint8 msgID, HCLIENT hSendTo, uint32 flags) override;
	LTRESULT SendSFXMessage(ILTMessage &msg, LTVector &pos, uint32 flags) override;

	void BroadcastFileMessage(uint32 nFileType, uint32 nMsgID, uint16 nFileID, int bToAll);

	LTRESULT CopyFile(const char *pszSourceFile, const char *pszDestFile) override;
	void SetModelAnimation(HOBJECT hObj, HMODELANIM hAnim) override;
	HMODELANIM GetModelAnimation(HOBJECT hObj) override;
	void SetModelLooping(HOBJECT hObj, LTBOOL bLoop) override;
	LTBOOL GetModelLooping(HOBJECT hObj) override;
	LTRESULT ResetModelAnimation(HOBJECT hObj) override;
	uint32 GetModelPlaybackState(HOBJECT hObj) override;
	HMODELANIM GetAnimIndex(HOBJECT hObj, char *pAnimName) override;
	void CPrint(char *pMsg, ...) override;
	uint32 GetPointContainers(LTVector *pPoint, HOBJECT *pList, uint32 maxListSize) override;
	LTBOOL GetContainerCode(HOBJECT hObj, uint16 *pCode) override;
	HSTRING FormatString(int messageCode, ...) override;

	HMESSAGEWRITE StartSpecialEffectMessage(LPBASECLASS pObject) override;
	HMESSAGEWRITE StartInstantSpecialEffectMessage(LTVector* pPos) override;
	HMESSAGEWRITE StartMessageToObject(LPBASECLASS pSender, HOBJECT hSendTo, uint32 messageID) override;
	LTRESULT      StartMessageToServer(LPBASECLASS pSender, uint32 messageID, HMESSAGEWRITE* hWrite) override;
	HMESSAGEWRITE StartMessage(HCLIENT hSendTo, uint8 messageID) override;

	LTRESULT GetGlobalForce(LTVector* pVec) override;
	LTRESULT SetGlobalForce(LTVector* pVec) override;

	LTRESULT SetFrictionCoefficient(HOBJECT hObj, float coeff) override;
	float    GetForceIgnoreLimit(HOBJECT hObj, float &limit) override;
	void     SetForceIgnoreLimit(HOBJECT hObj, float limit) override;
	LTRESULT GetVelocity(HOBJECT hObj, LTVector *pVel) override;
	LTRESULT SetVelocity(HOBJECT hObj, LTVector *pVel) override;
	LTRESULT GetAcceleration(HOBJECT hObj, LTVector *pAccel) override;
	LTRESULT SetAcceleration(HOBJECT hObj, LTVector *pAccel) override;
	float    GetObjectMass(HOBJECT hObj) override;
	void     SetObjectMass(HOBJECT hObj, float mass) override;
	void     GetObjectDims(HOBJECT hObj, LTVector *pNewDims) override;
	LTRESULT SetObjectDims(HOBJECT hObj, LTVector *pNewDims) override;
	LTRESULT SetObjectDims2(HOBJECT hObj, LTVector *pNewDims) override;
	LTRESULT MoveObject(HOBJECT hObj, LTVector *pNewPos) override;
	LTRESULT GetStandingOn(HOBJECT hObj, CollisionInfo *pInfo) override;
	uint32   GetObjectFlags(HOBJECT hObj) override;
	void     SetObjectFlags(HOBJECT hObj, uint32 flags) override;
	char*    GetObjectName(HOBJECT hObject) override;

	HSTRING  CopyString(HSTRING hString) override;
	HSTRING  CreateString(char *pString) override;
	void     FreeString(HSTRING hString) override;
	LTBOOL   CompareStrings(HSTRING hString1, HSTRING hString2) override;
	LTBOOL   CompareStringsUpper(HSTRING hString1, HSTRING hString2) override;
	char*    GetStringData(HSTRING hString) override;
	float    GetVarValueFloat(HCONSOLEVAR hVar) override;
	char*    GetVarValueString(HCONSOLEVAR hVar) override;
	float    GetTime() override;
	float    GetFrameTime() override;
	LTRESULT RemoveObject(HOBJECT hObj) override;
	LTRESULT FreeUnusedModels() override;
	LTRESULT SendTo(const void *pData, uint32 len, const char *sAddr, uint32 port) override;

    HMESSAGEWRITE StartHMessageWrite() override;
    LTRESULT SetModelFilenames(HOBJECT, char*, char*) override;

    CServerMgr* m_pServerMgr;
    ILTTransform m_TransformLT;
    ServerModelLT m_ModelLT;
    ServerCommonLT m_CommonLT;
    SPhysicsLT m_PhysicsLT;
    ServerLightAnimLT m_LightAnimLT;
};
