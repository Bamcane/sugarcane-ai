#ifndef SUGARCANE_SUGARCANE_H
#define SUGARCANE_SUGARCANE_H

#include <base/sugarcane.h>

class CSugarcane : public ISugarcane
{
private:
    IStorage *m_pStorage;

    void DoInput(int *pInputData);

    /* talk part */
    typedef void (*TALKBACK_FUNCTION)(string Talk);

    void InitTalkPart();
    void BackTalk(const char *pFrom, const char *pMessage, TALKBACK_FUNCTION Function);

    /* teeworlds */
    void InitTwsPart();
    void InputPrediction();

    static void TwsTalkBack(string Talk);

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