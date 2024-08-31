#include <new>

#include "client.h"
#include "main.h"
#include "system.h"

bool IntsToStr(const int *pInts, size_t NumInts, char *pStr, size_t StrSize)
{
	dbg_assert(NumInts > 0, "IntsToStr: NumInts invalid");
	dbg_assert(StrSize >= NumInts * sizeof(int), "IntsToStr: StrSize invalid");

	// Unpack string without validation
	size_t StrIndex = 0;
	for(size_t IntIndex = 0; IntIndex < NumInts; IntIndex++)
	{
		const int CurrentInt = pInts[IntIndex];
		pStr[StrIndex] = ((CurrentInt >> 24) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = ((CurrentInt >> 16) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = ((CurrentInt >> 8) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = (CurrentInt & 0xff) - 128;
		StrIndex++;
	}
	// Ensure null-termination
	pStr[StrIndex - 1] = '\0';

	// Ensure valid UTF-8
	if(str_utf8_check(pStr))
	{
		return true;
	}
	pStr[0] = '\0';
	return false;
}

namespace DDNet
{
	CClient *s_pClient = nullptr;

	static CClient *CreateClient()
	{
		CClient *pClient = static_cast<CClient *>(mem_alloc(sizeof(CClient), 1));
		s_pClient = pClient;

		mem_zero(pClient, sizeof(CClient));
		return new(pClient) CClient;
	}

	void ConnectTo(string Address, ISugarcane *pSugarcane)
	{
		if(secure_random_init() != 0)
		{
			log_msg("secure", "could not initialize secure RNG");
			return;
		}

		CClient *pClient = CreateClient();
		pClient->SetSugarcane(pSugarcane);

		IKernel *pKernel = IKernel::Create();
		pKernel->RegisterInterface(pClient);
		pClient->RegisterInterfaces();

		IEngine *pEngine = CreateEngine("DDNet");
		pEngine->Init();
		pClient->InitInterfaces();

		str_copy(pClient->m_aCmdConnect, Address, sizeof(pClient->m_aCmdConnect));
		pClient->Run();

		return;
	}

	void Disconnect()
	{
		s_pClient->NeedDisconnect();
	}
}