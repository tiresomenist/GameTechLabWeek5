#include "pch.h"
#include "ClassRegistry.h"
#include "Engine/Object/ClassType.h"

#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Gizmo/ObjectAxisGizmo.h"
#include "Editor/Gizmo/WorldAxisGizmo.h"
#include "Editor/Gizmo/WorldGridGizmo.h"

#include "Editor/Grid.h"

#include "Engine/Object/Object.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Component/ActorComponent.h"
#include "Engine/Component/SceneComponent.h"
#include "Engine/Component/CameraComponent.h"
#include "Engine/Component/StaticMeshComponent.h"

#include "Engine/Component/Primitive/PrimitiveComponent.h"
#include "Engine/Component/Primitive/TextComponent.h"
#include "Engine/Component/Primitive/FlipbookComponent.h"

#include "Engine/Component/Light/LightComponent.h"
#include "Engine/Component/Light/SpotLightComponent.h"
#include "Core/Container/Map.h"
#include <stdexcept>
#include <format>


namespace
{
	TMap<FName, FClassType*>& GetClassTypeMap()
	{
		// 최초 등록 또는 조회 시 레지스트리를 생성함
		static TMap<FName, FClassType*> ClassTypes;
		return ClassTypes;
	}

	TMap<const FClassType*, TArray<const FClassType*>>& GetChildClassMap()
	{
		static TMap<const FClassType*, TArray<const FClassType*>> ChildClasses;
		return ChildClasses;
	}
}

void* FClassRegistry::__INTERNAL__Add(FClassType* Type)
{
	if (Type == nullptr){ throw std::invalid_argument("Class type is null"); }

	if (Type->Name.IsNone()){ throw std::invalid_argument("Class name is None"); }

	auto& ClassTypes = GetClassTypeMap();

	if (FClassType** Existing = ClassTypes.Find(Type->Name))
	{
		// 동일 클래스의 재등록은 허용함
		if (*Existing == Type){	return nullptr; }

		// 동일 이름을 사용하는 다른 클래스의 등록을 거부함
		throw std::logic_error(std::format("FClassType.Name이 중복되었습니다. 중복되는 이름: {}",Type->DisplayName)
		);
	}

	ClassTypes.Add(Type->Name, Type);
	if (Type->ParentClassType != nullptr)
	{
		GetChildClassMap()[Type->ParentClassType].Add(Type);
	}

	return nullptr;
}

FClassType* FClassRegistry::FindClassType(const FName& TypeName)
{
	if (TypeName.IsNone()){	return nullptr; }

	if (FClassType** Found = GetClassTypeMap().Find(TypeName)){	return *Found; }

	return nullptr;
}

const TArray<const FClassType*>& FClassRegistry::GetChildClasses(const FClassType* ParentClass)
{
	if (ParentClass == nullptr) { throw std::invalid_argument("Parent class is null"); }
	if (TArray<const FClassType*>* Found = GetChildClassMap().Find(ParentClass)) { return *Found; }
	static TArray<const FClassType*> EmptyArray;
	return EmptyArray;
}
