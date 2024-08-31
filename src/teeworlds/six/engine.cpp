/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "system.h"
#include "engine.h"

#include "network.h"


class CEngine : public IEngine
{
public:
	CEngine(const char *pAppname)
	{
		// init the network
		net_init();
		CNetBase::Init();
	}

	void Init()
	{
	}

	void InitLogfile()
	{
	}
};

IEngine *CreateEngine(const char *pAppname) { return new CEngine(pAppname); }
