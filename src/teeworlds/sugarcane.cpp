#include <include/base.h>

#include <teeworlds/map/convert.h>

#include <teeworlds/six/main.h>
#include <teeworlds/six/generated_protocol.h>
#include <teeworlds/six/client.h>
#include <teeworlds/six/vmath.h>

#include <base/storage.h>
#include <sugarcane/sugarcane.h>

#include <array>
#include <cmath>
#include <chrono>

#include "astar.h"

template<typename T, typename T2>
inline T SaturatedAdd(T2 Min, T2 Max, T Current, T2 Modifier)
{
	if(Modifier < 0)
	{
		if(Current < Min)
			return Current;
		Current += Modifier;
		if(Current < Min)
			Current = Min;
		return Current;
	}
	else
	{
		if(Current > Max)
			return Current;
		Current += Modifier;
		if(Current > Max)
			Current = Max;
		return Current;
	}
}

float VelocityRamp(float Value, float Start, float Range, float Curvature)
{
	if(Value < Start)
		return 1.0f;
	return 1.0f/powf(Curvature, (Value-Start)/Range);
}

struct SCharacter
{
	float m_X;
	float m_Y;
	float m_VelX;
	float m_VelY;
	int m_Angle;
	int m_Direction;
	int m_Jumped;
	int m_HookedPlayer;
	int m_HookState;
	int m_HookTick;
	float m_HookX;
	float m_HookY;
	float m_HookDx;
	float m_HookDy;
	int m_PlayerFlags;
	int m_Health;
	int m_Armor;
	int m_AmmoCount;
	int m_Weapon;
	int m_Emote;
	int m_AttackTick;

    SCharacter& operator=(const CNetObj_Character& Source)
    {
        m_X = (float) Source.m_X;
        m_Y = (float) Source.m_Y;
        m_VelX = (float) Source.m_VelX / 256.0f;
        m_VelY = (float) Source.m_VelY / 256.0f;
        m_Angle = Source.m_Angle;
        m_Direction = Source.m_Direction;
        m_Jumped = Source.m_Jumped;
        m_HookedPlayer = Source.m_HookedPlayer;
        m_HookState = Source.m_HookState;
        m_HookTick = Source.m_HookTick;
        m_HookX = (float) Source.m_HookX;
        m_HookY = (float) Source.m_HookY;
        m_HookDx = (float) Source.m_HookDx / 256.0f;
        m_HookDy = (float) Source.m_HookDy / 256.0f;
        m_PlayerFlags = Source.m_PlayerFlags;
        m_Health = Source.m_Health;
        m_Armor = Source.m_Armor;
        m_AmmoCount = Source.m_AmmoCount;
        m_Weapon = Source.m_Weapon;
        m_Emote = Source.m_Emote;
        m_AttackTick = Source.m_AttackTick;

        return *this;
    }
};

struct SClient
{
    SClient()
    {
        m_Active = false;
        m_ClientID = -1;
        m_aName[0] = 0;
        m_aClan[0] = 0;
    }

    bool m_Active;
    bool m_Alive;

    int m_ClientID;
    int m_Score;
    int m_Team;

    SCharacter m_Character;

    char m_aName[MAX_NAME_LENGTH];
    char m_aClan[MAX_CLAN_LENGTH];
};

struct SLaser
{
    vec2 m_From;
    vec2 m_To;

    SLaser(vec2 From, vec2 To) :
        m_From(From),
        m_To(To)
    {
    }

    SLaser(CNetObj_Laser Laser) :
        m_From(vec2(Laser.m_FromX, Laser.m_FromY)),
        m_To(vec2(Laser.m_X, Laser.m_Y))
    {
    }
};

enum
{
	HOOK_RETRACTED=-1,
	HOOK_IDLE=0,
	HOOK_RETRACT_START=1,
	HOOK_RETRACT_END=3,
	HOOK_FLYING,
	HOOK_GRABBED,
};

static std::array<SClient, MAX_CLIENTS> s_aClients;
static std::vector<SLaser> s_vLasers;

static int s_LocalID;
static int s_TargetTeam;
static string s_NowName;

static vec2 s_GoToPos;
static vec2 s_MouseTarget;
static vec2 s_MouseTargetTo;
static SClient *s_pTarget;
constexpr float g_MouseMoveSpeedPerTick = 30.0f;

static std::vector<std::vector<int>> s_MapGrid;
static std::vector<std::vector<int>> s_MapGridWithEntity;
static ESMapItems *s_pMap;
static AStar *s_pAStar;
static int s_MapWidth;
static int s_MapHeight;

static CNetObj_PlayerInput s_LastInput = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static CNetObj_PlayerInput s_TickInput = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static std::chrono::system_clock::time_point s_LastNameChangeTime = std::chrono::system_clock::now();
static std::chrono::system_clock::time_point s_LastTeamChangeTime = std::chrono::system_clock::now();
static std::chrono::system_clock::time_point s_LastInputTime = std::chrono::system_clock::now();

static const char *s_apInfectClasses[] = {"Hunter", "Smoker", "Spider", "Ghoul", "Undead", "Witch", "Voodoo", "Slug", "Boomer", "Bat", "Ghost"};
static bool IsInfectClass(const char *pClassName)
{
    for(auto& Infect : s_apInfectClasses)
    {
        if(str_find_nocase(pClassName, Infect))
        {
            return true;
        }
    }
    return false;
}

static bool IsInfectClass(int ClientID)
{
    return s_aClients[ClientID].m_Active && IsInfectClass(s_aClients[ClientID].m_aClan);
}

static bool IsHumanClass(int ClientID)
{
    return !IsInfectClass(ClientID);
}

static bool IsOtherTeam(int ClientID)
{
    return IsInfectClass(s_LocalID) != IsInfectClass(ClientID);
}

static ESMapItems GetTile(float X, float Y)
{
    int MapX = clamp(round_to_int(X) / 32, 0, s_MapWidth);
    int MapY = clamp(round_to_int(Y) / 32, 0, s_MapHeight);
    return s_pMap[MapY * s_MapWidth + MapX];
}

static bool CheckPoint(float X, float Y, ESMapItems Flag = ESMapItems::TILEFLAG_SOLID)
{
    return GetTile(X, Y) & Flag;
}

static bool CheckPoint(vec2 Pos, ESMapItems Flag = ESMapItems::TILEFLAG_SOLID)
{
    return GetTile(Pos.x, Pos.y) & Flag;
}

static bool IsGrounded(float X, float Y)
{
    return CheckPoint(X + 14.f, Y + 19.f) || CheckPoint(X - 14.f, Y + 19.f);
}

static bool IsGrounded(vec2 Pos)
{
    return IsGrounded(Pos.x, Pos.y);
}

static ESMapItems IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision)
{
    float Distance = distance(Pos0, Pos1);
    int End(Distance+1);
    vec2 Last = Pos0;

    for(int i = 0; i < End; i++)
    {
        float a = i/Distance;
        vec2 Pos = mix(Pos0, Pos1, a);
        if(CheckPoint(Pos.x, Pos.y))
        {
            if(pOutCollision)
                *pOutCollision = Pos;
            if(pOutBeforeCollision)
                *pOutBeforeCollision = Last;
            return GetTile(Pos.x, Pos.y);
        }
        Last = Pos;
    }
    if(pOutCollision)
        *pOutCollision = Pos1;
    if(pOutBeforeCollision)
        *pOutBeforeCollision = Pos1;
    return ESMapItems::TILEFLAG_AIR;
}

static bool TestBox(vec2 Pos, vec2 Size)
{
    Size *= 0.5f;
    if(CheckPoint(Pos.x-Size.x, Pos.y-Size.y))
        return true;
    if(CheckPoint(Pos.x+Size.x, Pos.y-Size.y))
        return true;
    if(CheckPoint(Pos.x-Size.x, Pos.y+Size.y))
        return true;
    if(CheckPoint(Pos.x+Size.x, Pos.y+Size.y))
        return true;
    return false;
}

static void MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, float Elasticity)
{
    // do the move
    vec2 Pos = *pInoutPos;
    vec2 Vel = *pInoutVel;

    float Distance = length(Vel);
    int Max = (int)Distance;

    if(Distance > 0.00001f)
    {
        //vec2 old_pos = pos;
        float Fraction = 1.0f/(float)(Max+1);
        for(int i = 0; i <= Max; i++)
        {
            //float amount = i/(float)max;
            //if(max == 0)
                //amount = 0;

            vec2 NewPos = Pos + Vel*Fraction; // TODO: this row is not nice

            if(TestBox(vec2(NewPos.x, NewPos.y), Size))
            {
                int Hits = 0;

                if(TestBox(vec2(Pos.x, NewPos.y), Size))
                {
                    NewPos.y = Pos.y;
                    Vel.y *= -Elasticity;
                    Hits++;
                }

                if(TestBox(vec2(NewPos.x, Pos.y), Size))
                {
                    NewPos.x = Pos.x;
                    Vel.x *= -Elasticity;
                    Hits++;
                }

                // neither of the tests got a collision.
                // this is a real _corner case_!
                if(Hits == 0)
                {
                    NewPos.y = Pos.y;
                    Vel.y *= -Elasticity;
                    NewPos.x = Pos.x;
                    Vel.x *= -Elasticity;
                }
            }

            Pos = NewPos;
        }
    }

    *pInoutPos = Pos;
    *pInoutVel = Vel;
}

void CSugarcane::InitTwsPart()
{
    s_LocalID = -1;
    s_pMap = nullptr;
    s_pAStar = nullptr;
    s_NowName = "Sugarcane";
    s_MapWidth = 0;
    s_MapHeight = 0;
    s_TargetTeam = 0;
    s_MouseTarget = vec2(0.f, 0.f);
}

void CSugarcane::InputPrediction()
{
    const int PredictTicks = 25;
    vec2 NowPos(s_aClients[s_LocalID].m_Character.m_X, s_aClients[s_LocalID].m_Character.m_Y);

    CNetObj_PlayerInput TempInput;
    memcpy(&TempInput, &s_TickInput, sizeof(CNetObj_PlayerInput));

    CTuningParams *pTuning = DDNet::s_pClient->Tuning();
    s_TickInput.m_TargetX = (int) s_MouseTarget.x;
    s_TickInput.m_TargetY = (int) s_MouseTarget.y;

    if(s_pTarget)
    {
        vec2 TargetPos = vec2(s_pTarget->m_Character.m_X, s_pTarget->m_Character.m_Y);
        ESMapItems FrontBlock = IntersectLine(NowPos, TargetPos, nullptr, nullptr);
        if(!(FrontBlock & ESMapItems::TILEFLAG_SOLID) && distance(s_MouseTarget, normalize(TargetPos - NowPos)) < 0.1f)
        {
            if(IsInfectClass(s_LocalID))
            {
                if(distance(NowPos, TargetPos) < 96.0f)
                {
                    s_TickInput.m_Fire = !s_LastInput.m_Fire;
                }
            }
            else
            {
                s_TickInput.m_Fire = !s_LastInput.m_Fire;
            }
        }
    }

    if(!s_pAStar)
    {
        if(distance(s_MouseTargetTo, s_MouseTarget) > g_MouseMoveSpeedPerTick)
        {
            vec2 Direction = normalize(s_MouseTargetTo - s_MouseTarget);
            s_MouseTarget += Direction * g_MouseMoveSpeedPerTick;
            if(length(s_MouseTarget) < 120.0f)
            {
                s_MouseTarget = normalize(s_MouseTarget) * 120.0f;
            }
        }
        return;
    }

    const int PhysSize = 28;
    std::vector<std::pair<int, int>> Path = s_pAStar->findPath({NowPos.y / 32, (NowPos.x + 14.f) / 32}, 20);

    if(Path.empty())
    {
        if(distance(s_MouseTargetTo, s_MouseTarget) > g_MouseMoveSpeedPerTick)
        {
            vec2 Direction = normalize(s_MouseTargetTo - s_MouseTarget);
            s_MouseTarget += Direction * g_MouseMoveSpeedPerTick;
            if(length(s_MouseTarget) < 120.0f)
            {
                s_MouseTarget = normalize(s_MouseTarget) * 120.0f;
            }
        }
        return;
    }

    int MoveX = Path[0].second;
    int MoveY = Path[0].first;
    s_TickInput.m_Direction = MoveX;

    // search hook
    vec2 CollisionPos;
    ESMapItems FrontBlock = IntersectLine(NowPos, NowPos + normalize(s_MouseTarget) * pTuning->m_HookLength * 0.95f, nullptr, &CollisionPos);

    vec2 HookPos(s_aClients[s_LocalID].m_Character.m_HookX, s_aClients[s_LocalID].m_Character.m_HookY);
    if(s_aClients[s_LocalID].m_Character.m_HookState == HOOK_GRABBED)
    {
        s_TickInput.m_Hook = s_LastInput.m_Hook;
        s_TickInput.m_Direction = s_LastInput.m_Direction;
        if(distance(HookPos / 32, CollisionPos / 32) > 4.f || distance(HookPos, NowPos) < 18.f)
        {
            s_TickInput.m_Hook = 0;
            return;
        }
    }

    if(MoveY < 0)
    {
        bool FoundPos = false;
        vec2 CheckPos = NowPos;

        for(auto& Move : Path)
        {
            CheckPos += vec2(Move.second , Move.first) * 32.f;
            if(IsGrounded(CheckPos) && abs(CheckPos.y - NowPos.y) > 48.0f)
            {
                FoundPos = true;
                break;
            }
        }
        if(FoundPos)
        {
            s_MouseTargetTo = CheckPos + vec2(0.f, -48.f) - NowPos;
        }
        if((s_aClients[s_LocalID].m_Character.m_Jumped & 1 || 
            s_aClients[s_LocalID].m_Character.m_Jumped & 3 || 
            (CheckPos.y + 96.f < NowPos.y && CheckPos.y + 192.f > NowPos.y)) && 
            FrontBlock & ESMapItems::TILEFLAG_SOLID && 
            !(FrontBlock & ESMapItems::TILEFLAG_UNHOOKABLE))
        {
            if(s_aClients[s_LocalID].m_Character.m_HookState == HOOK_IDLE)
            {
                if(abs(CollisionPos.y - NowPos.y) > 16.f)
                {
                    s_TickInput.m_Hook = 1;
                    s_TickInput.m_Direction = (int) -sign(s_MouseTarget.x / 64);
                }
                else if(!s_LastInput.m_Jump && s_aClients[s_LocalID].m_Character.m_VelY / 256.f >= 0.f)
                {
                    s_TickInput.m_Jump = 1;
                }
            }
        }
        else if(!s_LastInput.m_Jump && s_aClients[s_LocalID].m_Character.m_VelY / 256.f >= 0.0f)
        {
            s_TickInput.m_Jump = 1;
        }
    }

    {
        if(distance(s_MouseTargetTo, s_MouseTarget) > g_MouseMoveSpeedPerTick)
        {
            vec2 Direction = normalize(s_MouseTargetTo - s_MouseTarget);
            s_MouseTarget += Direction * g_MouseMoveSpeedPerTick;
            if(length(s_MouseTarget) < 120.0f)
            {
                s_MouseTarget = normalize(s_MouseTarget) * 120.0f;
            }
        }
    }
}

void CSugarcane::TwsTalkBack(string Talk)
{
    CNetMsg_Cl_Say Msg;
    Msg.m_pMessage = Talk.c_str();
    Msg.m_Team = 0;
    DDNet::s_pClient->SendPackMsg(&Msg, MSGFLAG_VITAL);
}

void CSugarcane::OnNewSnapshot(void *pItem, const void *pData)
{
    IClient::CSnapItem *pSnapItem = (IClient::CSnapItem *) pItem;
    switch(pSnapItem->m_Type)
    {
        case NETOBJTYPE_CLIENTINFO:
        {
            CNetObj_ClientInfo *pObj = (CNetObj_ClientInfo *) pData;

            int ClientID = pSnapItem->m_ID;
            s_aClients[ClientID].m_ClientID = ClientID;
            IntsToStr(&pObj->m_Name0, 4, s_aClients[ClientID].m_aName, sizeof(s_aClients[ClientID].m_aName));
            IntsToStr(&pObj->m_Clan0, 3, s_aClients[ClientID].m_aClan, sizeof(s_aClients[ClientID].m_aClan));
        }
        break;

        case NETOBJTYPE_PLAYERINFO:
        {
            CNetObj_PlayerInfo *pObj = (CNetObj_PlayerInfo *) pData;

            int ClientID = pSnapItem->m_ID;
            if(pObj->m_Local)
                s_LocalID = ClientID;

            s_aClients[ClientID].m_Team = pObj->m_Team;
            s_aClients[ClientID].m_Score = pObj->m_Score;

            s_aClients[ClientID].m_Active = true;
        }
        break;

        case NETOBJTYPE_CHARACTER:
        {
            CNetObj_Character *pObj = (CNetObj_Character *) pData;

            int ClientID = pSnapItem->m_ID;

            s_aClients[ClientID].m_Alive = true;
            s_aClients[ClientID].m_Character = *pObj;
        }
        break;

        case NETOBJTYPE_LASER:
        {
            s_vLasers.push_back(SLaser(*(CNetObj_Laser *) pData));
        }
        break;
    }
}

void CSugarcane::RecvDDNetMsg(int MsgID, void *pData)
{
    if(MsgID == NETMSGTYPE_SV_READYTOENTER)
    {
        DDNet::s_pClient->EnterGame();

        CNetMsg_Cl_Say Msg;
        Msg.m_pMessage = "你好!这里是14.1岁的甘蔗!";
        Msg.m_Team = 0;
        DDNet::s_pClient->SendPackMsg(&Msg, MSGFLAG_VITAL);
    }
    else if(MsgID == NETMSGTYPE_SV_CHAT)
    {
        CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pData;
        if (pMsg->m_ClientID == -1)
            log_msgf("chat", "*** {}", pMsg->m_pMessage);
        else if(pMsg->m_ClientID >= 0 && pMsg->m_ClientID < MAX_CLIENTS && s_aClients[pMsg->m_ClientID].m_Active)
            log_msgf("chat", "{}: {}", s_aClients[pMsg->m_ClientID].m_aName, pMsg->m_pMessage);
        else
            log_msgf("chat", "{}: {}", pMsg->m_ClientID, pMsg->m_pMessage);

        if(string(pMsg->m_pMessage).startswith(s_NowName))
            BackTalk(pMsg->m_pMessage + 10, TwsTalkBack);
    }
    else if(MsgID == NETMSGTYPE_SV_BROADCAST)
    {
        CNetMsg_Sv_Broadcast *pMsg = (CNetMsg_Sv_Broadcast *)pData;
        if(!pMsg->m_pMessage || !pMsg->m_pMessage[0])
            return;
        log_msg("broadcast", pMsg->m_pMessage);
    }
}

void CSugarcane::DDNetTick(int *pInputData)
{
    int OtherPlayersCount = 0;
    for(auto& Client : s_aClients)
    {
        if(Client.m_Active && Client.m_ClientID != s_LocalID && Client.m_Team != TEAM_SPECTATORS)
            OtherPlayersCount++;

        if(Client.m_Active && 
            s_LastNameChangeTime + std::chrono::seconds(3) < std::chrono::system_clock::now() && 
            string(Client.m_aName) == string("Sugarcane") &&
            Client.m_ClientID != s_LocalID &&
            string(s_aClients[s_LocalID].m_aName) != string("TestSugarcane"))
        {
            // use elder sister
            CNetMsg_Cl_ChangeInfo Msg;
			Msg.m_pName = "TestSugarcane";
			Msg.m_pClan = "Mid·Night";
			Msg.m_Country = 156;
			Msg.m_pSkin = "santa_limekitty";
			Msg.m_UseCustomColor = 1;
			Msg.m_ColorBody = 0;
			Msg.m_ColorFeet = 0;
            DDNet::s_pClient->SendPackMsg(&Msg, MSGFLAG_VITAL);

            s_LastNameChangeTime = std::chrono::system_clock::now();
            s_NowName = "TestSugarcane";
            log_msg("sugarcane/tws", "send rename msg");
        }
    }

    if(OtherPlayersCount)
    {
        if(s_aClients[s_LocalID].m_Team == TEAM_SPECTATORS)
        {
            s_TargetTeam = TEAM_RED;
        }
    }
    else
    {
        s_TargetTeam = TEAM_SPECTATORS;
    }

    if(s_TargetTeam != s_aClients[s_LocalID].m_Team)
    {
        if(s_LastTeamChangeTime + std::chrono::seconds(3) < std::chrono::system_clock::now())
        {
            // use elder sister
            CNetMsg_Cl_ChangeInfo Msg;
			Msg.m_pName = "TestSugarcane";
			Msg.m_pClan = "Mid·Night";
			Msg.m_Country = 156;
			Msg.m_pSkin = "santa_limekitty";
			Msg.m_UseCustomColor = 1;
			Msg.m_ColorBody = 0;
			Msg.m_ColorFeet = 0;
            DDNet::s_pClient->SendPackMsg(&Msg, MSGFLAG_VITAL);

            s_LastNameChangeTime = std::chrono::system_clock::now();
            s_NowName = "TestSugarcane";
            log_msg("sugarcane/tws", "send switch team msg");
        }
    }

    if(s_aClients[s_LocalID].m_Alive)
    {
        vec2 NowPos(s_aClients[s_LocalID].m_Character.m_X, s_aClients[s_LocalID].m_Character.m_Y);

        // find target
        SClient *pLastTarget = s_pTarget;
        SClient *pMoveTarget = nullptr;
        s_pTarget = nullptr;

        float ClosetDistance = 9000.f;
        bool SelfInfect = IsInfectClass(s_LocalID);

        s_MapGridWithEntity = s_MapGrid;
        if(SelfInfect)
        {
            for(auto& Laser : s_vLasers)
            {
                float Distance = distance(Laser.m_From, Laser.m_To);
                int End(Distance+1);
                vec2 Last = Laser.m_From;

                for(int i = 0; i < End; i++)
                {
                    float a = i/Distance;
                    vec2 Pos = mix(Laser.m_From, Laser.m_To, a);
                    Last = Pos;
                    Pos /= 32;
                    if(Pos.x < 0 || Pos.x >= s_MapWidth ||
                        Pos.y < 0 || Pos.y >= s_MapHeight)
                        break;

                    s_MapGridWithEntity[Pos.y / 32][Pos.x / 32] = -1;
                }
            }
        }
        for(auto& Client : s_aClients)
        {
            if(Client.m_Active && Client.m_Alive && Client.m_ClientID != s_LocalID)
            {
                vec2 Pos = vec2(Client.m_Character.m_X, Client.m_Character.m_Y);
                float Distance = distance(Pos, NowPos);
                if(SelfInfect && !IsOtherTeam(Client.m_ClientID))
                {
                    Distance += (!pLastTarget || (pLastTarget && pLastTarget->m_ClientID != Client.m_ClientID)) ? 800.0f : 500.f;
                }
                else if(IsOtherTeam(Client.m_ClientID))
                {
                    Distance -= 200.0f;
                }

                if(Distance < ClosetDistance)
                {
                    ClosetDistance = Distance;
                    if(!IsOtherTeam(Client.m_ClientID))
                        pMoveTarget = &Client;
                    else
                    {
                        if(SelfInfect)
                            pMoveTarget = &Client;
                        s_pTarget = &Client;
                    }
                }
            }
        }

        if(pMoveTarget)
        {
            vec2 Pos = vec2(pMoveTarget->m_Character.m_X, pMoveTarget->m_Character.m_Y);
            if(s_pAStar)
                delete s_pAStar;

            s_pAStar = new AStar(s_MapGridWithEntity, {Pos.y / 32, Pos.x / 32});
            s_GoToPos = Pos;
            s_MouseTargetTo = Pos - NowPos;
        }

        if(s_pTarget)
        {
            s_MouseTargetTo = vec2(s_pTarget->m_Character.m_X, s_pTarget->m_Character.m_Y) - NowPos;
        }
    }

    DoInput(pInputData);
}

void CSugarcane::DoInput(int *pInputData)
{
    memcpy(&s_LastInput, &s_TickInput, sizeof(s_TickInput));
    memset(&s_TickInput, 0, sizeof(s_TickInput));
   
    if(s_aClients[s_LocalID].m_Alive)
        InputPrediction();

    memcpy(pInputData, &s_TickInput, sizeof(s_TickInput));
}

void CSugarcane::StartSnap()
{
    for(auto& Client : s_aClients)
    {
        Client.m_Active = false;
        Client.m_Alive = false;
    }
    s_vLasers.clear();
}

bool CSugarcane::DownloadMap(const char *pMap, int Crc, void* pData, int Size)
{
    return Storage()->TwsDownloadMap(pMap, std::to_string(Crc).c_str(), pData, Size);
}

bool CSugarcane::CheckMap(const char *pMap, int Crc)
{
    return Storage()->TwsMapExists(pMap, std::to_string(Crc).c_str());
}

bool CSugarcane::LoadMap(const char *pMap, int Crc)
{
    if(s_pMap)
        delete[] s_pMap;
    s_pMap = nullptr;
    s_MapWidth = 0;
    s_MapHeight = 0;

    if(!ConvertMap(pMap, std::to_string(Crc).c_str(), &s_pMap, s_MapWidth, s_MapHeight))
    {
        log_msg("sugarcane/tws", "failed to load teeworlds map");
        return false;
    }
    
    for(auto& Line : s_MapGrid)
    {
        Line.clear();
    }

    s_MapGrid.clear();
	s_MapGrid.resize(s_MapHeight);
    for(int y = 0; y < s_MapHeight; y++)
    {
	    s_MapGrid[y].resize(s_MapWidth);
        for(int x = 0; x < s_MapWidth; x++)
        {
            s_MapGrid[y][x] = 0;
            if(s_pMap[y * s_MapWidth + x] & ESMapItems::TILEFLAG_SOLID)
                s_MapGrid[y][x] = 1;
            if(s_pMap[y * s_MapWidth + x] & ESMapItems::TILEFLAG_DEATH)
                s_MapGrid[y][x] = -1;
        }
    }
    return true;
}

bool CSugarcane::NeedSendInput()
{
    bool Send = memcmp(&s_TickInput, &s_LastInput, sizeof(s_TickInput)) || s_LastInputTime + std::chrono::seconds(1) / 25 < std::chrono::system_clock::now();
    if(Send)
        s_LastInputTime = std::chrono::system_clock::now();

    return Send;
}