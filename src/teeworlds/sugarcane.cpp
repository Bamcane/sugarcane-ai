#include <include/base.h>

#include <teeworlds/map/convert.h>

#include <teeworlds/six/main.h>
#include <teeworlds/six/generated_protocol.h>
#include <teeworlds/six/client.h>
#include <teeworlds/six/math.h>
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
    int64_t m_LastSnapshotTick;
    int64_t m_Tick;
    vec2 m_Pos;
    vec2 m_Vel;
    vec2 m_HookPos;
    vec2 m_HookDir;
	int m_Angle;
	int m_Direction;
	int m_Jumped;
	int m_HookedPlayer;
	int m_HookState;
	int m_HookTick;
	int m_PlayerFlags;
	int m_Health;
	int m_Armor;
	int m_AmmoCount;
	int m_Weapon;
	int m_Emote;
	int m_AttackTick;

    void Prediction();

    SCharacter& operator=(const CNetObj_Character& Source)
    {
        m_Pos = vec2((float) Source.m_X, (float) Source.m_Y);
        m_Vel = vec2((float) Source.m_VelX / 256.0f, (float) Source.m_VelY / 256.0f);
        m_HookPos = vec2((float) Source.m_HookX, (float) Source.m_HookY);
        m_HookDir = vec2((float) Source.m_HookDx / 256.0f, (float) Source.m_HookDy / 256.0f);
        m_Angle = Source.m_Angle;
        m_Direction = Source.m_Direction;
        m_Jumped = Source.m_Jumped;
        m_HookedPlayer = Source.m_HookedPlayer;
        m_HookState = Source.m_HookState;
        m_HookTick = Source.m_HookTick;
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

struct SMapDetail
{
    std::vector<vec2> m_vStrongholds;

    void SaveStronghold(vec2 NewStronghold)
    {
        for(auto& Stronghold : m_vStrongholds)
        {
            float Distance = distance(Stronghold, NewStronghold);
            if(Distance < 480.f)
            {
                return;
            }
        }
        m_vStrongholds.push_back(NewStronghold);
    }

    void FindNearestStronghold(vec2 Pos, vec2** pFindPos)
    {
        float MinDistance = 32000.f;

        *pFindPos = nullptr;
        for(auto& Stronghold : m_vStrongholds)
        {
            float Distance = distance(Stronghold, Pos);
            if(Distance < MinDistance)
            {
                *pFindPos = &Stronghold;
                MinDistance = Distance;
            }
        }
    }

    void Reset()
    {
        m_vStrongholds.clear();
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

static SClient s_aClients[MAX_CLIENTS];
static std::vector<SLaser> s_vLasers;
static SMapDetail s_MapDetail;

static int s_LocalID;
static int s_TargetTeam;

static bool s_FindStronghold;

static vec2 s_StrongholdPos;
static vec2 s_GoToPos;
static vec2 s_MouseTarget;
static vec2 s_MouseTargetTo;
static SClient *s_pMoveTarget;
static SClient *s_pTarget;
constexpr float g_MaxMouseMoveSpeedPerTick = 40.0f;
constexpr float g_MinMouseMoveSpeedPerTick = 8.0f;

static std::vector<std::vector<int>> s_MapGrid;
static std::vector<std::vector<int>> s_MapGridWithEntity;
static ESMapItems *s_pMap;
static AStar *s_pAStar;
static int s_MapWidth;
static int s_MapHeight;

static CNetObj_PlayerInput s_LastInput = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static CNetObj_PlayerInput s_TickInput = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static std::chrono::system_clock::time_point s_LastTeamChangeTime = std::chrono::system_clock::now();
static std::chrono::system_clock::time_point s_LastInputTime = std::chrono::system_clock::now();
static std::chrono::system_clock::time_point s_LastFindTeammate = std::chrono::system_clock::now();
static std::chrono::system_clock::time_point s_LastStrongholdFindTime = std::chrono::system_clock::now();

static const char *s_apInfectClasses[] = {"Hunter", "Smoker", "Spider", "Ghoul", "Undead", "Witch", "Voodoo", "Slug", "Boomer", "Bat", "Ghost", "Freezer", "Nightmare", "Slime", "InfectBot"};
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

void SCharacter::Prediction()
{
    float PhysSize = 28.0f;// get ground state
	bool Grounded = false;
	if(CheckPoint(m_Pos.x+PhysSize/2, m_Pos.y+PhysSize/2+5))
		Grounded = true;
	if(CheckPoint(m_Pos.x-PhysSize/2, m_Pos.y+PhysSize/2+5))
		Grounded = true;

    CTuningParams *pTuning = DDNet::s_pClient->Tuning();
	m_Vel.y += pTuning->m_Gravity;

	float MaxSpeed = Grounded ? pTuning->m_GroundControlSpeed : pTuning->m_AirControlSpeed;
	float Accel = Grounded ? pTuning->m_GroundControlAccel : pTuning->m_AirControlAccel;
	float Friction = Grounded ? pTuning->m_GroundFriction : pTuning->m_AirFriction;

	// add the speed modification according to players wanted direction
	if(m_Direction < 0)
		m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Vel.x, -Accel);
	if(m_Direction > 0)
		m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Vel.x, Accel);
	if(m_Direction == 0)
		m_Vel.x *= Friction;

	// handle jumping
	// 1 bit = to keep track if a jump has been made on this input
	// 2 bit = to keep track if a air-jump has been made
	if(Grounded)
		m_Jumped &= ~2;

	// do hook
	if(m_HookState == HOOK_IDLE)
	{
		m_HookedPlayer = -1;
		m_HookState = HOOK_IDLE;
		m_HookPos = m_Pos;
	}
	else if(m_HookState >= HOOK_RETRACT_START && m_HookState < HOOK_RETRACT_END)
	{
		m_HookState++;
	}
	else if(m_HookState == HOOK_RETRACT_END)
	{
		m_HookState = HOOK_RETRACTED;
		m_HookState = HOOK_RETRACTED;
	}
	else if(m_HookState == HOOK_FLYING)
	{
		vec2 NewPos = m_HookPos+m_HookDir*pTuning->m_HookFireSpeed;
		if(distance(m_Pos, NewPos) > pTuning->m_HookLength)
		{
			m_HookState = HOOK_RETRACT_START;
			NewPos = m_Pos + normalize(NewPos-m_Pos) * pTuning->m_HookLength;
		}

		// make sure that the hook doesn't go though the ground
		bool GoingToHitGround = false;
		bool GoingToRetract = false;
		ESMapItems Hit = IntersectLine(m_HookPos, NewPos, &NewPos, 0);
		if(Hit != ESMapItems::TILEFLAG_AIR)
		{
			if(Hit&ESMapItems::TILEFLAG_UNHOOKABLE)
				GoingToRetract = true;
			else
				GoingToHitGround = true;
		}

		// Check against other players first
		if(pTuning->m_PlayerHooking)
		{
			float Distance = 0.0f;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!s_aClients[i].m_Alive)
					continue;
				SCharacter *pCharCore = &s_aClients[i].m_Character;
                if(pCharCore == this)
                    continue;

				vec2 ClosestPoint = closest_point_on_line(m_HookPos, NewPos, pCharCore->m_Pos);
				if(distance(pCharCore->m_Pos, ClosestPoint) < PhysSize+2.0f)
				{
					if(m_HookedPlayer == -1 || distance(m_HookPos, pCharCore->m_Pos) < Distance)
					{
						m_HookState = HOOK_GRABBED;
						m_HookedPlayer = i;
						Distance = distance(m_HookPos, pCharCore->m_Pos);
					}
				}
			}
		}

		if(m_HookState == HOOK_FLYING)
		{
			// check against ground
			if(GoingToHitGround)
			{
				m_HookState = HOOK_GRABBED;
			}
			else if(GoingToRetract)
			{
				m_HookState = HOOK_RETRACT_START;
			}

			m_HookPos = NewPos;
		}
	}

	if(m_HookState == HOOK_GRABBED)
	{
		if(m_HookedPlayer != -1)
		{
			SCharacter *pCharCore = &s_aClients[m_HookedPlayer].m_Character;
			if(s_aClients[m_HookedPlayer].m_Alive)
				m_HookPos = pCharCore->m_Pos;
			else
			{
				// release hook
				m_HookedPlayer = -1;
				m_HookState = HOOK_RETRACTED;
				m_HookPos = m_Pos;
			}

			// keep players hooked for a max of 1.5sec
			//if(Server()->Tick() > hook_tick+(Server()->TickSpeed()*3)/2)
				//release_hooked();
		}

		// don't do this hook rutine when we are hook to a player
		if(m_HookedPlayer == -1 && distance(m_HookPos, m_Pos) > 46.0f)
		{
			vec2 HookVel = normalize(m_HookPos-m_Pos)*pTuning->m_HookDragAccel;
			// the hook as more power to drag you up then down.
			// this makes it easier to get on top of an platform
			if(HookVel.y > 0)
				HookVel.y *= 0.3f;

			// the hook will boost it's power if the player wants to move
			// in that direction. otherwise it will dampen everything abit
			if((HookVel.x < 0 && m_Direction < 0) || (HookVel.x > 0 && m_Direction > 0))
				HookVel.x *= 0.95f;
			else
				HookVel.x *= 0.75f;

			vec2 NewVel = m_Vel+HookVel;

			// check if we are under the legal limit for the hook
			if(length(NewVel) < pTuning->m_HookDragSpeed || length(NewVel) < length(m_Vel))
				m_Vel = NewVel; // no problem. apply

		}

		// release hook (max hook time is 1.25
		m_HookTick++;
		if(m_HookedPlayer != -1 && (m_HookTick > SERVER_TICK_SPEED+SERVER_TICK_SPEED/5 || !s_aClients[m_HookedPlayer].m_Alive))
		{
			m_HookedPlayer = -1;
			m_HookState = HOOK_RETRACTED;
			m_HookPos = m_Pos;
		}
	}

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(!s_aClients[i].m_Alive)
            continue;
        SCharacter *pCharCore = &s_aClients[i].m_Character;
        if(pCharCore == this)
            continue;

        // handle player <-> player collision
        float Distance = distance(m_Pos, pCharCore->m_Pos);
        vec2 Dir = normalize(m_Pos - pCharCore->m_Pos);
        if(pTuning->m_PlayerCollision && Distance < PhysSize*1.25f && Distance > 0.0f)
        {
            float a = (PhysSize*1.45f - Distance);
            float Velocity = 0.5f;

            // make sure that we don't add excess force by checking the
            // direction against the current velocity. if not zero.
            if (length(m_Vel) > 0.0001)
                Velocity = 1-(dot(normalize(m_Vel), Dir)+1)/2;

            m_Vel += Dir*a*(Velocity*0.75f);
            m_Vel *= 0.85f;
        }

        // handle hook influence
        if(m_HookedPlayer == i && pTuning->m_PlayerHooking)
        {
            if(Distance > PhysSize*1.50f) // TODO: fix tweakable variable
            {
                float Accel = pTuning->m_HookDragAccel * (Distance/pTuning->m_HookLength);
                float DragSpeed = pTuning->m_HookDragSpeed;

                // add force to the hooked player
                pCharCore->m_Vel.x = SaturatedAdd(-DragSpeed, DragSpeed, pCharCore->m_Vel.x, Accel*Dir.x*1.5f);
                pCharCore->m_Vel.y = SaturatedAdd(-DragSpeed, DragSpeed, pCharCore->m_Vel.y, Accel*Dir.y*1.5f);

                // add a little bit force to the guy who has the grip
                m_Vel.x = SaturatedAdd(-DragSpeed, DragSpeed, m_Vel.x, -Accel*Dir.x*0.25f);
                m_Vel.y = SaturatedAdd(-DragSpeed, DragSpeed, m_Vel.y, -Accel*Dir.y*0.25f);
            }
        }
    }

	// clamp the velocity to something sane
	if(length(m_Vel) > 6000)
		m_Vel = normalize(m_Vel) * 6000;

    // move
    float RampValue = VelocityRamp(length(m_Vel)*50, pTuning->m_VelrampStart, pTuning->m_VelrampRange, pTuning->m_VelrampCurvature);

	m_Vel.x = m_Vel.x*RampValue;

	vec2 NewPos = m_Pos;
	MoveBox(&NewPos, &m_Vel, vec2(28.0f, 28.0f), 0);

	m_Vel.x = m_Vel.x*(1.0f/RampValue);

	if(pTuning->m_PlayerCollision)
	{
		// check player collision
		float Distance = distance(m_Pos, NewPos);
		int End = Distance+1;
		vec2 LastPos = m_Pos;
		for(int i = 0; i < End; i++)
		{
			float a = i/Distance;
			vec2 Pos = mix(m_Pos, NewPos, a);
			for(int p = 0; p < MAX_CLIENTS; p++)
			{
                if(!s_aClients[p].m_Alive)
                    continue;
                SCharacter *pCharCore = &s_aClients[p].m_Character;
                if(pCharCore == this)
                    continue;
				float D = distance(Pos, pCharCore->m_Pos);
				if(D < 28.0f && D > 0.0f)
				{
					if(a > 0.0f)
						m_Pos = LastPos;
					else if(distance(NewPos, pCharCore->m_Pos) > D)
						m_Pos = NewPos;
					return;
				}
			}
			LastPos = Pos;
		}
	}

	m_Pos = NewPos;
}

void CSugarcane::InitTwsPart()
{
    s_LocalID = -1;
    s_pMap = nullptr;
    s_pAStar = nullptr;
    s_MapWidth = 0;
    s_MapHeight = 0;
    s_TargetTeam = 0;
    s_pMoveTarget = nullptr;
    s_pTarget = nullptr;
    s_MouseTarget = vec2(0.f, 0.f);
    s_MouseTargetTo = vec2(0.f, 0.f);
}

static float GetWeaponDistance(int Weapon)
{
    switch(Weapon)
    {
        case WEAPON_HAMMER: return 64.f;
        case WEAPON_GUN: return 640.f;
        case WEAPON_SHOTGUN: return 240.f;
        case WEAPON_GRENADE: return 320.f;
        case WEAPON_RIFLE: return 640.f;
        case WEAPON_NINJA: return 192.f;
    }
    return 64.0f;
}

void CSugarcane::InputPrediction()
{
    const int PhysSize = 28;
    vec2 NowPos(s_aClients[s_LocalID].m_Character.m_Pos);

    CNetObj_PlayerInput TempInput;
    memcpy(&TempInput, &s_TickInput, sizeof(CNetObj_PlayerInput));

    CTuningParams *pTuning = DDNet::s_pClient->Tuning();
    s_TickInput.m_TargetX = (int) s_MouseTarget.x;
    s_TickInput.m_TargetY = (int) s_MouseTarget.y;

    auto MoveCursor = [&]()
    {
        int MouseSpeed = random_int(g_MinMouseMoveSpeedPerTick, g_MaxMouseMoveSpeedPerTick);
        if(distance(s_MouseTargetTo, s_MouseTarget) > MouseSpeed * 3)
        {
            vec2 Direction = normalize(s_MouseTargetTo - s_MouseTarget);
            s_MouseTarget += Direction * MouseSpeed;
            s_MouseTarget = normalize(s_MouseTarget) * clamp(length(s_MouseTarget), 0.f, 400.f);
        }
        return;
    };

    bool TargetHook = false;
    auto Move = [&]()
    {
        if(!s_pAStar)
        {
            return;
        }

        std::vector<std::pair<int, int>> Path = s_pAStar->findPath({NowPos.y / 32, (NowPos.x + PhysSize / 2) / 32}, 20);

        if(Path.empty())
        {
            return;
        }

        int MoveX = Path[0].second;
        int MoveY = Path[0].first;
        s_TickInput.m_Direction = MoveX;

        bool Grounded = false;
        if(CheckPoint(NowPos.x + PhysSize / 2, NowPos.y + PhysSize / 2 + 5))
            Grounded = true;
        if(CheckPoint(NowPos.x - PhysSize / 2, NowPos.y + PhysSize / 2 + 5))
            Grounded = true;

        if(MoveY < 0)
        {
            if(s_aClients[s_LocalID].m_Character.m_Vel.y > -pTuning->m_GroundJumpImpulse / 2.f)
            {
                if(Grounded)
                {
                    s_TickInput.m_Jump = 1;
                    s_aClients[s_LocalID].m_Character.m_Vel.y = -pTuning->m_GroundJumpImpulse;
                    s_aClients[s_LocalID].m_Character.m_Jumped |= 1;
                }
                else if(!(s_aClients[s_LocalID].m_Character.m_Jumped & 2))
                {
                    s_TickInput.m_Jump = 1;
                    s_aClients[s_LocalID].m_Character.m_Vel.y = -pTuning->m_AirJumpImpulse;
                    s_aClients[s_LocalID].m_Character.m_Jumped |= 3;
                }
            }
            if(!s_TickInput.m_Jump)
            {
                TargetHook = true;
                s_TickInput.m_Hook = 1;
                s_TickInput.m_Direction = (int) -sign(s_MouseTargetTo.x);
            }
        }
    };

    Move();

    if(s_pTarget && s_pTarget->m_Character.m_Emote != EMOTE_PAIN)
    {
        int ActiveWeapon = s_aClients[s_LocalID].m_Character.m_Weapon;
        if(!TargetHook)
        {
            s_MouseTargetTo = s_pTarget->m_Character.m_Pos - NowPos;
            ESMapItems Hit = IntersectLine(NowPos, s_pTarget->m_Character.m_Pos, nullptr, nullptr);
            if(!(Hit & ESMapItems::TILEFLAG_SOLID) && distance(s_pTarget->m_Character.m_Pos, NowPos) < GetWeaponDistance(ActiveWeapon) && distance(normalize(s_MouseTarget), normalize(s_MouseTargetTo)) < 0.5f)
            {
                s_TickInput.m_Fire = !s_LastInput.m_Fire;
            }
        }
    }
    MoveCursor();
}

void CSugarcane::TwsResponseBack(string Response)
{
    CNetMsg_Cl_Say Msg;
    Msg.m_pMessage = Response.c_str();
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
            s_aClients[ClientID].m_Character = *pObj;
            s_aClients[ClientID].m_Character.m_Tick = s_aClients[ClientID].m_Character.m_LastSnapshotTick = pObj->m_Tick;
            if(pObj->m_Tick) 
                for(int64_t& Tick = s_aClients[ClientID].m_Character.m_Tick; Tick < DDNet::s_pClient->GameTick(); Tick++)
                    s_aClients[ClientID].m_Character.Prediction();
            s_aClients[ClientID].m_Alive = true;
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

        s_LastTeamChangeTime = std::chrono::system_clock::now();
        CNetMsg_Cl_Say Msg;
        Msg.m_pMessage = "/alwaysrandom 1";
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

        if(pMsg->m_ClientID >= 0 && pMsg->m_ClientID < MAX_CLIENTS && s_aClients[pMsg->m_ClientID].m_Active && string(pMsg->m_pMessage).startswith(s_aClients[s_LocalID].m_aName))
        {
            BackResponse(std::format("Game-Chat|{}: {}", s_aClients[pMsg->m_ClientID].m_aName, pMsg->m_pMessage + str_length(s_aClients[s_LocalID].m_aName)).c_str(), TwsResponseBack);
        }
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
        if(!Client.m_Active)
            continue;
        if(Client.m_ClientID != s_LocalID && Client.m_Team != TEAM_SPECTATORS)
            OtherPlayersCount++;
    }

    if(OtherPlayersCount)
    {
        if(s_aClients[s_LocalID].m_Team == TEAM_SPECTATORS)
        {
            s_TargetTeam = TEAM_RED;
        }
    }
    else if(s_LastTeamChangeTime + std::chrono::seconds(5) < std::chrono::system_clock::now())
    {
        s_TargetTeam = TEAM_SPECTATORS;
    }

    if(s_TargetTeam != s_aClients[s_LocalID].m_Team)
    {
        CNetMsg_Cl_SetTeam Msg;
        Msg.m_Team = s_TargetTeam;
        DDNet::s_pClient->SendPackMsg(&Msg, MSGFLAG_VITAL);

        s_LastTeamChangeTime = std::chrono::system_clock::now();
        log_msg("sugarcane/tws", "send switch team msg");
    }

    if(s_aClients[s_LocalID].m_Alive)
    {
        vec2 NowPos(s_aClients[s_LocalID].m_Character.m_Pos);

        // find target
        if(s_pTarget)
            if(!s_pTarget->m_Active || !s_pTarget->m_Alive || !IsOtherTeam(s_pTarget->m_ClientID))
                s_pTarget = nullptr;
        if(s_pMoveTarget)
            if(!s_pMoveTarget->m_Active || !s_pMoveTarget->m_Alive)
                s_pMoveTarget = nullptr;

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

        bool SearchNewTeammate = !s_pMoveTarget || s_LastFindTeammate + std::chrono::seconds(7) < std::chrono::system_clock::now();
        bool SearchStronghold = s_LastStrongholdFindTime + std::chrono::seconds(20) < std::chrono::system_clock::now();
        std::vector<std::pair<int, vec2>> vStrongholds;
        for(auto& Client : s_aClients)
        {
            if(Client.m_Active && Client.m_Alive && Client.m_ClientID != s_LocalID)
            {
                vec2 Pos = vec2(Client.m_Character.m_Pos);
                float Distance = distance(Pos, NowPos);
                Distance -= (IsOtherTeam(Client.m_ClientID) ? DDNet::s_pClient->GameTick() - Client.m_Character.m_LastSnapshotTick : Client.m_Character.m_LastSnapshotTick - DDNet::s_pClient->GameTick()) * 30.0f;
                if(IsOtherTeam(Client.m_ClientID))
                    Distance -= 480.0f;

                if(SearchStronghold && !SelfInfect)
                {
                    bool FindNearest = false;
                    for(auto& Stronghold : vStrongholds)
                    {
                        if(distance(Pos, Stronghold.second) < 320.f)
                        {
                            FindNearest = true;
                            Stronghold.first++;
                            break;
                        }
                    }
                    if(!FindNearest)
                    {
                        vStrongholds.push_back(std::make_pair(1, Client.m_Character.m_Pos));
                    }
                }

                if(Distance < ClosetDistance)
                {
                    ClosetDistance = Distance;
                    if(!IsOtherTeam(Client.m_ClientID))
                    {
                        if(SearchNewTeammate)
                            s_pMoveTarget = &Client;
                    }
                    else
                    {
                        s_pTarget = &Client;
                    }
                }
            }
        }
        if(SearchNewTeammate)
            s_LastFindTeammate = std::chrono::system_clock::now();
        if(SearchStronghold)
        {
            for(auto& Stronghold : vStrongholds)
            {
                if(Stronghold.first >= 3)
                {
                    s_MapDetail.SaveStronghold(Stronghold.second);
                    log_msgf("sugarcane/game", "找到新的据点 {},{}", Stronghold.second.x, Stronghold.second.y);
                }
            }

            vec2 *pFindPos = nullptr;
            s_MapDetail.FindNearestStronghold(NowPos, &pFindPos);
            if(pFindPos && distance(*pFindPos, NowPos) > 480.0f)
            {
                s_FindStronghold = true;
                s_StrongholdPos = *pFindPos;
                s_pMoveTarget = nullptr;
            }
            s_LastStrongholdFindTime = std::chrono::system_clock::now();
        }

        if(SelfInfect && s_pTarget)
            s_pMoveTarget = s_pTarget;

        if(s_pMoveTarget)
        {
            s_GoToPos = s_pMoveTarget->m_Character.m_Pos;
        }
        else if(s_FindStronghold)
        {
            s_GoToPos = s_StrongholdPos;
        }
        if(s_pAStar)
            delete s_pAStar;

        s_pAStar = new AStar(s_MapGridWithEntity, {s_GoToPos.y / 32, s_GoToPos.x / 32});
        s_MouseTargetTo =  normalize(s_GoToPos - NowPos) * clamp(distance(s_GoToPos, NowPos), 0.f, 400.f);

        if(s_pTarget)
        {
            s_MouseTargetTo = s_pTarget->m_Character.m_Pos - NowPos;
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
        if(!Client.m_Active)
            continue;
        if(!DDNet::s_pClient->SnapFindItem(IClient::SNAP_PREV, NETOBJTYPE_CHARACTER, Client.m_ClientID) || 
            !DDNet::s_pClient->SnapFindItem(IClient::SNAP_PREV, NETOBJTYPE_PLAYERINFO, Client.m_ClientID))
            Client.m_Alive = false;
        if(!DDNet::s_pClient->SnapFindItem(IClient::SNAP_PREV, NETOBJTYPE_PLAYERINFO, Client.m_ClientID))
            Client.m_Active = false;
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

    s_MapDetail.Reset();
    s_FindStronghold = false;

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
