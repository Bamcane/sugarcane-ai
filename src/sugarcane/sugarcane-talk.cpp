#include <lib/base.h>
#include <base/storage.h>
#include <rapidfuzz/fuzz.hpp>

#include "sugarcane.h"

#include <random>
#include <thread>
#include <vector>

struct STalkData
{
    std::vector<string> m_Froms;
    std::vector<string> m_Responses;
};

static std::vector<STalkData> s_TalkDatas;
void CSugarcane::InitTalkPart()
{
    m_TalkPartInit = false;

    std::thread([&]()
    {
        IFileReader *pFileReader = Storage()->ReadFile("data", "talk", "data");
        if(!pFileReader)
        {
            log_msg("sugarcane/talk", "initialization failed");
            return;
        }

        string Line;
        STalkData TempData;
        while(pFileReader->ReadLine(Line))
        {
            if(Line.startswith("=="))
            {
                string& From = TempData.m_Froms.emplace_back(string(Line.substr(3)));
            }
            else if(Line.startswith("##"))
            {
                string& Response = TempData.m_Responses.emplace_back(string(Line.substr(3)));
            }
            else if(Line.startswith("$$$$"))
            {
                if(!TempData.m_Froms.empty() && !TempData.m_Responses.empty())
                    s_TalkDatas.push_back(TempData);
                TempData.m_Froms.clear();
                TempData.m_Responses.clear();
            }
        }

        pFileReader->Close();

        if(!TempData.m_Froms.empty() && !TempData.m_Responses.empty())
            s_TalkDatas.push_back(TempData);

        m_TalkPartInit = true;
        log_msg("sugarcane/talk", "initialization success");
    }).detach();
}

static std::random_device s_RandomDevice;
static std::default_random_engine s_RandomEngine(s_RandomDevice());
int random_int(int Min, int Max)
{
    std::uniform_int_distribution<int> Distribution(Min, Max);
    return Distribution(s_RandomEngine);
}

void CSugarcane::BackTalk(const char *pFrom, TALKBACK_FUNCTION Function)
{
    if(!m_TalkPartInit)
        return;
    STalkData *pBestComp = nullptr;
    double BestScore = 0.0f;
    for(auto& Data : s_TalkDatas)
    {
        for(auto& DataFrom : Data.m_Froms)
        {
            double Score = rapidfuzz::fuzz::token_sort_ratio(pFrom, DataFrom.c_str());
            if(Score > BestScore)
            {
                pBestComp = &Data;
                BestScore = Score;
            }
        }
    }

    if(BestScore < 64.0f)
    {
        Function("诶qwwwww, 显然我的CPU没法处理....");
        return;
    }
    if(pBestComp->m_Responses.empty())
        return;

    Function(pBestComp->m_Responses[random_int(0, pBestComp->m_Responses.size() - 1)]);
}