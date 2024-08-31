/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef TEEWORLDS_SIX_ICLIENT_H
#define TEEWORLDS_SIX_ICLIENT_H
#include "kernel.h"

#include "message.h"

class IClient : public IInterface
{
	MACRO_INTERFACE("client", 0)
protected:
	// quick access to state of the client
	int m_State;

	// quick access to time variables
	int m_PrevGameTick;
	int m_CurGameTick;
	float m_GameIntraTick;
	float m_GameTickTime;

	int m_PredTick;
	float m_PredIntraTick;

	float m_LocalTime;
	float m_RenderFrameTime;

	int m_GameTickSpeed;
public:
	int m_LocalID;

	bool m_DummySendConnInfo;

	class CSnapItem
	{
	public:
		int m_Type;
		int m_ID;
		int m_DataSize;
	};

	/* Constants: Client States
		STATE_OFFLINE - The client is offline.
		STATE_CONNECTING - The client is trying to connect to a server.
		STATE_LOADING - The client has connected to a server and is loading resources.
		STATE_ONLINE - The client is connected to a server and running the game.
		STATE_DEMOPLAYBACK - The client is playing a demo
		STATE_QUITING - The client is quiting.
	*/

	enum
	{
		STATE_OFFLINE=0,
		STATE_CONNECTING,
		STATE_LOADING,
		STATE_ONLINE,
		STATE_DEMOPLAYBACK,
		STATE_QUITING,
	};

	//
	inline int State() const { return m_State; }

	// tick time access
	inline int PrevGameTick() const { return m_PrevGameTick; }
	inline int GameTick() const { return m_CurGameTick; }
	inline int PredGameTick() const { return m_PredTick; }
	inline float IntraGameTick() const { return m_GameIntraTick; }
	inline float PredIntraGameTick() const { return m_PredIntraTick; }
	inline float GameTickTime() const { return m_GameTickTime; }
	inline int GameTickSpeed() const { return m_GameTickSpeed; }

	// other time access
	inline float RenderFrameTime() const { return m_RenderFrameTime; }
	inline float LocalTime() const { return m_LocalTime; }

	// actions
	virtual void Connect(const char *pAddress) = 0;
	virtual void Disconnect() = 0;

	// networking
	virtual void EnterGame() = 0;

	// snapshot interface
	enum
	{
		SNAP_CURRENT=0,
		SNAP_PREV=1
	};

	// TODO: Refactor: should redo this a bit i think, too many virtual calls
	virtual int SnapNumItems(int SnapID) = 0;
	virtual void *SnapFindItem(int SnapID, int Type, int ID) = 0;
	virtual void *SnapGetItem(int SnapID, int Index, CSnapItem *pItem) = 0;
	virtual void SnapInvalidateItem(int SnapID, int Index) = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags) = 0;

	template<class T>
	int SendPackMsg(T *pMsg, int Flags)
	{
		CMsgPacker Packer(pMsg->MsgID());
		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags);
	}

	//
	virtual const char *ErrorString() = 0;
	virtual const char *LatestVersion() = 0;
	virtual bool ConnectionProblems() = 0;
};

#endif
