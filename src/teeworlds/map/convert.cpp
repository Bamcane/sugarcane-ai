#include <include/base.h>

#include <filesystem>
#include <fstream>

#include <zlib.h>

#include "convert.h"
#include "mapitems.h"

struct CDatafileItemType
{
	int m_Type;
	int m_Start;
	int m_Num;
} ;

struct CDatafileItem
{
	int m_TypeAndID;
	int m_Size;
};

struct CDatafileHeader
{
	char m_aID[4];
	int m_Version;
	int m_Size;
	int m_Swaplen;
	int m_NumItemTypes;
	int m_NumItems;
	int m_NumRawData;
	int m_ItemSize;
	int m_DataSize;
};

struct CDatafileData
{
	int m_NumItemTypes;
	int m_NumItems;
	int m_NumRawData;
	int m_ItemSize;
	int m_DataSize;
	char m_aStart[4];
};

struct CDatafileInfo
{
	CDatafileItemType *m_pItemTypes;
	int *m_pItemOffsets;
	int *m_pDataOffsets;
	int *m_pDataSizes;

	char *m_pItemStart;
	char *m_pDataStart;
};

struct CDatafile
{
    std::ifstream *m_pFile;
	unsigned m_Crc;
	CDatafileInfo m_Info;
	CDatafileHeader m_Header;
	int m_DataStartOffset;
	char **m_ppDataPtrs;
	char *m_pData;
};

bool ConvertMap(string Map, string Crc, ESMapItems **ppResult, int& Width, int& Height)
{
    std::filesystem::path Path(std::filesystem::current_path());
    Path.append("tws-maps");
    Path.append(Map.c_str());
    Path.append(Crc.c_str());
    Path.concat(".map");

    std::ifstream FileReader;
    FileReader.open(Path, std::ios::in);
    if(!FileReader.is_open())
        return false;

    CDatafileHeader Header;
    FileReader.seekg(0);
	FileReader.readsome((char *) &Header, sizeof(Header));
	if(Header.m_aID[0] != 'A' || Header.m_aID[1] != 'T' || Header.m_aID[2] != 'A' || Header.m_aID[3] != 'D')
	{
		if(Header.m_aID[0] != 'D' || Header.m_aID[1] != 'A' || Header.m_aID[2] != 'T' || Header.m_aID[3] != 'A')
		{
			log_msgf("datafile", "wrong signature. {:c} {:c} {:c} {:c}", Header.m_aID[0], Header.m_aID[1], Header.m_aID[2], Header.m_aID[3]);
			FileReader.close();
			return false;
		}
	}

    if(Header.m_Version != 3 && Header.m_Version != 4)
	{
		log_msgf("datafile", "wrong version. version={:d}", Header.m_Version);
		FileReader.close();
		return false;
	}
    FileReader.seekg(sizeof(Header));

	// read in the rest except the data
	unsigned Size = 0;
	Size += Header.m_NumItemTypes * sizeof(CDatafileItemType);
	Size += (Header.m_NumItems + Header.m_NumRawData) * sizeof(int);
	if(Header.m_Version == 4)
		Size += Header.m_NumRawData * sizeof(int); // v4 has uncompressed data sizes aswell
	Size += Header.m_ItemSize;

	unsigned AllocSize = Size;
	AllocSize += sizeof(CDatafile); // add space for info structure
	AllocSize += Header.m_NumRawData * sizeof(void*); // add space for data pointers

	CDatafile *pTmpDataFile = (CDatafile*) malloc(AllocSize);
	pTmpDataFile->m_Header = Header;
	pTmpDataFile->m_DataStartOffset = sizeof(CDatafileHeader) + Size;
	pTmpDataFile->m_ppDataPtrs = (char**)(pTmpDataFile+1);
	pTmpDataFile->m_pData = (char *)(pTmpDataFile+1) + Header.m_NumRawData * sizeof(char *);
	pTmpDataFile->m_pFile = &FileReader;

	// clear the data pointers and sizes
	memset(pTmpDataFile->m_ppDataPtrs, 0, Header.m_NumRawData * sizeof(void*));

	// read types, offsets, sizes and item data
    unsigned ReadSize = FileReader.readsome(pTmpDataFile->m_pData, Size);
	if(ReadSize != Size)
	{
	    FileReader.close();
		free(pTmpDataFile);
		pTmpDataFile = 0;
		log_msgf("convert/tws", "couldn't load the whole thing, wanted={} got={}", Size, ReadSize);
		return false;
	}

    pTmpDataFile->m_Info.m_pItemTypes = (CDatafileItemType *) pTmpDataFile->m_pData;
	pTmpDataFile->m_Info.m_pItemOffsets = (int *) &pTmpDataFile->m_Info.m_pItemTypes[pTmpDataFile->m_Header.m_NumItemTypes];
	pTmpDataFile->m_Info.m_pDataOffsets = (int *) &pTmpDataFile->m_Info.m_pItemOffsets[pTmpDataFile->m_Header.m_NumItems];
	pTmpDataFile->m_Info.m_pDataSizes = (int *) &pTmpDataFile->m_Info.m_pDataOffsets[pTmpDataFile->m_Header.m_NumRawData];

	if(Header.m_Version == 4)
		pTmpDataFile->m_Info.m_pItemStart = (char *) &pTmpDataFile->m_Info.m_pDataSizes[pTmpDataFile->m_Header.m_NumRawData];
	else
		pTmpDataFile->m_Info.m_pItemStart = (char *) &pTmpDataFile->m_Info.m_pDataOffsets[pTmpDataFile->m_Header.m_NumRawData];
	pTmpDataFile->m_Info.m_pDataStart = pTmpDataFile->m_Info.m_pItemStart + pTmpDataFile->m_Header.m_ItemSize;

	auto GetType = [pTmpDataFile](int Type, int *pStart, int *pNum) -> void
    {
        *pStart = 0;
        *pNum = 0;

        if(!pTmpDataFile)
            return;

        for(int i = 0; i < pTmpDataFile->m_Header.m_NumItemTypes; i++)
        {
            if(pTmpDataFile->m_Info.m_pItemTypes[i].m_Type == Type)
            {
                *pStart = pTmpDataFile->m_Info.m_pItemTypes[i].m_Start;
                *pNum = pTmpDataFile->m_Info.m_pItemTypes[i].m_Num;
                return;
            }
        }
    };

    auto GetItem = [pTmpDataFile](int Index, int *pType, int *pID) -> void*
    {
        if(!pTmpDataFile || Index < 0 || Index >= pTmpDataFile->m_Header.m_NumItems)
        {
            if(pType)
                *pType = 0;
            if(pID)
                *pID = 0;

            return 0;
        }

        CDatafileItem *i = (CDatafileItem *)(pTmpDataFile->m_Info.m_pItemStart + pTmpDataFile->m_Info.m_pItemOffsets[Index]);
        if(pType)
            *pType = (i->m_TypeAndID>>16)&0xffff; // remove sign extention
        if(pID)
            *pID = i->m_TypeAndID&0xffff;
        return (void *)(i+1);
    };

    auto GetFileDataSize = [pTmpDataFile](int Index) -> int const
    {
        if(!pTmpDataFile) { return 0; }

        if(Index == pTmpDataFile->m_Header.m_NumRawData - 1)
            return pTmpDataFile->m_Header.m_DataSize - pTmpDataFile->m_Info.m_pDataOffsets[Index];
        return pTmpDataFile->m_Info.m_pDataOffsets[Index+1] - pTmpDataFile->m_Info.m_pDataOffsets[Index];
    };

    auto GetData = [pTmpDataFile, GetFileDataSize](int Index, int Swap) -> void *
    {
        if(!pTmpDataFile) { return nullptr; }

        if(Index < 0 || Index >= pTmpDataFile->m_Header.m_NumRawData)
            return nullptr;

        // load it if needed
        if(!pTmpDataFile->m_ppDataPtrs[Index])
        {
            // fetch the data size
            int DataSize = GetFileDataSize(Index);

            if(pTmpDataFile->m_Header.m_Version == 4)
            {
                // v4 has compressed data
                void *pTemp = (char *) malloc(DataSize);
                unsigned long UncompressedSize = pTmpDataFile->m_Info.m_pDataSizes[Index];
                unsigned long s;

                pTmpDataFile->m_ppDataPtrs[Index] = (char *) malloc(UncompressedSize);

                // read the compressed data
                pTmpDataFile->m_pFile->seekg(pTmpDataFile->m_DataStartOffset + pTmpDataFile->m_Info.m_pDataOffsets[Index]);
                pTmpDataFile->m_pFile->read((char *) pTemp, DataSize);

                // decompress the data, TODO: check for errors
                s = UncompressedSize;
                uncompress((Bytef*)pTmpDataFile->m_ppDataPtrs[Index], &s, (Bytef*)pTemp, DataSize);

                // clean up the temporary buffers
                free(pTemp);
            }
            else
            {
                // load the data
                pTmpDataFile->m_ppDataPtrs[Index] = (char *)malloc(DataSize);
                pTmpDataFile->m_pFile->seekg(pTmpDataFile->m_DataStartOffset + pTmpDataFile->m_Info.m_pDataOffsets[Index]);
                pTmpDataFile->m_pFile->read((char *) pTmpDataFile->m_ppDataPtrs[Index], DataSize);
            }
        }

        return pTmpDataFile->m_ppDataPtrs[Index];
    };

    int LayersStart, LayersNum;
    GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
    for(int l = 0; l < LayersNum; l++)
    {
        CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(GetItem(LayersStart + l, 0, 0));
        if(pLayer->m_Type == LAYERTYPE_TILES)
        {
            CMapItemLayerTilemap *pTilemap = reinterpret_cast<CMapItemLayerTilemap *>(pLayer);
            if(pTilemap->m_Flags&TILESLAYERFLAG_GAME)
            {
                CTile *pTiles = static_cast<CTile *>(GetData(pTilemap->m_Data, 0));

                Width = pTilemap->m_Width;
                Height = pTilemap->m_Height;
                ESMapItems *pItems = new ESMapItems[Width * Height];
                for(int i = 0; i < Width * Height; i++)
                {
                    int Index = pTiles[i].m_Index;

                    if(Index > 128)
                        continue;

                    switch(Index)
                    {
                    case TILE_DEATH:
                        pItems[i] = ESMapItems::TILEFLAG_DEATH;
                        break;
                    case TILE_SOLID:
                        pItems[i] = ESMapItems::TILEFLAG_SOLID;
                        break;
                    case TILE_NOHOOK:
                        pItems[i] = ESMapItems::TILEFLAG_SOLID | ESMapItems::TILEFLAG_UNHOOKABLE;
                        break;
                    default:
                        pItems[i] = ESMapItems::TILEFLAG_AIR;
                    }
                }
                *ppResult = pItems;
                FileReader.close();
                return true; // there can only be one game layer and game group
            }
        }
    }
	FileReader.close();
    log_msg("convert/tws", "couldn't find game layer");
    return false;
}