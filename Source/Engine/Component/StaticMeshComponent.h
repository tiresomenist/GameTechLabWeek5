#pragma once
#include "MeshComponent.h"
#include "Core/Name/Name.h"
#include "Engine/Resource/StaticMesh.h"

class UStaticMeshComponent : public UMeshComponent
{
    UCLASS(UStaticMeshComponent, "StaticMeshComponent", UMeshComponent)

public:
    void SetStaticMesh(const FName& InMeshKey);
    void SetStaticMesh(const FString& FilePath);
    void SetStaticMesh(const char* InMeshKey) { SetStaticMesh(FName(InMeshKey)); }
    //virtual FMeshResource* GetMeshResource() const override;
    virtual void Serialize(FArchive& Archive) override;

    //void SetMaterial(FMaterial* InMaterial, uint32 MaterialSlot);
    //void SetMaterial(const FString& InMaterialPath, uint32 MaterialSlot = 0);

    const FName& GetStaticMeshKey() const { return MeshKey; }
    //const FString& GetMaterialPath() const { return MaterialPath; }
    //const FMaterial* GetMaterial(uint32 MaterialSlot) { return MaterialList[MaterialSlot]; }
    virtual const FString& GetMaterialPath(uint32 MaterialSlot = 0) const override;
    virtual const FMaterial* GetMaterial(uint32 MaterialSlot = 0) const override;

    UStaticMesh* GetStaticMesh() const;

    virtual bool GetLocalBounds(FVector& OutMin, FVector& OutMax) const override;

    virtual void CreateRenderData(TArray<FPrimitiveRenderData>& ComponentRenderData, bool bSelected) override;

    virtual FMeshResource* GetMeshResource() const override;

private:
    FName MeshKey;
};
