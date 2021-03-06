#pragma once

#include "IPluginInterface.h"
#include "IHashType.h"

#include "skse64/GameTypes.h"
#include "skse64/GameThreads.h"
#include "skse64/NiTypes.h"
#include "skse64/NiGeometry.h"

#include "StringTable.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "half.hpp"

#include <string>
#include <set>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <ctime>
#include <mutex>

#include <DirectXMath.h>
#include <DirectXPackedVector.h>

class TESObjectREFR;
struct SKSESerializationInterface;
class BSResourceNiBinaryStream;
class NiAVObject;
class TESObjectARMO;
class TESObjectARMA;
class NiExtraData;
class TESNPC;
class TESRace;
class NiSkinPartition;
struct ModInfo;

#define MORPH_MOD_DIRECTORY "actors\\character\\BodyGenData\\"

class BodyMorph
{
public:
	bool operator<(const BodyMorph & rhs) const { return *m_name < *rhs.m_name; }
	bool operator==(const BodyMorph & rhs) const	{ return *m_name == *rhs.m_name; }

	StringTableItem	m_name;
	float			m_value;

	void Save(SKSESerializationInterface * intfc, UInt32 kVersion);
	bool Load(SKSESerializationInterface * intfc, UInt32 kVersion, const StringIdMap & stringTable);
};

class BodyMorphSet : public std::set<BodyMorph>
{
public:
	// Serialization
	void Save(SKSESerializationInterface * intfc, UInt32 kVersion);
	bool Load(SKSESerializationInterface * intfc, UInt32 kVersion, const StringIdMap & stringTable);
};

class BodyMorphData : public std::unordered_map<StringTableItem, std::unordered_map<StringTableItem, float>>
{
public:
	void Save(SKSESerializationInterface * intfc, UInt32 kVersion);
	bool Load(SKSESerializationInterface * intfc, UInt32 kVersion, const StringIdMap & stringTable);
};

class ActorMorphs : public SafeDataHolder<std::unordered_map<UInt64, BodyMorphData>>
{
	friend class BodyMorphInterface;
public:
	typedef std::unordered_map<UInt64, BodyMorphData>	MorphMap;

	// Serialization
	void Save(SKSESerializationInterface * intfc, UInt32 kVersion);
	bool Load(SKSESerializationInterface * intfc, UInt32 kVersion, const StringIdMap & stringTable);
};

class TriShapeVertexDelta
{
public:
	UInt16		index;
	DirectX::XMVECTOR delta;
};

class TriShapePackedVertexDelta
{
public:
	UInt16	index;
	DirectX::XMVECTOR delta;
};

class TriShapePackedUVDelta
{
public:
	UInt16	index;
	SInt16	u;
	SInt16	v;
};

class TriShapeVertexData
{
public:
	virtual void ApplyMorphRaw(UInt16 vertCount, void * vertices, float factor) = 0;

	struct Layout
	{
		UInt64	vertexDesc;
		UInt8	* vertexData;
	};

	virtual void ApplyMorph(UInt16 vertexCount, Layout * vertexData, float factor) = 0;
};
typedef std::shared_ptr<TriShapeVertexData> TriShapeVertexDataPtr;

class TriShapeFullVertexData : public TriShapeVertexData
{
public:
	TriShapeFullVertexData() : m_maxIndex(0) { }

	virtual void ApplyMorphRaw(UInt16 vertCount, void * vertices, float factor);
	virtual void ApplyMorph(UInt16 vertexCount, Layout * vertexData, float factor);

	UInt32								m_maxIndex;
	std::vector<TriShapeVertexDelta>	m_vertexDeltas;
};
typedef std::shared_ptr<TriShapeFullVertexData> TriShapeFullVertexDataPtr;

class TriShapePackedVertexData : public TriShapeVertexData
{
public:
	TriShapePackedVertexData() : m_maxIndex(0) { }

	virtual void ApplyMorphRaw(UInt16 vertCount, void * vertices, float factor);
	virtual void ApplyMorph(UInt16 vertexCount, Layout * vertexData, float factor);

	float									m_multiplier;
	UInt32									m_maxIndex;
	std::vector<TriShapePackedVertexDelta>	m_vertexDeltas;
};
typedef std::shared_ptr<TriShapePackedVertexData> TriShapePackedVertexDataPtr;

class TriShapePackedUVData : public TriShapeVertexData
{
public:
	TriShapePackedUVData() : m_maxIndex(0) { }

	struct UVCoord
	{
		half_float::half u;
		half_float::half v;
	};

	virtual void ApplyMorphRaw(UInt16 vertCount, void * vertices, float factor);
	virtual void ApplyMorph(UInt16 vertexCount, Layout * vertexData, float factor);

	float								m_multiplier;
	UInt32								m_maxIndex;
	std::vector<TriShapePackedUVDelta>	m_uvDeltas;
};
typedef std::shared_ptr<TriShapePackedUVData> TriShapePackedUVDataPtr;


class BodyMorphMap : public std::unordered_map<SKEEFixedString, std::pair<TriShapeVertexDataPtr, TriShapeVertexDataPtr>>
{
	friend class MorphCache;
public:
	BodyMorphMap() : m_hasUV(false) { }

	void ApplyMorphs(TESObjectREFR * refr, std::function<void(const TriShapeVertexDataPtr, float)> vertexFunctor, std::function<void(const TriShapeVertexDataPtr, float)> uvFunctor) const;
	bool HasMorphs(TESObjectREFR * refr) const;

	bool HasUV() const { return m_hasUV; }

private:
	bool m_hasUV;
};

class TriShapeMap : public std::unordered_map<SKEEFixedString, BodyMorphMap>
{
public:
	TriShapeMap()
	{
		memoryUsage = sizeof(TriShapeMap);
	}

	UInt32 memoryUsage;
};

class MorphFileCache
{
	friend class MorphCache;
	friend class BodyMorphInterface;
public:
	void ApplyMorphs(TESObjectREFR * refr, NiAVObject * rootNode, bool erase = false, bool defer = false);
	void ApplyMorph(TESObjectREFR * refr, NiAVObject * rootNode, bool erase, const std::pair<SKEEFixedString, BodyMorphMap> & bodyMorph, std::mutex * mtx = nullptr, bool deferred = true);

private:
	TriShapeMap vertexMap;
	std::time_t accessed;
};

class MorphCache : public SafeDataHolder<std::unordered_map<SKEEFixedString, MorphFileCache>>
{
	friend class BodyMorphInterface;

public:
	MorphCache()
	{
		totalMemory = sizeof(MorphCache);
		memoryLimit = totalMemory;
	}

	typedef std::unordered_map<SKEEFixedString, TriShapeMap>	FileMap;

	SKEEFixedString CreateTRIPath(const char * relativePath);
	bool CacheFile(const char * modelPath);

	void ApplyMorphs(TESObjectREFR * refr, NiAVObject * rootNode, bool attaching = false, bool deferUpdate = false);
	void UpdateMorphs(TESObjectREFR * refr, bool deferUpdate = false);

	void Shrink();

private:
	UInt32 memoryLimit;
	UInt32 totalMemory;
};

class NIOVTaskUpdateModelWeight : public TaskDelegate
{
public:
	virtual void Run();
	virtual void Dispose();

	NIOVTaskUpdateModelWeight(Actor * actor);
	
private:
	UInt32	m_formId;
};

class NIOVTaskUpdateSkinPartition : public TaskDelegate
{
public:
	virtual void Run();
	virtual void Dispose();

	NIOVTaskUpdateSkinPartition(NiSkinInstance * skinInstance, NiSkinPartition * partition);

private:
	NiPointer<NiSkinPartition>	m_partition;
	NiPointer<NiSkinInstance>	m_skinInstance;
};

class BodyGenMorphData
{
public:
	SKEEFixedString	name;
	float			lower;
	float			upper;
};

class BodyGenMorphSelector : public std::vector<BodyGenMorphData>
{
public:
	UInt32 Evaluate(std::function<void(SKEEFixedString, float)> eval);
};

class BodyGenMorphs : public std::vector<BodyGenMorphSelector>
{
public:
	UInt32 Evaluate(std::function<void(SKEEFixedString, float)> eval);
};

class BodyGenTemplate : public std::vector<BodyGenMorphs>
{
public:
	UInt32 Evaluate(std::function<void(SKEEFixedString, float)> eval);
};
typedef std::shared_ptr<BodyGenTemplate> BodyGenTemplatePtr;


typedef std::unordered_map<SKEEFixedString, BodyGenTemplatePtr> BodyGenTemplates;

class BodyTemplateList : public std::vector<BodyGenTemplatePtr>
{
public:
	UInt32 Evaluate(std::function<void(SKEEFixedString, float)> eval);
};

class BodyGenDataTemplates : public std::vector<BodyTemplateList>
{
public:
	UInt32 Evaluate(std::function<void(SKEEFixedString, float)> eval);
};
typedef std::shared_ptr<BodyGenDataTemplates> BodyGenDataTemplatesPtr;

typedef std::unordered_map<TESNPC*, BodyGenDataTemplatesPtr> BodyGenData;

class BodyMorphInterface : public IPluginInterface
{
public:
	enum
	{
		kCurrentPluginVersion = 3,
		kSerializationVersion1 = 1,
		kSerializationVersion2 = 2,
		kSerializationVersion3 = 3,
		kSerializationVersion = kSerializationVersion3
	};
	virtual UInt32 GetVersion();

	// Serialization
	virtual void Save(SKSESerializationInterface * intfc, UInt32 kVersion);
	virtual bool Load(SKSESerializationInterface * intfc, UInt32 kVersion, const StringIdMap & stringTable);
	virtual void Revert();

	void LoadMods();

	virtual void SetMorph(TESObjectREFR * actor, SKEEFixedString morphName, SKEEFixedString morphKey, float relative);
	virtual float GetMorph(TESObjectREFR * actor, SKEEFixedString morphName, SKEEFixedString morphKey);
	virtual void ClearMorph(TESObjectREFR * actor, SKEEFixedString morphName, SKEEFixedString morphKey);

	virtual float GetBodyMorphs(TESObjectREFR * actor, SKEEFixedString morphName);
	virtual void ClearBodyMorphNames(TESObjectREFR * actor, SKEEFixedString morphName);

	virtual void VisitMorphs(TESObjectREFR * actor, std::function<void(SKEEFixedString, std::unordered_map<StringTableItem, float> *)> functor);
	virtual void VisitKeys(TESObjectREFR * actor, SKEEFixedString name, std::function<void(SKEEFixedString, float)> functor);

	virtual void ClearMorphs(TESObjectREFR * actor);

	virtual void ApplyVertexDiff(TESObjectREFR * refr, NiAVObject * rootNode, bool erase = false);

	virtual void ApplyBodyMorphs(TESObjectREFR * refr, bool deferUpdate = true);
	virtual void UpdateModelWeight(TESObjectREFR * refr, bool immediate = false);

	virtual void SetCacheLimit(UInt32 limit);
	virtual bool HasMorphs(TESObjectREFR * actor);

	virtual bool ReadBodyMorphs(SKEEFixedString filePath);
	virtual bool ReadBodyMorphTemplates(SKEEFixedString filePath);
	virtual UInt32 EvaluateBodyMorphs(TESObjectREFR * actor);

	virtual bool HasBodyMorph(TESObjectREFR * actor, SKEEFixedString morphName, SKEEFixedString morphKey);
	virtual bool HasBodyMorphName(TESObjectREFR * actor, SKEEFixedString morphName);
	virtual bool HasBodyMorphKey(TESObjectREFR * actor, SKEEFixedString morphKey);
	virtual void ClearBodyMorphKeys(TESObjectREFR * actor, SKEEFixedString morphKey);
	virtual void VisitStrings(std::function<void(SKEEFixedString)> functor);
	virtual void VisitActors(std::function<void(TESObjectREFR*)> functor);

protected:
	void GetFilteredNPCList(std::vector<TESNPC*> activeNPCs[], const ModInfo * modInfo, UInt32 gender, TESRace * raceFilter);

private:
	ActorMorphs	actorMorphs;
	MorphCache	morphCache;
	BodyGenTemplates bodyGenTemplates;
	BodyGenData	bodyGenData[2];

	friend class NIOVTaskUpdateMorph;
};