#include <include/base.h>
#include <include/external/ollama.hpp>

#include <base/storage.h>

#include "sugarcane.h"

static Ollama s_Server("http://192.168.31.11:11434");

void CSugarcane::InitTalkPart()
{
    try
    {
        // preload
        s_Server.load_model(m_pInformation->m_aChatModel);
    }
    catch(std::exception &e)
    {
        log_msgf("sugarcane/talk", "preload error: {}", e.what());
    }
}

void CSugarcane::BackTalk(const char *pFrom, const char *pMessage, TALKBACK_FUNCTION Function)
{
    SInformation *pInformation = m_pInformation;
    std::thread([Function, pInformation](string From, string Message) -> void
    {
        try
        {
            ollama::message Request("user", std::format("Game-Chat|{}: {}", From.c_str(), Message.c_str()));
            ollama::response Response = s_Server.chat(pInformation->m_aChatModel, Request);
            if(Response.has_error())
            {
                Function(pInformation->m_ErrorMessage[0]);
            }
            else
            {
                if(Response.as_simple_string().starts_with("/say "))
                    Function(Response.as_simple_string().substr(strlen("/say")).c_str());
            }
        }
        catch(std::exception &e)
        {
            const char *pErrorMessage = e.what();
            Function(std::vformat(std::string_view(pInformation->m_ErrorMessage[1]), std::make_format_args(pErrorMessage)).c_str());
        }
    }, pFrom, pMessage).detach();
}