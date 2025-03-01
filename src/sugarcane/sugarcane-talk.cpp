#include <include/base.h>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <include/httplib/httplib.h>
#include <include/json/json.hpp>

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

static std::queue<SRequest> s_qRequests;
static bool s_TalkEnabled;

void CSugarcane::InitTalkPart()
{
    s_TalkEnabled = (m_pInformation->m_aChatModelAPIKey[0] != '\0');
    /*
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
    */
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
            std::this_thread::sleep_for(std::chrono::seconds(2));
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
            std::string TargetLink = std::format("/api/v1/apps/{}/completion", pInformation->m_aChatModel);
            std::string Authorization = std::format("Bearer {}", pInformation->m_aChatModelAPIKey);
            httplib::Client Request("https://dashscope.aliyuncs.com");
            httplib::Headers Headers = 
            {
                {"Authorization", Authorization}
            };

            nlohmann::json RequestData =
            {
                {
                    "input", 
                    {
                        {
                            "prompt", RequestStr.c_str()
                        }
                    }
                },
                {
                    "parameters",
                    nlohmann::json({})
                },
                {
                    "debug",
                    nlohmann::json({})
                }
            };
            Request.enable_server_certificate_verification(false);
            Request.enable_server_hostname_verification(false);
            Request.set_connection_timeout(10);
            Request.set_read_timeout(10);
            std::string RequestStringData = RequestData.dump(4);
            if(auto Response = Request.Post(TargetLink, Headers, RequestStringData, "application/json"))
            {
                if(Response->status == httplib::StatusCode::OK_200)
                {
                    nlohmann::json Json = nlohmann::json::parse(Response->body);
                    string ResponseStr = Json["output"]["text"].get<std::string>().c_str();
                    Function(ResponseStr);
                }
                else
                {
                    log_msgf("sugarcane/talk", "{}", Response->body);
                }
            }
            else
            {
                std::string ErrorMessage = httplib::to_string(Response.error());
                std::string Buffer = std::vformat(std::string_view(pInformation->m_ErrorMessage[1]), std::make_format_args(ErrorMessage));
                Function(std::format("{}", Buffer).c_str());
                log_msgf("sugarcane/talk", "Request: {}", RequestStringData);
            }
        }
        catch(std::exception &e)
        {
            const char *pErrorMessage = e.what();
            std::string Buffer = std::vformat(std::string_view(pInformation->m_ErrorMessage[1]), std::make_format_args(pErrorMessage));
            Function(std::format("{}", Buffer).c_str());
        }
    }, Function, m_pInformation, Request, std::chrono::system_clock::now(), std::chrono::seconds(Timeout)});
    if(!s_Working)
        RequestLoop();
}