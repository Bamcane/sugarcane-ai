#include <include/base.h>
#include <include/external/ollama.hpp>

#include <base/storage.h>

#include "sugarcane.h"

#include <queue>

typedef void (*REQUEST_FUNCTION)(CSugarcane::BACK_FUNCTION Function, SInformation *pInformation, string RequestStr);

struct SRequest
{
    REQUEST_FUNCTION m_Function;

    CSugarcane::BACK_FUNCTION m_BackFunction;
    SInformation *m_pInformation;
    string m_RequestStr;
    std::chrono::system_clock::time_point m_CallTime;
    std::chrono::seconds m_Timeout;
};

static Ollama s_Server("http://192.168.31.11:11434");
static std::queue<SRequest> s_qRequests;
static bool s_TalkEnabled;

void CSugarcane::InitTalkPart()
{
    try
    {
        // preload
        s_Server.load_model(m_pInformation->m_aChatModel);
        s_TalkEnabled = true;
    }
    catch(std::exception &e)
    {
        log_msgf("sugarcane/talk", "preload error: {}", e.what());
        s_TalkEnabled = false;
    }
}

static bool s_Working = false;
void CSugarcane::RequestLoop()
{
    if(!s_TalkEnabled)
        return;
    // work until nothing to do
    std::thread([&]()
    {
        if(m_Shutdown)
            return;
        if(s_Working)
            return;

        s_Working = true;
        while(!s_qRequests.empty())
        {
            if(m_Shutdown)
                return;
            SRequest Request = s_qRequests.front();
            s_qRequests.pop();
            if(Request.m_CallTime + Request.m_Timeout < std::chrono::system_clock::now())
                continue;
            Request.m_Function(Request.m_BackFunction, Request.m_pInformation, Request.m_RequestStr);
        }
        s_Working = false;
    }).detach();
}

void CSugarcane::BackResponse(string Request, BACK_FUNCTION Function, int Timeout)
{
    if(!s_TalkEnabled)
        return;

    s_qRequests.push(SRequest{[](BACK_FUNCTION Function, SInformation *pInformation, string RequestStr) -> void
    {
        try
        {
            ollama::message Request("user", RequestStr.c_str());
            ollama::response Response = s_Server.chat(pInformation->m_aChatModel, Request);
            if(Response.has_error())
            {
                Function(std::format("/say {}", pInformation->m_ErrorMessage[0]).c_str());
            }
            else
            {
                Function(Response.as_simple_string().c_str());
            }
        }
        catch(std::exception &e)
        {
            const char *pErrorMessage = e.what();
            std::string Buffer = std::vformat(std::string_view(pInformation->m_ErrorMessage[1]), std::make_format_args(pErrorMessage));
            Function(std::format("/say {}", Buffer).c_str());
        }
    }, Function, m_pInformation, Request, std::chrono::system_clock::now(), std::chrono::seconds(Timeout)});
    if(!s_Working)
        RequestLoop();
}