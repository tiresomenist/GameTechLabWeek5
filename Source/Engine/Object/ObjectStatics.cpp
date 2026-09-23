#include "pch.h"
#include "ObjectStatics.h"
#include "Engine/Object/Object.h"
#include <limits>
#include <stdexcept>

void GObjectStatics::AppendObjectsByClass(const FClassType* ClassType, bool IncludeDerived, TArray<UObject*>& OutObjects)
{
	if (TArray<UObject*>* Found = ObjectsByClass.Find(ClassType))
	{
		for (UObject* Object : *Found)
		{
			OutObjects.Add(Object);
		}
	}

	if (IncludeDerived)
	{
		for (const FClassType* ChildClass : FClassRegistry::GetChildClasses(ClassType))
		{
			AppendObjectsByClass(ChildClass, true, OutObjects);
		}
	}
}

uint32 GObjectStatics::GenerateUUID(EObjectDomain Domain)
{
    auto& Next = NextUUID[static_cast<size_t>(Domain)];
    if (Next == (std::numeric_limits<uint32>::max)())
        throw std::overflow_error("UUID exhausted");
    return Next++;
}

void GObjectStatics::SetNextUUID(EObjectDomain Domain, uint32 UUID)
{
    NextUUID[static_cast<size_t>(Domain)] = UUID;
}

uint32 GObjectStatics::ReserveSlot()
{
    if (FirstFreeSlot == -1)
    {
        if (Slots.Num() >= (std::numeric_limits<int32>::max)())
        {
            throw std::overflow_error("Object slot limit");
        }

		const uint32 Index = static_cast<uint32>(Slots.Num());
		Slots.Add(FObjectSlot{ .NextFreeSlot = -2 });
		return Index;
    }

	const int32 FreeSlot = FirstFreeSlot;
	FirstFreeSlot = Slots[FreeSlot].NextFreeSlot;
    Slots[FreeSlot].NextFreeSlot = -2;
    return FreeSlot;
}

void GObjectStatics::CommitSlot(uint32 Index, UObject* Object)
{
	if (Object == nullptr)
	{
		throw std::invalid_argument("Object is nullptr");
	}
	if (Index >= Slots.Num())
	{
		throw std::out_of_range("Index out of range");
	}
	if (Slots[Index].NextFreeSlot != -2)
	{
		throw std::logic_error("Slot not reserved");
	}

	TArray<UObject*>& Objects = ObjectsByClass[Object->GetInstanceClass()];
	Objects.Add(Object);
	Slots[Index] = FObjectSlot{
		.Object = Object, 
		.NextFreeSlot = -1,
		.ClassIndex = Objects.Num() - 1,
	};
}

void GObjectStatics::CancelSlot(uint32 Index) noexcept
{
    if (Index < Slots.Num() &&
    	Slots[Index].Object == nullptr &&
    	Slots[Index].NextFreeSlot == -2)
    {
	    Slots[Index] = FObjectSlot{
		    .NextFreeSlot = FirstFreeSlot
	    };
        FirstFreeSlot = Index;
    }
}

void GObjectStatics::Unregister(uint32 Index, UObject* Object) noexcept
{
    if (Object != nullptr &&
    	static_cast<int32>(Index) < Slots.Num() &&
    	Slots[Index].Object == Object &&
    	ObjectsByClass.Find(Object->GetInstanceClass()) != nullptr)
    {
		TArray<UObject*>& Objects = ObjectsByClass[Object->GetInstanceClass()];

		const int32 RemoveIndex = Slots[Index].ClassIndex;
		if (RemoveIndex != Objects.Num() - 1)
		{
			UObject* MovedObject = Objects.Last();
			Objects[RemoveIndex] = MovedObject;
			Slots[MovedObject->GetInternalIndex()].ClassIndex = RemoveIndex;
		}
		Objects.Pop();

	    Slots[Index] = FObjectSlot{
		    .NextFreeSlot = FirstFreeSlot,
	    };
		FirstFreeSlot = static_cast<int32>(Index);
    }
}

void GObjectStatics::Release()
{
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
	    delete Slots[i].Object;
    }
    Slots.Empty();
    FirstFreeSlot = -1;
	ObjectsByClass.Empty();
}

TArray<UObject*> GObjectStatics::GetObjectsOfClass(const FClassType* ClassType, bool IncludeDerived)
{
	if (ClassType == nullptr) { return {}; }

	TArray<UObject*> Result;
	if (TArray<UObject*>* Found = ObjectsByClass.Find(ClassType))
	{
		Result = *Found;
	}

	if (IncludeDerived)
	{
		for (const FClassType* ChildClass : FClassRegistry::GetChildClasses(ClassType))
		{
			for (UObject* ChildObject : GetObjectsOfClass(ChildClass, true))
			{
				Result.Add(ChildObject);
			}
		}
	}

	return Result;
}
