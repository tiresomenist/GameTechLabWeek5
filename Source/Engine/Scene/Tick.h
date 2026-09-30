#pragma once
#include "Core/Core.h"
#include "Core/Container/Array.h"

enum class ETickGroup : uint32
{
    Update,
    PostUpdate,
    Count
};

constexpr uint32 TickGroupCount = static_cast<uint32>(ETickGroup::Count);

struct FTickSettings
{
    bool bCanEverTick = false;
    bool bTickEnabled = false;
    ETickGroup TickGroup = ETickGroup::Update;

    // 등록 상태는 목록 관리 코드만 변경합니다. Count는 미등록입니다.
    ETickGroup RegisteredGroup = ETickGroup::Count;
};

template <typename T>
class TTickRegistry
{
public:
    // 소속과 Tick 설정에 맞춰 활성 목록을 갱신합니다.
    void Refresh(T* Object, FTickSettings& Settings, bool bBelongsToScene)
    {
        const ETickGroup DesiredGroup =
            bBelongsToScene && Settings.bCanEverTick && Settings.bTickEnabled
            ? Settings.TickGroup : ETickGroup::Count;

        if (Settings.RegisteredGroup == DesiredGroup) return;

        if (Settings.RegisteredGroup != ETickGroup::Count)
        {
            const uint32 GroupIndex = static_cast<uint32>(Settings.RegisteredGroup);
            TArray<T*>& List = Groups[GroupIndex];

            for (uint32 Index = 0; Index < static_cast<uint32>(List.Num()); ++Index)
            {
                if (List[Index] != Object) continue;

                // 실행 중에는 원소 위치를 유지하고 참조만 제거합니다.
                if (bTicking)
                {
                    List[Index] = nullptr;
                    bNeedsCompact[GroupIndex] = true;
                }
                else
                {
                    List.RemoveAt(Index);
                }
                break;
            }
        }

        Settings.RegisteredGroup = DesiredGroup;
        if (DesiredGroup != ETickGroup::Count)
            Groups[static_cast<uint32>(DesiredGroup)].Add(Object);
    }

    // 모든 그룹의 실행 범위를 프레임 시작 시 고정합니다.
    void BeginTick()
    {
        bTicking = true;
        for (uint32 GroupIndex = 0; GroupIndex < TickGroupCount; ++GroupIndex)
            TickCounts[GroupIndex] = static_cast<uint32>(Groups[GroupIndex].Num());
    }

    // 해당 그룹에서 프레임 시작 전에 등록된 대상만 실행합니다.
    void TickGroup(ETickGroup Group, float DeltaTime)
    {
        const uint32 GroupIndex = static_cast<uint32>(Group);
        TArray<T*>& List = Groups[GroupIndex];

        for (uint32 Index = 0; Index < TickCounts[GroupIndex]; ++Index)
        {
            // Tick 안에서 배열이 확장되어도 원소 참조를 보관하지 않습니다.
            T* Object = List[Index];
            if (Object) Object->Tick(DeltaTime);
        }
    }

    // 실행 중 제거된 자리만 정리하고 배열 용량은 유지합니다.
    void EndTick()
    {
        bTicking = false;
        for (uint32 GroupIndex = 0; GroupIndex < TickGroupCount; ++GroupIndex)
        {
            if (!bNeedsCompact[GroupIndex]) continue;
            Groups[GroupIndex].Remove(nullptr);
            bNeedsCompact[GroupIndex] = false;
        }
    }

    // Scene 전체 정리 시 Tick 외부에서 호출합니다.
    void Clear()
    {
        for (uint32 GroupIndex = 0; GroupIndex < TickGroupCount; ++GroupIndex)
        {
            Groups[GroupIndex].Empty();
            TickCounts[GroupIndex] = 0;
            bNeedsCompact[GroupIndex] = false;
        }
        bTicking = false;
    }

private:
    TArray<T*> Groups[TickGroupCount];
    uint32 TickCounts[TickGroupCount]{};
    bool bNeedsCompact[TickGroupCount]{};
    bool bTicking = false;
};