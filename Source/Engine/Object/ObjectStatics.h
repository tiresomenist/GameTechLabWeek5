#pragma once
#include "Core/Container/Array.h"
#include "Core/Core.h"
#include "Core/Container/Map.h"
#include "Engine/Object/ObjectDomain.h"

struct FClassType;
class UObject;

struct FObjectSlot
{
    UObject* Object = nullptr;
	int32 NextFreeSlot = -1; // >= 0: 다음 빈 슬롯 인덱스, -1: 사용 중 또는 빈 마지막 슬롯, -2: 예약됨
    int32 ClassIndex = -1;
};

class GObjectStatics
{
    inline static TArray<uint32> NextUUID = TArray<uint32>(static_cast<size_t>(EObjectDomain::MAX_ITEMS));
    inline static TArray<FObjectSlot> Slots;
    inline static int32 FirstFreeSlot = -1;
	inline static TMap<const FClassType*, TArray<UObject*>> ObjectsByClass;
	inline static void AppendObjectsByClass(const FClassType* ClassType, bool IncludeDerived, TArray<UObject*>& OutObjects);

public:
    static uint32 GenerateUUID(EObjectDomain Domain);
    static uint32 GetNextUUID(EObjectDomain Domain) { return NextUUID[static_cast<size_t>(Domain)]; }
    static void SetNextUUID(EObjectDomain Domain, uint32 UUID);
    static uint32 ReserveSlot();
    static void CommitSlot(uint32 Index, UObject* Object);
    static void CancelSlot(uint32 Index) noexcept;
    static void Unregister(uint32 Index, UObject* Object) noexcept;
    static void Release();
	static TArray<UObject*> GetObjectsOfClass(const FClassType* ClassType, bool IncludeDerived = true);
};
