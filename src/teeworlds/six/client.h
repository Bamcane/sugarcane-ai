/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef TEEWORLDS_SIX_CLIENT_H
#define TEEWORLDS_SIX_CLIENT_H

#include "iclient.h"
#include "system.h"
#include "protocol.h"
#include "generated_protocol.h"
#include "network.h"
#include "engine.h"
#include "snapshot.h"
#include "tune.h"

class CSmoothTime
{
	int64 m_Snap;
	int64 m_Current;
	int64 m_Target;

	int m_SpikeCounter;

	float m_aAdjustSpeed[2]; // 0 = down, 1 = up
public:
	void Init(int64 Target);
	void SetAdjustSpeed(int Direction, float Value);

	int64 Get(int64 Now);

	void UpdateInt(int64 Target);
	void Update(int64 Target, int TimeLeft, int AdjustDirection);
};

class CClient : public IClient
{
	friend class ISugarcane;
	class ISugarcane *m_pSugarcane;
	// needed interfaces
	IEngine *m_pEngine;

	enum
	{
		NUM_SNAPSHOT_TYPES=2,
		PREDICTION_MARGIN=1000/50/2, // magic network prediction value
	};

	class CNetClient m_NetClient[2];
	
	char m_aServerAddressStr[256];

	char m_aMapDownloadName[256];
	int m_MapDownloadCrc;
	int m_MapDownloadChunk;

	unsigned m_SnapshotParts;
	int64 m_LocalStartTime;

	NETADDR m_ServerAddress;
	int m_SnapCrcErrors;

	int m_AckGameTick;
	int m_CurrentRecvTick;
	
	// version-checking
	char m_aVersionStr[10];

	// pinging
	int64 m_PingStartTime;

	//
	char m_aCurrentMap[256];
	unsigned m_CurrentMapCrc;
	
	// time
	CSmoothTime m_GameTime;
	CSmoothTime m_PredictedTime;
	
	// input
	struct // TODO: handle input better
	{
		int m_aData[MAX_INPUT_SIZE]; // the input data
		int m_Tick; // the tick that the input is for
		int64 m_PredictedTime; // prediction latency when we sent this input
		int64 m_Time;
	} m_aInputs[200];

	int m_CurrentInput;
	
	// the game snapshots are modifiable by the game
	class CSnapshotStorage m_SnapshotStorage;
	CSnapshotStorage::CHolder *m_Snapshots[NUM_SNAPSHOT_TYPES];

	int m_ReceivedSnapshots;
	char m_aSnapshotIncomingData[CSnapshot::MAX_SIZE];

	class CSnapshotStorage::CHolder m_aDemorecSnapshotHolders[NUM_SNAPSHOT_TYPES];
	char *m_aDemorecSnapshotData[NUM_SNAPSHOT_TYPES][CSnapshot::MAX_SIZE];

	class CSnapshotDelta m_SnapshotDelta;

	CNetObjHandler m_NetObjHandler;

	CTuningParams m_Tuning;
public:
	IEngine *Engine() { return m_pEngine; }
	CTuningParams *Tuning() { return &m_Tuning; }

	CClient();

	// ----- send functions -----
	virtual int SendMsg(CMsgPacker *pMsg, int Flags);

	//
	char m_aCmdConnect[256];
	virtual void Connect(const char *pAddress);
	virtual void DisconnectWithReason(const char *pReason);
	virtual void Disconnect();

	int SendMsgEx(CMsgPacker *pMsg, int Flags, bool System=true);
	void SendInfo();
	void SendEnterGame();
	void SendReady();
	void SendInput();

	// ------ state handling -----
	void SetState(int s);

	// called when the map is loaded and we should init for a new round
	void OnEnterGame();
	virtual void EnterGame();

	// ---

	void *SnapGetItem(int SnapID, int Index, CSnapItem *pItem);
	void SnapInvalidateItem(int SnapID, int Index);
	void *SnapFindItem(int SnapID, int Type, int ID);
	int SnapNumItems(int SnapID);
	void SnapSetStaticsize(int ItemType, int Size);

	virtual const char *ErrorString();
	const char *LatestVersion();
	virtual bool ConnectionProblems();

	void ProcessConnlessPacket(CNetChunk *pPacket);
	void ProcessServerPacket(CNetChunk *pPacket);

	void RegisterInterfaces();
	void InitInterfaces();

	void PumpNetwork();

	void Update();
	void Run();

	void OnNewSnapshot();
	void Rcon(const char *pCmd);

	void SetSugarcane(class ISugarcane *pSugarcane);

	void NeedDisconnect();
};

#endif // TEEWORLDS_SIX_CLIENT_H