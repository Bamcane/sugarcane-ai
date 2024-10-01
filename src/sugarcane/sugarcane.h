#ifndef SUGARCANE_SUGARCANE_H
#define SUGARCANE_SUGARCANE_H

#include <base/sugarcane.h>

class CSugarcane : public ISugarcane
{
    IStorage *m_pStorage;

    void DoInput(int *pInputData);
private:
    /* talk part */
    typedef void (*TALKBACK_FUNCTION)(string Talk);

    void InitTalkPart();
    void BackTalk(const char *pFrom, const char *pMessage, TALKBACK_FUNCTION Function);

private:
    /* teeworlds */
    void InitTwsPart();
    void InputPrediction();

    static void TwsTalkBack(string Talk);
public:
    IStorage *Storage() { return m_pStorage; }

    void Init(IStorage *pStorage) override;

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