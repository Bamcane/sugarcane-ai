#ifndef BASE_SUGARCANE_H
#define BASE_SUGARCANE_H

struct SInformation 
{
    char m_aID[8]; // id to type
    char m_aFullName[32];
    char m_aChatModel[64];
    char m_aChatModelAPIKey[64];
    // TWS
    char m_aGameName[16];
    char m_aClan[12];
    // Talk
    char m_ErrorMessage[2][128];
};

class ISugarcane
{
public:
    virtual struct SInformation *Information() = 0;

    virtual void Init(class IStorage *pStorage, int argc, const char **argv) = 0;
    virtual void Run() = 0;

    /* teeworlds */
    virtual void OnNewSnapshot(void *pItem, const void *pData) = 0;
    virtual void RecvDDNetMsg(int MsgID, void *pData) = 0;
    virtual void DDNetTick(int *pInputData) = 0;
    virtual void StartSnap() = 0; 

    virtual bool DownloadMap(const char *pMap, int Crc, void* pData, int Size) = 0;
    virtual bool CheckMap(const char *pMap, int Crc) = 0;
    virtual bool LoadMap(const char *pMap, int Crc) = 0;
    virtual bool NeedSendInput() = 0;
};

extern ISugarcane *CreateSugarcane();

#endif // BASE_SUGARCANE_H