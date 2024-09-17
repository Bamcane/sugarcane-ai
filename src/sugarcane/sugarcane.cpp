#include <include/base.h>
#include <teeworlds/six/main.h>
#include <teeworlds/six/generated_protocol.h>
#include <teeworlds/six/iclient.h>

#include <base/storage.h>
#include "sugarcane.h"

#include <csignal>

void HandleSigIntTerm(int Param)
{
	// Exit the next time a signal is received
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);

    DDNet::Disconnect();
}

void CSugarcane::Init(IStorage *pStorage)
{
	m_pStorage = pStorage;

	InitTwsPart();
	InitTalkPart();
}

#define BAMCANE_AISERVER "sh.teemidnight.online"
#define CR_SERVER "81.70.102.43:8303"
void CSugarcane::Run()
{
	signal(SIGINT, HandleSigIntTerm);
	signal(SIGTERM, HandleSigIntTerm);

    DDNet::ConnectTo(BAMCANE_AISERVER, this);
}

ISugarcane *CreateSugarcane() { return new CSugarcane(); }