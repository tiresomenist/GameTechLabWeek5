#pragma once
#include "Engine/Component/Primitive/PrimitiveComponent.h"
#include "Core/Name/Name.h"
#include <memory>


class UMeshComponent : public UPrimitiveComponent
{
    UCLASS(UMeshComponent, "MeshComponent", UPrimitiveComponent)

public:
    // 메시 Component의 Tick 사용 가능 여부를 초기화합니다.
    void Initialize() override;

    ~UMeshComponent() override;

    //virtual FMeshResource* GetMeshResource() const override;
    virtual void Serialize(FArchive& Archive) override;
    virtual void Tick(float DeltaTime) override;

    virtual FMeshResource* GetMeshResource() const override { return nullptr; }

    // override material 설정 - 여기서 수정해도 실제 staticmesh의 material은 바뀌지 않음
    void SetOverrideMaterial(std::unique_ptr<FMaterial> InMaterial, uint32 MaterialSlot);
    void SetOverrideMaterial(const FString& InMaterialPath, uint32 MaterialSlot = 0);

    virtual const FString& GetMaterialPath(uint32 MaterialSlot = 0) const;
    virtual const FMaterial* GetMaterial(uint32 MaterialSlot = 0) const;

    FMaterial* GetOrCreateOverrideMaterial(uint32 Slot);
    void ResetOverrideMaterial(uint32 Slot);
    bool HasOverrideMaterial(uint32 SlotIdx);
    
    virtual void CreateRenderData(TArray<FPrimitiveRenderData>& ComponentRenderData, const FPrimitiveRenderContext& Context, bool bSelected = false) override;

    bool IsVisible() { return bIsVisible; }
    void SetVisibility(bool InVisibility) { bIsVisible = InVisibility; }

    bool IsUVScrollEnabled() const { return bEnableUVScroll; }
    void SetUVScrollEnabled(bool InbEnable) { bEnableUVScroll = InbEnable; }
    void SetboolUVScroll(bool InbEnableUVScroll) { bEnableUVScroll = InbEnableUVScroll; }

    const FVector2& GetUVScale() const { return UVScale; }
    void SetUVScale(const FVector2& InScale) { UVScale = InScale; }
    void SetUVScale(float InScaleU, float InScaleV) { UVScale = FVector2(InScaleU, InScaleV); }

    const FVector2& GetScrollSpeed() const { return ScrollSpeed; }
    void SetScrollSpeed(const FVector2& InSpeed) { ScrollSpeed = InSpeed; }
    void SetScrollSpeed(float InSpeedU, float InSpeedV) { ScrollSpeed = FVector2(InSpeedU, InSpeedV); }

    const FVector2& GetUVOffset() const { return UVOffset; }
    void SetUVOffset(const FVector2& InOffset) { UVOffset = InOffset; }
    void SetUVOffset(float InOffsetU, float InOffsetV) { UVOffset = FVector2(InOffsetU, InOffsetV); }
    void ResetUVOffset() { UVOffset = FVector2::Zero; }

    void RefreshUVScrollTick();
protected:
    void ClearOverrideMaterials();

    TArray<std::unique_ptr<FMaterial>> OverrideMaterials;
    bool bEnableUVScroll = true;
    FVector2 UVScale{ 1.0f, 1.0f };
    FVector2 UVOffset{ 0.0f, 0.0f };
    FVector2 ScrollSpeed{ 0.0f, 0.0f };


private:
    bool bIsVisible = true;
};
