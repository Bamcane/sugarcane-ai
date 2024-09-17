#include <include/base.h>
#include <filesystem>
#include <fstream>

#include "storage.h"

class CFileReader : public IFileReader
{
    std::fstream m_File;
public:
    CFileReader() = default;

    bool Open(std::filesystem::path Path)
    {
        m_File.open(Path, std::ios::in);
        m_File.seekg(0UL);

        return m_File.is_open();
    }

    bool ReadLine(string& Line) override
    {
        char aBuf[128];
        if(!m_File.getline(aBuf, sizeof(aBuf)))
            return false;

        Line.clear();
        Line.copy(aBuf);
        return true;
    }

    bool ReadFile(string& Buffer, size_t Size) override
    {
        char *pBuf = new char[Size + 1];
        if(!m_File.get(pBuf, Size * sizeof(char)))
        {
            delete[] pBuf;
            return false;
        }
        pBuf[Size] = 0;

        Buffer.append(pBuf);
        delete[] pBuf;
        return true;
    }

    void SetPos(size_t Pos) override
    {
        m_File.seekg(Pos);
    }

    void Close() override
    {
        m_File.close();
        // auto destroy
        delete this;
    }
};

class CStorage : public IStorage
{
    std::filesystem::path m_CurrentPath;
public:
    CStorage() 
    {
        m_CurrentPath.clear();
    }

    void Init() override
    {
        m_CurrentPath = std::filesystem::current_path();
        log_msgf("storage", "init current path: {}", m_CurrentPath.c_str());

        std::filesystem::path Path = m_CurrentPath;
        Path.append("tws-maps");
        if(!std::filesystem::exists(Path))
            std::filesystem::create_directory(Path);
    }

    void WriteFile(string Dir, string File, string Extension, void *pData, bool Rewrite) override
    {
        std::filesystem::path Path = m_CurrentPath;
        Path.append(Dir.c_str());
        Path.append(File.c_str());
        Path.concat(".");
        Path.concat(Extension.c_str());

        std::filesystem::path DirPath(Dir.c_str());
        if(!std::filesystem::exists(DirPath))
            std::filesystem::create_directory(DirPath);

        std::ofstream WriteFile;
        WriteFile.open(Path, Rewrite ? (std::ios::out | std::ios::trunc) : std::ios::out);
        if(!WriteFile)
        {
            log_msgf("storage", "failed open file '{}'", Path.c_str());
            return;
        }
        WriteFile << (const unsigned char*) pData;
        WriteFile.close();
    }

    bool FileExists(string Dir, string File, string Extension) override
    {
        std::filesystem::path Path = m_CurrentPath;
        Path.append(Dir.c_str());
        Path.append(File.c_str());
        Path.concat(".");
        Path.concat(Extension.c_str());
        return std::filesystem::exists(Path);
    }

    IFileReader *ReadFile(string Dir, string File, string Extension) override
    {
        std::filesystem::path Path = m_CurrentPath;
        Path.append(Dir.c_str());
        Path.append(File.c_str());
        Path.concat(".");
        Path.concat(Extension.c_str());

        return ReadFile(Path.c_str());
    }

    IFileReader *ReadFile(string Path) override
    {
        CFileReader *pFileReader = new CFileReader();
        if(!pFileReader->Open(Path.c_str()))
        {
            log_msgf("storage", "failed open file '{}'", Path.c_str());

            delete pFileReader;
            return nullptr;
        }
        return pFileReader;
    }

    /* teeworlds */
    IFileReader *ReadMap(string Map, string MapCrc) override
    {
        std::filesystem::path Path = m_CurrentPath;
        Path.append("tws-maps");
        Path.append(Map.c_str());
        Path.append(MapCrc.c_str());
        Path.concat(".map");

        return ReadFile(Path.c_str());
    }

    bool TwsMapExists(string Map, string MapCrc) override
    {
        std::filesystem::path Path = m_CurrentPath;
        Path.append("tws-maps");
        Path.append(Map.c_str());
        Path.append(MapCrc.c_str());
        Path.concat(".map");

        std::filesystem::path DirPath = m_CurrentPath;
        DirPath.append("tws-maps");
        DirPath.append(Map.c_str());
        if(!std::filesystem::exists((DirPath)))
            std::filesystem::create_directory(DirPath);

        return std::filesystem::exists(Path);
    }

    bool TwsDownloadMap(string Map, string MapCrc, void* pData, int Size) override
    {
        std::filesystem::path Path = m_CurrentPath;
        Path.append("tws-maps");
        Path.append(Map.c_str());
        Path.append(MapCrc.c_str());
        Path.concat(".map");

        std::ofstream MapFile;
        MapFile.open(Path, std::ios::app);

        if(!MapFile)
        {
            log_msgf("storage", "download part of teeworlds map to {} failed", Path.c_str());
            return false;
        }
        MapFile.write((const char *) pData, Size);
        MapFile.close();

        return true;
    }
};

IStorage *CreateStorage() { return new CStorage(); }