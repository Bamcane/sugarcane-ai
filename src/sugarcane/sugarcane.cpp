#include <include/base.h>
#include <teeworlds/six/main.h>
#include <teeworlds/six/generated_protocol.h>
#include <teeworlds/six/iclient.h>

#include <base/storage.h>
#include "sugarcane.h"

#include <csignal>

SInformation s_aInformations[] = {
	{"AL-1S", "Tendou Arisu", "AL-1S", "天童爱丽丝", "MSS-GDD", {"呜呜...请救救...爱丽丝", "呜呜...请救救...爱丽丝\"{}\""}},
	{"S-SC", "Stable Sugarcane", "stablesugarcane-ai", "稳蔗", "Mid·Night", {"...请和甘箨说我的AI出现了一些故障...", "...\"{}\""}},
	{"T-SC", "Test Sugarcane", "testsugarcane-ai", "遣蔗", "Mid·Night", {"请告诉甘箨我的AI出问题啦!", "请告诉甘箨这些!: \"{}\""}}
};

void HandleSigIntTerm(int Param)
{
	// Exit the next time a signal is received
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);

    DDNet::Disconnect();
}

void CSugarcane::Init(IStorage *pStorage, int argc, const char **argv)
{
	m_pStorage = pStorage;

	srand(time(0));

	{
		bool FindInformation = false;
		if(argc > 1)
		{
			auto Iter = std::find_if(std::begin(s_aInformations), std::end(s_aInformations), 
				[argv](const SInformation& Info) -> bool
				{
					return strcmp(Info.m_aID, (const char *) argv[1]) == 0;
				});
			if(Iter != std::end(s_aInformations))
			{
				m_pInformation = Iter;
				FindInformation = true;
			}
		}
		if(!FindInformation)
			m_pInformation = &s_aInformations[0];
		log_msgf("sugarcane", "choose \"{}\" as information", m_pInformation->m_aFullName);
	}

	InitTwsPart();
	InitTalkPart();
}

#define BAMCANE_AISERVER "sh.teemidnight.online"
#define CR_SERVER "81.70.102.43:8303"
void CSugarcane::Run()
{
	signal(SIGINT, HandleSigIntTerm);
	signal(SIGTERM, HandleSigIntTerm);

    DDNet::ConnectTo(CR_SERVER, this);
}

ISugarcane *CreateSugarcane() { return new CSugarcane(); }
