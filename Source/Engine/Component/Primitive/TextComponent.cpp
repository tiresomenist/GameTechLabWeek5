#include "pch.h"
#include "TextComponent.h"

#include "Engine/Component/CameraComponent.h"
#include "Core/Serialization/Archive.h"
#include "Engine/Renderer/Text/TextMeshBuilder.h"
#include "Engine/Resource/ResourceManager.h"

const FMatrix& UTextComponent::GetRenderWorldMatrix(const UCameraComponent* Camera) const
{
	if (!Camera) return GetWorldMatrix();

	// 빌보드 방향은 카메라를 따르되, 위치와 크기는 Attach 부모까지 반영한다.
	const FMatrix& WorldMatrix = GetWorldMatrix();
	const FVector WorldScale(
		WorldMatrix.GetAxis(0).Length(),
		WorldMatrix.GetAxis(1).Length(),
		WorldMatrix.GetAxis(2).Length());

	BillboardWorldMatrix = FMatrix::MakeScaleMatrix(WorldScale)
		* Camera->GetRelativeRotation().ToRotationMatrix()
		* FMatrix::MakeTranslationMatrix(WorldMatrix.GetOrigin());
	return BillboardWorldMatrix;
}

void UTextComponent::Serialize(FArchive& Archive)
{
	Super::Serialize(Archive);
	Archive.OptionalField("Text", Text);
}

// 캐시된 글자 Bounds에 기존 선택 여백을 더한다.
bool UTextComponent::GetLocalBounds(FVector& OutMin, FVector& OutMax) const
{
	FFontAtlas* Font = GResourceManager::GetInstance()->GetDefaultFont();
	if (!Font || !FTextMeshBuilder::UpdateLayoutCache(TextLayout, Text, *Font)) return false;

	const FVector Padding(SelectionPadding, SelectionPadding, SelectionPadding);
	OutMin = TextLayout.LocalBounds.Min - Padding;
	OutMax = TextLayout.LocalBounds.Max + Padding;
	return true;
}

// 문자열 배치는 재사용하고 해당 카메라의 빌보드 행렬만 복사한다.
bool UTextComponent::BuildTextItem(const UCameraComponent* Camera, FWorldTextItem& OutItem) const
{
	if (!Camera || Text.empty()) return false;

	FFontAtlas* Font = GResourceManager::GetInstance()->GetDefaultFont();
	if (!Font || !FTextMeshBuilder::UpdateLayoutCache(TextLayout, Text, *Font)) return false;

	OutItem.Text = Text;
	OutItem.Layout = &TextLayout;
	OutItem.WorldMatrix = GetRenderWorldMatrix(Camera);
	return true;
}