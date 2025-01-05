#ifndef BASE_SUGARCANE_H
#define BASE_SUGARCANE_H

class ISugarcane
{
public:
    virtual void Init(class IStorage *pStorage) = 0;
    virtual void Run() = 0;

    virtual const char *GetName() = 0;
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