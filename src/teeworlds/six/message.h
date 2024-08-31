/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef TEEWORLDS_SIX_MESSAGE_H
#define TEEWORLDS_SIX_MESSAGE_H

#include "packer.h"

class CMsgPacker : public CPacker
{
public:
	CMsgPacker(int Type)
	{
		Reset();
		AddInt(Type);
	}
};

#endif
