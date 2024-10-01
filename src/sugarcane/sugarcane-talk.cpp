#include <include/base.h>
#include <include/external/ollama.hpp>

#include <base/storage.h>

#include "sugarcane.h"

#include <random>
#include <thread>
#include <vector>

void CSugarcane::InitTalkPart()
{
    try
    {
    // preload
    ollama::load_model("sugarcane-ai");
    }
    catch(std::exception &e)
    {
        log_msgf("sugarcane/talk", "preload error: {}", e.what());
    }
}

void CSugarcane::BackTalk(const char *pFrom, const char *pMessage, TALKBACK_FUNCTION Function)
{
    std::thread([Function](string From, string Message) -> void
    {
        try
        {
            ollama::message Request("user", std::format("{}和你说:\"{}\"，你该怎么回答", From.c_str(), Message.c_str()));
            ollama::response Response = ollama::chat("sugarcane-ai", Request);
            if(Response.has_error())
            {
                Function("请告诉甘箨我的AI出问题啦!QAQ!");
            }
            else
            {
                Function(Response.as_simple_string().c_str());
            }
        }
        catch(std::exception &e)
        {
            Function(std::format("请告诉甘箨: \"{}\" QAQ!", e.what()).c_str());
        }
    }, pFrom, pMessage).detach();
}