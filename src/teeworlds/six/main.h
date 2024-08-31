#ifndef MAIN_H
#define MAIN_H

#include <lib/base.h>
#include "protocol.h"

class ISugarcane;
class CClient;

bool IntsToStr(const int *pInts, size_t NumInts, char *pStr, size_t StrSize);

namespace DDNet
{
    void ConnectTo(string Address, ISugarcane *pSugarcane);
    void Disconnect();

    extern CClient *s_pClient;
};

#endif