#include "pch.h"

#include <Windows.h>
#include <iostream>
#include <vector>
#include <format>

#include "MinHook.h"

#pragma comment(lib, "libMinHook.x64.lib")

class Vec3 {
public:
	float x;
	float y;
	float z;
};

// Claim-pilot storage objects
class ClaimData {
public:
	int dimensionId;
	Vec3 cornerOne;
	Vec3 cornerEight;
	std::string claimId;
};

class PlayerData {
public:
	std::string xuid;
	std::vector<ClaimData> claims;
};

class StorageData {
public:
	std::vector<PlayerData> players;
};

void* getHookPoint(int offset)
{
	uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
	return reinterpret_cast<LPVOID*>(base + offset);
}

template <typename T>
inline MH_STATUS MH_CreateHookEx(LPVOID pTarget, LPVOID pDetour, T** ppOriginal)
{
	return MH_CreateHook(pTarget, pDetour, reinterpret_cast<LPVOID*>(ppOriginal));
}

class BlockPos {
public:
	int x;
	int y;
	int z;

	BlockPos(int x, int y, int z) {
		this->x = x;
		this->y = y;
		this->z = z;
	}

	static BlockPos* clone(BlockPos* pos) {
		return new BlockPos(pos->x, pos->y, pos->z);
	}

	std::string toString() {
		return std::format("({}, {}, {})", this->x, this->y, this->z);
	}
};

class Test {
public:
	int test;
};

class AutomaticId {
public:
	int value;
};

class BlockSource {
public:
	AutomaticId* getDimensionId();
};

class Block {};

class Actor {};

class HashedString {
public:
	void* hash;
	std::string str;
};

using Block_getName = HashedString*(*)(Block*);

using BlockSource_setBlock = bool(*)(BlockSource*, BlockPos*, Block*, int, void*, Actor*);
using BlockSource_getDimensionId = AutomaticId*(*)(BlockSource*, AutomaticId*);

using FireBlock_tick = void(*)(Block*, BlockSource*, BlockPos*, void*);

Block_getName BGetName;

BlockSource_getDimensionId BSGetDimensionId;
BlockSource_setBlock BSSetBlockTarget;
BlockSource_setBlock BSSetBlock = nullptr;

FireBlock_tick FBTickTarget;
FireBlock_tick FBTick = nullptr;

bool isPointInBox(BlockPos* point, Vec3* cornerOne, Vec3* cornerTwo) {
	return point->x >= cornerOne->x && point->x <= cornerTwo->x &&
		point->y >= cornerOne->y && point->y <= cornerTwo->y &&
		point->z >= cornerOne->z && point->z <= cornerTwo->z;
}

template <typename T>
void pushVector(std::vector<T> &initialVector, std::vector<T> &nextVector) {
	for (int i = 0; i < nextVector.size(); i++) {
		initialVector.push_back(nextVector[i]);
	}
}

namespace ClaimPilotStorage {
	StorageData storage;
}

std::vector<ClaimData> getAllClaims() {
	std::vector<ClaimData> claims;

	StorageData storageData = ClaimPilotStorage::storage;
	for (int i = 0; i < storageData.players.size(); i++) {
		PlayerData player = storageData.players[i];

		pushVector(claims, player.claims);
	}

	return claims;
}

ClaimData* getClaimAtPos(BlockPos* point, int32_t dimensionId) {
	std::vector<ClaimData> claims = getAllClaims();

	for (int i = 0; i < claims.size(); i++) {
		ClaimData claim = claims[i];

		if (isPointInBox(point, &claim.cornerOne, &claim.cornerEight) && claim.dimensionId == dimensionId) {
			return &claim;
		}
	}

	return nullptr;
}

std::string getBlockId(Block* block) {
	HashedString* hashedStr = BGetName(block);
	return hashedStr->str;
}

namespace FireBlockTickInfo {
	bool isActive = false;
	BlockPos* pos;
	bool fromMayPlace = false;
}

namespace ExplosionInfo {
	bool isActive = false;
}

bool onSetBlock(BlockSource* region, BlockPos* pos, Block* block, int uInt, void* actorSyncMessage, Actor* placer) {
	if (FireBlockTickInfo::isActive) {
		ClaimData* claim = getClaimAtPos(pos, region->getDimensionId()->value);
		if (claim == nullptr) {
			return BSSetBlock(region, pos, block, uInt, actorSyncMessage, placer);
		}

		if (!isPointInBox(FireBlockTickInfo::pos, &claim->cornerOne, &claim->cornerEight)) {
			return false;
		}

		return BSSetBlock(region, pos, block, uInt, actorSyncMessage, placer);
	}

	if (ExplosionInfo::isActive) { // Activated during explosion
		ClaimData* claim = getClaimAtPos(pos, region->getDimensionId()->value);
		if (claim == nullptr) {
			return BSSetBlock(region, pos, block, uInt, actorSyncMessage, placer);
		}

		return false;
	}

	return BSSetBlock(region, pos, block, uInt, actorSyncMessage, placer); // Nothing is active, just working normally
}

void onFireBlockTick(Block* block, BlockSource* region, BlockPos* pos, void* random) {
	FireBlockTickInfo::isActive = true;
	FireBlockTickInfo::pos = BlockPos::clone(pos);
	FBTick(block, region, pos, random);
	FireBlockTickInfo::isActive = false;
	delete FireBlockTickInfo::pos;
	FireBlockTickInfo::fromMayPlace = false;
}

AutomaticId* BlockSource::getDimensionId() {
	AutomaticId test;
	return BSGetDimensionId(this, &test);
}

extern "C" {
	__declspec(dllexport) void init(int BSSetBlockOffset, int FBTickOffset, int BGetNameOffset, int BSGetDimensionIdOffset) {
		BSSetBlockTarget = (BlockSource_setBlock)getHookPoint(BSSetBlockOffset);
		BGetName = (Block_getName)getHookPoint(BGetNameOffset);
		BSGetDimensionId = (BlockSource_getDimensionId)getHookPoint(BSGetDimensionIdOffset);
		FBTickTarget = (FireBlock_tick)getHookPoint(FBTickOffset);
		
		MH_Initialize();
		MH_CreateHook(BSSetBlockTarget, &onSetBlock, reinterpret_cast<LPVOID*>(&BSSetBlock));
		MH_CreateHook(FBTickTarget, &onFireBlockTick, reinterpret_cast<LPVOID*>(&FBTick));

		MH_EnableHook(FBTickTarget);
		MH_EnableHook(BSSetBlockTarget);
	}

	__declspec(dllexport) void updateStorage(StorageData storage) {
		ClaimPilotStorage::storage = storage;
	}

	__declspec(dllexport) void setSetBlockHookEnabled(bool value) {
		if (value) {
			ExplosionInfo::isActive = true;
		}
		else {
			ExplosionInfo::isActive = false;
		}
	}
}