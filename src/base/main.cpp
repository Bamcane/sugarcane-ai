#include <include/base.h>

#include "storage.h"
#include "sugarcane.h"

int main(int argc, const char **argv)
{
    IStorage *pStorage = CreateStorage();
    ISugarcane *pSugarcane = CreateSugarcane();

    pStorage->Init();
    pSugarcane->Init(pStorage);

    log_msg("main", "start running...");
    pSugarcane->Run();
    log_msg("main", "shutdown...");

    delete pStorage;

    return 0;
}