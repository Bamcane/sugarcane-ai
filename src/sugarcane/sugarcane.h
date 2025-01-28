#ifndef SUGARCANE_SUGARCANE_H
#define SUGARCANE_SUGARCANE_H

#include <base/sugarcane.h>

class CSugarcane : public ISugarcane
{
public:
    /* talk part */
    typedef void (*BACK_FUNCTION)(string Response);
private:
    bool m_Shutdown;
    IStorage *m_pStorage;

    void DoInput(int *pInputData);

    void InitTalkPart();
    void RequestLoop();
    void BackResponse(string Request, BACK_FUNCTION Function, int Timeout = 30);

    /* teeworlds */
    void InitTwsPart();
    void InputPrediction();

    static void TwsResponseBack(string Response);

    SInformation *m_pInformation;
public:
    SInformation *Information() { return m_pInformation; }
    IStorage *Storage() { return m_pStorage; }

    void Init(IStorage *pStorage, int argc, const char **argv) override;

    void Run() override;

    /* teeworlds */
    void OnNewSnapshot(void *pItem, const void *pData) override;
    void RecvDDNetMsg(int MsgID, void *pData) override;
    void DDNetTick(int *pInputData) override;
    void StartSnap() override;

    bool DownloadMap(const char *pMap, int Crc, void* pData, int Size) override;
    bool CheckMap(const char *pMap, int Crc) override;
    bool LoadMap(const char *pMap, int Crc) override;
    bool NeedSendInput() override;
};

#endif // SUGARCANE_SUGARCANE_H