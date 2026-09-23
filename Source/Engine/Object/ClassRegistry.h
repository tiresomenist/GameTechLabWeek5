#pragma once

#include "Core/Container/Array.h"
#include "Core/Container/String.h"
#include "Core/Name/Name.h"

struct FClassType;

class FClassRegistry
{
public:
	/// <summary>
	/// 클래스 정의를 추가합니다.
	/// 주의: 이 함수는 UCLASS를 통해 호출되므로 직접 이 함수를 호출하지 말것
	/// </summary>
	/// <param name="Type">등록하려는 FClassType</param>
	static void* __INTERNAL__Add(FClassType* Type);

	/// <summary>
	/// 주어진 타입 FName으로 FClassType을 검색합니다.
	/// </summary>
	/// <param name="TypeName">찾고자 하는 FClassType의 FName</param>
	/// <returns></returns>
	static FClassType* FindClassType(const FName& TypeName);

	/// <summary>
	/// 주어진 FClassType의 자식 클래스들을 검색합니다.
	/// </summary>
	/// <param name="ParentClass">부모 클래스</param>
	/// <returns>자식 클래스들의 배열</returns>
	static const TArray<const FClassType*>& GetChildClasses(const FClassType* ParentClass);
};
