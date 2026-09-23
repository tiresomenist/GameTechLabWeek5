#include "pch.h"
#include "Material.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/TextureResource.h"
#include "Core/Serialization/Archive.h"

void FMaterial::SetTexture(const FString& InPath)
{
	TexturePath = InPath;
	GResourceManager* RM = GResourceManager::GetInstance();
	if (!RM) return;

	FTextureResource* Tex = RM->GetOrLoadTexture(InPath);
	if (Tex && Tex->GetSRV())
	{
		SRV = Tex->GetSRV();
	}
	else
	{
		FTextureResource* White = RM->GetOrLoadTexture("Assets/Textures/WhiteTexture.png");
		SRV = White ? White->GetSRV() : nullptr;
	}
}

void FMaterial::SetSampler(const FName& InName)
{
	SamplerName = InName;
	GResourceManager* RM = GResourceManager::GetInstance();
	if (RM)
	{
		Sampler = RM->GetSampler(InName);
	}
}

void FMaterial::Serialize(FArchive& Archive)
{
    Archive.OptionalField("TexturePath", TexturePath);

    FString SamplerNameStr = SamplerName.ToString();
    Archive.OptionalField("SamplerName", SamplerNameStr);

    TArray<float> Color{ DiffuseColor.X, DiffuseColor.Y, DiffuseColor.Z, DiffuseColor.W };
    Archive.OptionalField("DiffuseColor", Color);

    Archive.OptionalField("AlphaCutoff", AlphaCutoff);
    Archive.OptionalField("bEnableUVScroll", bEnableUVScroll);

    TArray<float> Scale{ UVScale.X, UVScale.Y };
    Archive.OptionalField("UVScale", Scale);

    TArray<float> Speed{ ScrollSpeed.X, ScrollSpeed.Y };
    Archive.OptionalField("ScrollSpeed", Speed);

    // 불러오기(Loading) 모드일 때 포인터 및 벡터 복원
    if (Archive.IsLoading())
    {
        if (!TexturePath.empty())
        {
            SetTexture(TexturePath); // SRV 자동 복원
        }
        if (!SamplerNameStr.empty())
        {
            SetSampler(FName(SamplerNameStr)); // Sampler 포인터 자동 복원
        }
        if (Color.Num() == 4)
        {
            DiffuseColor = FVector4(Color[0], Color[1], Color[2], Color[3]);
        }
        if (Scale.Num() == 2)
        {
            UVScale = FVector2(Scale[0], Scale[1]);
        }
        if (Speed.Num() == 2)
        {
            ScrollSpeed = FVector2(Speed[0], Speed[1]);
        }
    }
}
