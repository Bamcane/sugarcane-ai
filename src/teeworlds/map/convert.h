#ifndef TEEWORLDS_MAP_CONVERT_H
#define TEEWORLDS_MAP_CONVERT_H

#include <include/string.h>
#include <cstdint>

enum class ESMapItems : int32_t
{
	TILEFLAG_AIR = 0,
    TILEFLAG_SOLID = 1 << 0,
    TILEFLAG_DEATH = 1 << 1,
	TILEFLAG_UNHOOKABLE = 1 << 2,
    TILEFLAG_INFECTION = 1 << 3,
};

inline ESMapItems operator|(const ESMapItems& A, const ESMapItems& B)
{
	return static_cast<ESMapItems>(static_cast<int32_t>(A) | static_cast<int32_t>(B));
}

inline bool operator&(const ESMapItems& A, const ESMapItems& B)
{
	return static_cast<int32_t>(A) & static_cast<int32_t>(B);
}

bool ConvertMap(string Map, string Crc, ESMapItems **ppResult, int& Width, int& Height);

#endif // TEEWORLDS_MAP_CONVERT_H