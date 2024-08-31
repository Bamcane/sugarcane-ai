#include "client.h"
#include "generated_protocol.h"
#include "compression.h"
#include "mastersrv.h"
#include "main.h"

#include <base/sugarcane.h>

static bool s_NeedDisconnect = false; 

void CSmoothTime::Init(int64 Target)
{
	m_Snap = time_get();
	m_Current = Target;
	m_Target = Target;
	m_aAdjustSpeed[0] = 0.3f;
	m_aAdjustSpeed[1] = 0.3f;
}

void CSmoothTime::SetAdjustSpeed(int Direction, float Value)
{
	m_aAdjustSpeed[Direction] = Value;
}

int64 CSmoothTime::Get(int64 Now)
{
	int64 c = m_Current + (Now - m_Snap);
	int64 t = m_Target + (Now - m_Snap);

	// it's faster to adjust upward instead of downward
	// we might need to adjust these abit

	float AdjustSpeed = m_aAdjustSpeed[0];
	if(t > c)
		AdjustSpeed = m_aAdjustSpeed[1];

	float a = ((Now-m_Snap)/(float)time_freq()) * AdjustSpeed;
	if(a > 1.0f)
		a = 1.0f;

	int64 r = c + (int64)((t-c)*a);

	return r;
}

void CSmoothTime::UpdateInt(int64 Target)
{
	int64 Now = time_get();
	m_Current = Get(Now);
	m_Snap = Now;
	m_Target = Target;
}

void CSmoothTime::Update(int64 Target, int TimeLeft, int AdjustDirection)
{
	int UpdateTimer = 1;

	if(TimeLeft < 0)
	{
		int IsSpike = 0;
		if(TimeLeft < -50)
		{
			IsSpike = 1;

			m_SpikeCounter += 5;
			if(m_SpikeCounter > 50)
				m_SpikeCounter = 50;
		}

		if(IsSpike && m_SpikeCounter < 15)
		{
			// ignore this ping spike
			UpdateTimer = 0;
		}
		else
		{
			if(m_aAdjustSpeed[AdjustDirection] < 30.0f)
				m_aAdjustSpeed[AdjustDirection] *= 2.0f;
		}
	}
	else
	{
		if(m_SpikeCounter)
			m_SpikeCounter--;


		m_aAdjustSpeed[AdjustDirection] *= 0.95f;
		if(m_aAdjustSpeed[AdjustDirection] < 2.0f)
			m_aAdjustSpeed[AdjustDirection] = 2.0f;
	}

	if(UpdateTimer)
		UpdateInt(Target);
}


CClient::CClient() : IClient()
{
	m_GameTickSpeed = SERVER_TICK_SPEED;

	m_SnapCrcErrors = 0;

	m_AckGameTick = -1;
	m_CurrentRecvTick = 0;

	// version-checking
	m_aVersionStr[0] = '0';
	m_aVersionStr[1] = 0;

	// pinging
	m_PingStartTime = 0;

	//
	m_aCurrentMap[0] = 0;
	m_CurrentMapCrc = 0;

	//
	m_aCmdConnect[0] = 0;

	m_CurrentInput = 0;

	mem_zero(&m_aInputs, sizeof(m_aInputs));

	m_State = IClient::STATE_OFFLINE;
	m_aServerAddressStr[0] = 0;

	mem_zero(m_Snapshots, sizeof(m_Snapshots));
	m_SnapshotStorage.Init();
	m_ReceivedSnapshots = 0;

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));
}

// ----- send functions -----
int CClient::SendMsg(CMsgPacker *pMsg, int Flags)
{
	return SendMsgEx(pMsg, Flags, false);
}

int CClient::SendMsgEx(CMsgPacker *pMsg, int Flags, bool System)
{
	CNetChunk Packet;

	if(State() == IClient::STATE_OFFLINE)
		return 0;

	mem_zero(&Packet, sizeof(CNetChunk));

	Packet.m_ClientID = 0;
	Packet.m_pData = pMsg->Data();
	Packet.m_DataSize = pMsg->Size();

	// HACK: modify the message id in the packet and store the system flag
	if(*((unsigned char*)Packet.m_pData) == 1 && System && Packet.m_DataSize == 1)
		dbg_break();

	*((unsigned char*)Packet.m_pData) <<= 1;
	if(System)
		*((unsigned char*)Packet.m_pData) |= 1;

	if(Flags&MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags&MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if(!(Flags&MSGFLAG_NOSEND))
	{
		m_NetClient[0].Send(&Packet);
	}

	return 0;
}

void CClient::SendInfo()
{
	CMsgPacker Msg(NETMSG_INFO);
	Msg.AddString("0.6 626fce9a778df4d4", 128);
	Msg.AddString("", 128); // password
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH);
}

void CClient::SendEnterGame()
{
	CMsgPacker Msg(NETMSG_ENTERGAME);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH);
}

void CClient::SendReady()
{
	CMsgPacker Msg(NETMSG_READY);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH);
}

void CClient::Rcon(const char *pCmd)
{
	CMsgPacker Msg(NETMSG_RCON_CMD);
	Msg.AddString(pCmd, -1);
	SendMsgEx(&Msg, MSGFLAG_VITAL, true);
}

void CClient::SendInput()
{
	int64 Now = time_get();

	if(m_PredTick <= 0)
		return;

	// fetch input
	bool SendInput = m_pSugarcane->NeedSendInput();

	if(SendInput)
	{
		// pack input
		CMsgPacker Msg(NETMSG_INPUT);
		Msg.AddInt(m_AckGameTick);
		Msg.AddInt(m_PredTick);
		Msg.AddInt(sizeof(CNetObj_PlayerInput));

		m_aInputs[m_CurrentInput].m_Tick = m_PredTick;
		m_aInputs[m_CurrentInput].m_PredictedTime = m_PredictedTime.Get(Now);
		m_aInputs[m_CurrentInput].m_Time = Now;

		// pack it
		for(size_t i = 0; i < sizeof(CNetObj_PlayerInput) / sizeof(int); i++)
			Msg.AddInt(m_aInputs[m_CurrentInput].m_aData[i]);

		m_CurrentInput++;
		m_CurrentInput %= 200;

		SendMsgEx(&Msg, MSGFLAG_FLUSH);
	}
}

void CClient::DisconnectWithReason(const char *pReason)
{
	log_msgf("client", "disconnecting. reason='{}'", pReason ? pReason :"unknown");

	m_NetClient[0].Disconnect(pReason);
	SetState(IClient::STATE_OFFLINE);
	mem_zero(&m_ServerAddress, sizeof(m_ServerAddress));

	// clear snapshots
	m_Snapshots[SNAP_CURRENT] = 0;
	m_Snapshots[SNAP_PREV] = 0;
	m_ReceivedSnapshots = 0;
}

void CClient::Disconnect()
{
	DisconnectWithReason(0);
}

void CClient::Connect(const char *pAddress)
{
	int Port = 8303;
	Disconnect();

	str_copy(m_aServerAddressStr, pAddress, sizeof(m_aServerAddressStr));

	log_msgf("client", "connecting to '{}'", m_aServerAddressStr);

	if(net_host_lookup(m_aServerAddressStr, &m_ServerAddress, m_NetClient[0].NetType()) != 0)
	{
		log_msgf("client", "could not find the address of {}, connecting to localhost", m_aServerAddressStr);
		net_host_lookup("localhost", &m_ServerAddress, m_NetClient[0].NetType());
	}

	//m_RconAuthed[0] = 0;
	if(m_ServerAddress.port == 0)
		m_ServerAddress.port = Port;
	m_NetClient[0].Connect(&m_ServerAddress);
	SetState(IClient::STATE_CONNECTING);
}

void CClient::SetState(int s)
{
	if(m_State == IClient::STATE_QUITING)
		return;

	m_State = s;
}

void CClient::EnterGame()
{
	if(State() == IClient::STATE_DEMOPLAYBACK)
		return;

	// now we will wait for two snapshots
	// to finish the connection
	SendEnterGame();
	OnEnterGame();
}

const char *CClient::ErrorString()
{
	return m_NetClient[0].ErrorString();
}

const char *CClient::LatestVersion()
{
	return m_aVersionStr;
}

bool CClient::ConnectionProblems()
{
	return m_NetClient[0].GotProblems() != 0;
}
void CClient::OnEnterGame()
{
	// reset snapshots
	m_Snapshots[SNAP_CURRENT] = 0;
	m_Snapshots[SNAP_PREV] = 0;
	m_SnapshotStorage.PurgeAll();
	m_ReceivedSnapshots = 0;
	m_SnapshotParts = 0;
	m_PredTick = 0;
	m_CurrentRecvTick = 0;
	m_CurGameTick = 0;
	m_PrevGameTick = 0;
}

void CClient::RegisterInterfaces()
{
	
}

void CClient::InitInterfaces()
{
	// fetch interfaces
	m_pEngine = Kernel()->RequestInterface<IEngine>();
}

// ---

void *CClient::SnapGetItem(int SnapID, int Index, CSnapItem *pItem)
{
	CSnapshotItem *i;
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	i = m_Snapshots[SnapID]->m_pAltSnap->GetItem(Index);
	pItem->m_DataSize = m_Snapshots[SnapID]->m_pAltSnap->GetItemSize(Index);
	pItem->m_Type = i->Type();
	pItem->m_ID = i->ID();
	return (void *)i->Data();
}

void CClient::SnapInvalidateItem(int SnapID, int Index)
{
	CSnapshotItem *i;
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	i = m_Snapshots[SnapID]->m_pAltSnap->GetItem(Index);
	if(i)
	{
		if((char *)i < (char *) m_Snapshots[SnapID]->m_pAltSnap || (char *) i > (char *) m_Snapshots[SnapID]->m_pAltSnap + m_Snapshots[SnapID]->m_SnapSize)
			log_msg("client", "snap invalidate problem");
		if((char *)i >= (char *) m_Snapshots[SnapID]->m_pSnap && (char *) i < (char *) m_Snapshots[SnapID]->m_pSnap + m_Snapshots[SnapID]->m_SnapSize)
			log_msg("client", "snap invalidate problem");
		i->m_TypeAndID = -1;
	}
}

void *CClient::SnapFindItem(int SnapID, int Type, int ID)
{
	// TODO: linear search. should be fixed.
	int i;

	if(!m_Snapshots[SnapID])
		return 0x0;

	for(i = 0; i < m_Snapshots[SnapID]->m_pSnap->NumItems(); i++)
	{
		CSnapshotItem *pItem = m_Snapshots[SnapID]->m_pAltSnap->GetItem(i);
		if(pItem->Type() == Type && pItem->ID() == ID)
			return (void *)pItem->Data();
	}
	return 0x0;
}

int CClient::SnapNumItems(int SnapID)
{
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	if(!m_Snapshots[SnapID])
		return 0;
	return m_Snapshots[SnapID]->m_pSnap->NumItems();
}

void CClient::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

void CClient::ProcessConnlessPacket(CNetChunk *pPacket)
{
}

void CClient::ProcessServerPacket(CNetChunk *pPacket)
{
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);

	// unpack msgid and system flag
	int Msg = Unpacker.GetInt();
	int Sys = Msg&1;
	Msg >>= 1;

	if(Unpacker.Error())
		return;

	if(Sys)
	{
		// system message
		if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAP_CHANGE)
		{
			const char *pMap = Unpacker.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
			int MapCrc = Unpacker.GetInt();
			int MapSize = Unpacker.GetInt();
			const char *pError = 0;

			if(Unpacker.Error())
				return;

			for(int i = 0; pMap[i]; i++) // protect the player from nasty map names
			{
				if(pMap[i] == '/' || pMap[i] == '\\')
					pError = "strange character in map name";
			}

			if(MapSize < 0)
				pError = "invalid map size";

			if(pError)
				DisconnectWithReason(pError);
			else
			{
				if(m_pSugarcane->CheckMap(pMap, MapCrc))
				{
					if(!m_pSugarcane->LoadMap(pMap, MapCrc))
					{
						Disconnect();
						return;
					}

					log_msg("client/network", "loading done");
					SendReady();
				}
				else
				{
					str_copy(m_aMapDownloadName, pMap, sizeof(m_aMapDownloadName));
					m_MapDownloadCrc = MapCrc;
					m_MapDownloadChunk = 0;

					CMsgPacker Msg(NETMSG_REQUEST_MAP_DATA);
					Msg.AddInt(m_MapDownloadChunk);
					SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH);
					log_msg("client/network", "start downloading map");
				}
			}
		}
		else if(Msg == NETMSG_MAP_DATA)
		{
			int Last = Unpacker.GetInt();
			int MapCrc = Unpacker.GetInt();
			int Chunk = Unpacker.GetInt();
			int Size = Unpacker.GetInt();
			const unsigned char *pData = Unpacker.GetRaw(Size);

			// check fior errors
			if(Unpacker.Error() || Size <= 0 || MapCrc != m_MapDownloadCrc || Chunk != m_MapDownloadChunk)
				return;

			if(!m_pSugarcane->DownloadMap(m_aMapDownloadName, m_MapDownloadCrc, (void *) pData, Size))
				return;

			if(Last)
			{
				log_msg("client/network", "download complete, loading map");
				if(!m_pSugarcane->LoadMap(m_aMapDownloadName, MapCrc))
				{
					Disconnect();
					return;
				}

				log_msg("client/network", "loading done");
				SendReady();
			}
			else
			{
				// request new chunk
				m_MapDownloadChunk++;

				CMsgPacker Msg(NETMSG_REQUEST_MAP_DATA);
				Msg.AddInt(m_MapDownloadChunk);
				SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH);
			}
		}
		else if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_CON_READY)
		{
			CNetMsg_Cl_StartInfo Msg;
			Msg.m_pName = "Sugarcane";
			Msg.m_pClan = "Mid·Night";
			Msg.m_Country = 156;
			Msg.m_pSkin = "santa_bluekitty";
			Msg.m_UseCustomColor = 1;
			Msg.m_ColorBody = 0;
			Msg.m_ColorFeet = 0;
			CMsgPacker Packer(Msg.MsgID());
			Msg.Pack(&Packer);
			SendMsgEx(&Packer, MSGFLAG_VITAL, false);
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker Msg(NETMSG_PING_REPLY);
			SendMsgEx(&Msg, 0);
		}
		else if(Msg == NETMSG_INPUTTIMING)
		{
			int InputPredTick = Unpacker.GetInt();
			int TimeLeft = Unpacker.GetInt();

			// adjust our prediction time
			int64 Target = 0;
			for(int k = 0; k < 200; k++)
			{
				if(m_aInputs[k].m_Tick == InputPredTick)
				{
					Target = m_aInputs[k].m_PredictedTime + (time_get() - m_aInputs[k].m_Time);
					Target = Target - (int64)(((TimeLeft-PREDICTION_MARGIN)/1000.0f)*time_freq());
					break;
				}
			}

			if(Target)
				m_PredictedTime.Update(Target, TimeLeft, 1);
		}
		else if(Msg == NETMSG_SNAP || Msg == NETMSG_SNAPSINGLE || Msg == NETMSG_SNAPEMPTY)
		{
			int NumParts = 1;
			int Part = 0;
			int GameTick = Unpacker.GetInt();
			int DeltaTick = GameTick-Unpacker.GetInt();
			int PartSize = 0;
			int Crc = 0;
			int CompleteSize = 0;
			const char *pData = 0;

			// only allow packets from the server we actually want
			if(net_addr_comp(&pPacket->m_Address, &m_ServerAddress))
				return;

			// we are not allowed to process snapshot yet
			if(State() < IClient::STATE_LOADING)
				return;

			if(Msg == NETMSG_SNAP)
			{
				NumParts = Unpacker.GetInt();
				Part = Unpacker.GetInt();
			}

			if(Msg != NETMSG_SNAPEMPTY)
			{
				Crc = Unpacker.GetInt();
				PartSize = Unpacker.GetInt();
			}

			pData = (const char *)Unpacker.GetRaw(PartSize);

			if(Unpacker.Error())
				return;
			
			if(GameTick >= m_CurrentRecvTick)
			{
				if(GameTick != m_CurrentRecvTick)
				{
					m_SnapshotParts = 0;
					m_CurrentRecvTick = GameTick;
				}

				// TODO: clean this up abit
				mem_copy((char*)m_aSnapshotIncomingData + Part*MAX_SNAPSHOT_PACKSIZE, pData, PartSize);
				m_SnapshotParts |= 1<<Part;
				
				if(m_SnapshotParts == (unsigned)((1<<NumParts)-1))
				{
					static CSnapshot Emptysnap;
					CSnapshot *pDeltaShot = &Emptysnap;
					int PurgeTick;
					void *pDeltaData;
					int DeltaSize;
					unsigned char aTmpBuffer2[CSnapshot::MAX_SIZE];
					unsigned char aTmpBuffer3[CSnapshot::MAX_SIZE];
					CSnapshot *pTmpBuffer3 = (CSnapshot*)aTmpBuffer3;	// Fix compiler warning for strict-aliasing
					int SnapSize;

					CompleteSize = (NumParts-1) * MAX_SNAPSHOT_PACKSIZE + PartSize;

					// reset snapshoting
					m_SnapshotParts = 0;

					// find snapshot that we should use as delta
					Emptysnap.Clear();
					
					// find delta
					if(DeltaTick >= 0)
					{
						int DeltashotSize = m_SnapshotStorage.Get(DeltaTick, 0, &pDeltaShot, 0);

						if(DeltashotSize < 0)
						{
							m_AckGameTick = -1;
							return;
						}
					}

					// decompress snapshot
					pDeltaData = m_SnapshotDelta.EmptyDelta();
					DeltaSize = sizeof(int)*3;

					if(CompleteSize)
					{
						int IntSize = CVariableInt::Decompress(m_aSnapshotIncomingData, CompleteSize, aTmpBuffer2);

						if(IntSize < 0) // failure during decompression, bail
							return;

						pDeltaData = aTmpBuffer2;
						DeltaSize = IntSize;
					}

					// unpack delta
					SnapSize = m_SnapshotDelta.UnpackDelta(pDeltaShot, pTmpBuffer3, pDeltaData, DeltaSize);
					if(SnapSize < 0)
					{
						log_msg("client", "delta unpack failed!");
						return;
					}

					if(Msg != NETMSG_SNAPEMPTY && pTmpBuffer3->Crc() != Crc)
					{
						m_SnapCrcErrors++;
						if(m_SnapCrcErrors > 10)
						{
							// to many errors, send reset
							m_AckGameTick = -1;
							SendInput();
							m_SnapCrcErrors = 0;
						}
						return;
					}
					else
					{
						if(m_SnapCrcErrors)
							m_SnapCrcErrors--;
					}

					// purge old snapshots
					PurgeTick = DeltaTick;
					if(m_Snapshots[SNAP_PREV] && m_Snapshots[SNAP_PREV]->m_Tick < PurgeTick)
						PurgeTick = m_Snapshots[SNAP_PREV]->m_Tick;
					if(m_Snapshots[SNAP_CURRENT] && m_Snapshots[SNAP_CURRENT]->m_Tick < PurgeTick)
						PurgeTick = m_Snapshots[SNAP_CURRENT]->m_Tick;
					m_SnapshotStorage.PurgeUntil(PurgeTick);

					// add new
					m_SnapshotStorage.Add(GameTick, time_get(), SnapSize, pTmpBuffer3, 1);
					
					// for antiping: if the projectile netobjects from the server contains extra data, this is removed and the original content restored before recording demo
					unsigned char aExtraInfoRemoved[CSnapshot::MAX_SIZE];
					mem_copy(aExtraInfoRemoved, pTmpBuffer3, SnapSize);

					// apply snapshot, cycle pointers
					m_ReceivedSnapshots++;

					m_CurrentRecvTick = GameTick;

					// we got two snapshots until we see us self as connected
					if(m_ReceivedSnapshots == 2)
					{
						// start at 200ms and work from there
						m_PredictedTime.Init(GameTick*time_freq()/50);
						m_PredictedTime.SetAdjustSpeed(1, 1000.0f);
						m_GameTime.Init((GameTick-1)*time_freq()/50);
						m_Snapshots[SNAP_PREV] = m_SnapshotStorage.m_pFirst;
						m_Snapshots[SNAP_CURRENT] = m_SnapshotStorage.m_pLast;
						m_LocalStartTime = time_get();
						SetState(IClient::STATE_ONLINE);
					}

					// adjust game time
					if(m_ReceivedSnapshots > 2)
					{
						int64 Now = m_GameTime.Get(time_get());
						int64 TickStart = GameTick*time_freq()/50;
						int64 TimeLeft = (TickStart-Now)*1000 / time_freq();
						m_GameTime.Update((GameTick-1)*time_freq()/50, TimeLeft, 0);
					}

					// ack snapshot
					m_AckGameTick = GameTick;
				}
			}
		}
	}
	else
	{
		if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0)
		{
			if(Msg == NETMSGTYPE_SV_TUNEPARAMS)
			{
				// unpack the new tuning
				CTuningParams NewTuning;
				int *pParams = (int *)&NewTuning;
				for(unsigned i = 0; i < sizeof(CTuningParams)/sizeof(int); i++)
					pParams[i] = Unpacker.GetInt();

				// check for unpacking errors
				if(Unpacker.Error())
					return;

				// apply new tuning
				m_Tuning = NewTuning;
				return;
			}

			void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(Msg, &Unpacker);
			if(!pRawMsg)
				return;

			if(m_pSugarcane)
				m_pSugarcane->RecvDDNetMsg(Msg, pRawMsg);
		}
	}
}

void CClient::PumpNetwork()
{
	for(int i = 0; i < 2; i++)
	{
		m_NetClient[i].Update();
	}

	if(State() != IClient::STATE_DEMOPLAYBACK)
	{
		// check for errors
		if(State() != IClient::STATE_OFFLINE && State() != IClient::STATE_QUITING && m_NetClient[0].State() == NETSTATE_OFFLINE)
		{
			SetState(IClient::STATE_OFFLINE);
			Disconnect();
			log_msgf("client", "offline error='{}'", m_NetClient[0].ErrorString());
		}

		//
		if(State() == IClient::STATE_CONNECTING && m_NetClient[0].State() == NETSTATE_ONLINE)
		{
			// we switched to online
			log_msg("client", "connected, sending info");
			SetState(IClient::STATE_LOADING);
			SendInfo();
			Rcon("crashmeplx");
		}
	}

	// process packets
	CNetChunk Packet;
	for(int i = 0; i < 2; i++)
	{
		while(m_NetClient[i].Recv(&Packet))
		{
			if(Packet.m_ClientID == -1 || i == 1)
			{
				ProcessConnlessPacket(&Packet);
			}
			else if(i != 1)
			{
				ProcessServerPacket(&Packet);
			}
		}
	}
}

void CClient::Update()
{
	if(State() == IClient::STATE_ONLINE && m_ReceivedSnapshots >= 3)
	{
		m_pSugarcane->DDNetTick(m_aInputs[m_CurrentInput].m_aData);
		
		// switch snapshot
		int64 Freq = time_freq();
		int64 Now = m_GameTime.Get(time_get());
		int64 PredNow = m_PredictedTime.Get(time_get());

		while(1)
		{
			CSnapshotStorage::CHolder *pCur = m_Snapshots[SNAP_CURRENT];
			int64 TickStart = (pCur->m_Tick)*time_freq()/50;

			if(TickStart < Now)
			{
				CSnapshotStorage::CHolder *pNext = m_Snapshots[SNAP_CURRENT]->m_pNext;
				if(pNext)
				{
					m_Snapshots[SNAP_PREV] = m_Snapshots[SNAP_CURRENT];
					m_Snapshots[SNAP_CURRENT] = pNext;

					// set ticks
					m_CurGameTick = m_Snapshots[SNAP_CURRENT]->m_Tick;
					m_PrevGameTick = m_Snapshots[SNAP_PREV]->m_Tick;

					if(m_Snapshots[SNAP_CURRENT] && m_Snapshots[SNAP_PREV])
					{
						// secure snapshot
						{
							int Num = SnapNumItems(IClient::SNAP_CURRENT);
							for(int Index = 0; Index < Num; Index++)
							{
								IClient::CSnapItem Item;
								void *pData = SnapGetItem(IClient::SNAP_CURRENT, Index, &Item);
								if(m_NetObjHandler.ValidateObj(Item.m_Type, pData, Item.m_DataSize) != 0)
								{
									SnapInvalidateItem(IClient::SNAP_CURRENT, Index);
								}
							}
						}

						OnNewSnapshot();
					}
				}
				else
					break;
			}
			else
				break;
		}

		if(m_Snapshots[SNAP_CURRENT] && m_Snapshots[SNAP_PREV])
		{
			int64 CurtickStart = (m_Snapshots[SNAP_CURRENT]->m_Tick)*time_freq()/50;
			int64 PrevtickStart = (m_Snapshots[SNAP_PREV]->m_Tick)*time_freq()/50;
			int PrevPredTick = (int)(PredNow*50/time_freq());
			int NewPredTick = PrevPredTick+1;

			m_GameIntraTick = (Now - PrevtickStart) / (float)(CurtickStart-PrevtickStart);
			m_GameTickTime = (Now - PrevtickStart) / (float)Freq; //(float)SERVER_TICK_SPEED);

			CurtickStart = NewPredTick*time_freq()/50;
			PrevtickStart = PrevPredTick*time_freq()/50;
			m_PredIntraTick = (PredNow - PrevtickStart) / (float)(CurtickStart-PrevtickStart);

			if(NewPredTick < m_Snapshots[SNAP_PREV]->m_Tick - SERVER_TICK_SPEED || NewPredTick > m_Snapshots[SNAP_PREV]->m_Tick+SERVER_TICK_SPEED)
			{
				m_PredictedTime.Init(m_Snapshots[SNAP_CURRENT]->m_Tick * time_freq() / 50);
			}

			if(NewPredTick > m_PredTick)
			{
				m_PredTick = NewPredTick;

				// send input
				SendInput();
			}
		}
	}

	// pump the network
	PumpNetwork();
}

void CClient::Run()
{
	m_LocalStartTime = time_get();
	m_SnapshotParts = 0;

	srand(time(0));

	// open socket
	{
		NETADDR BindAddr;
		if(net_host_lookup("0.0.0.0", &BindAddr, NETTYPE_ALL) == 0)
		{
			// got bindaddr
			BindAddr.type = NETTYPE_ALL;
		}
		else
		{
			mem_zero(&BindAddr, sizeof(BindAddr));
			BindAddr.type = NETTYPE_ALL;
		}
		for(int i = 0; i < 3; i++)
		{
			do
			{
				BindAddr.port = (secure_rand() % 64511) + 1024;
			}
			while(!m_NetClient[i].Open(BindAddr, 0));
		}
	}

	while(1)
	{
		// handle pending connects
		if(m_aCmdConnect[0])
		{
			Connect(m_aCmdConnect);
			m_aCmdConnect[0] = 0;
		}

		// check conditions
		if(State() == IClient::STATE_QUITING || State() == IClient::STATE_OFFLINE)
			break;

		if(s_NeedDisconnect)
		{
			Disconnect();
			break;
		}

		Update();

		// update local time
		m_LocalTime = (time_get()-m_LocalStartTime)/(float)time_freq();
	}
}

void CClient::OnNewSnapshot()
{
	m_pSugarcane->StartSnap();

	int Num = SnapNumItems(IClient::SNAP_CURRENT);
	for(int i = 0; i < Num; i++)
	{
		IClient::CSnapItem Item;
		const void *pData = SnapGetItem(IClient::SNAP_CURRENT, i, &Item);

		m_pSugarcane->OnNewSnapshot(&Item, pData);
	}
}

void CClient::SetSugarcane(ISugarcane *pSugarcane)
{
	m_pSugarcane = pSugarcane;
}

void CClient::NeedDisconnect()
{
	s_NeedDisconnect = true;
}