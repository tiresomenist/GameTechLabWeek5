#include <pch.h>
#include "StaticMesh.h"
#include "ResourceManager.h"
#include "Engine/Resource/TextureResource.h"
#include "Core/Util/File.h"
#include "Engine/Log.h"
#include <stdexcept>


UStaticMesh::~UStaticMesh()
{
	for (FMaterial* Mat : Materials)
	{
		delete Mat;
	}
	Materials.Empty();
}
void UStaticMesh::BuildFromMeshData(const FStaticMeshData& MeshData)
{
	GResourceManager* RM = GResourceManager::GetInstance();
	this->MeshResource = RM->CreateStaticMeshResource( MeshData.PathFileName, MeshData.Vertices, MeshData.Indices);

    for (const FStaticMeshMaterial CPUMaterial : MeshData.Materials)
    {
        ID3D11ShaderResourceView* SRV = nullptr;
        FString TexturePath = File::PathToUtf8(CPUMaterial.DiffuseTexturePath);
        if (!CPUMaterial.DiffuseTexturePath.empty())
        {
            // 텍스처 로딩이 실패했을때는 SRV==nullptr이라서 하단 블럭에서 화이트텍스처로 대체됨 
            try
            {
                if (FTextureResource* Tex = RM->GetOrLoadTexture(TexturePath))
                    SRV = Tex->GetSRV();
            }
            catch (const std::exception& Error)
            {
                UE_LOG("[StaticMesh] Texture load failed: {} ({})", TexturePath, Error.what());
            }

        }

        // SRV 로드에 실패했거나 경로가 없었던 경우 화이트 텍스처로 대체
        if (!SRV)
        {
            TexturePath = "Assets/Textures/WhiteTexture.png"; 
            if (FTextureResource* WhiteTex = RM->GetOrLoadTexture(TexturePath))
                SRV = WhiteTex->GetSRV();
        }
        FMaterial GPUMaterial = RM->CreateStaticMeshMaterial(SRV, TexturePath, CPUMaterial.DiffuseTextureOptions.bClamp);
        Materials.Add(new FMaterial(GPUMaterial));
    }
	BoundsMin = MeshData.BoundsMin;
	BoundsMax = MeshData.BoundsMax;
	bHasBounds = true;

	SourceFilePath = MeshData.PathFileName;
	MeshKey = FName(MeshData.PathFileName);

	Sections = MeshData.Sections;
	Objects = MeshData.Objects;

	StartIndex = 0;
	IndexCount = static_cast<uint32>(MeshData.Indices.Num());
}