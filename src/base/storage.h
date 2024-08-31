#ifndef BASE_STORAGE_H
#define BASE_STORAGE_H

#include <lib/base.h>

class IFileReader
{
public:
    virtual bool ReadLine(string& Line) = 0;
    virtual bool ReadFile(string& Buffer, size_t Size) = 0;
    virtual void SetPos(size_t Pos) = 0;
    virtual void Close() = 0;
};

class IStorage
{
public:
    virtual void Init() = 0;
    virtual void WriteFile(string Dir, string File, string Extension, void *pData, bool Rewrite = false) = 0;

    virtual bool FileExists(string Dir, string File, string Extension) = 0;

    virtual IFileReader *ReadFile(string Dir, string File, string Extension) = 0;
    virtual IFileReader *ReadFile(string Path) = 0;
    
    /* teeworlds */
    virtual IFileReader *ReadMap(string Map, string MapCrc) = 0;
    virtual bool TwsMapExists(string Map, string MapCrc) = 0;
    virtual bool TwsDownloadMap(string Map, string MapCrc, void* pData, int Size) = 0;
};

extern IStorage *CreateStorage();

#endif // BASE_STORAGE_H