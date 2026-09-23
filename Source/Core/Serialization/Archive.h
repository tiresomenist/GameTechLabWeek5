#pragma once

#include "Core/Container/String.h"
#include "Core/Container/Array.h"
#include "Core/Core.h"
#include <type_traits>
#include <limits>
#include <stdexcept>
#include <cstddef>

class FArchive
{
public:
	virtual ~FArchive() = default;
	virtual bool IsLoading()const = 0;
	bool IsSaving() const;

	template<typename T>
	// 필수 Field 입력용
	void Field(const char* Name, T& Value) {
		if (!SerializeValue(Name, Value)) {
			throw std::runtime_error(FString("Missing Archive Field :") + (Name ? Name : "<element>"));
		}
	}
	// 선택 Field 입력용
	template<typename T>
    bool OptionalField(const char* Name, T& Value) {
		return SerializeValue(Name, Value);
	}

    // Name에 해당하는 객체 처리 시작
    virtual bool BeginObject(const char* Name) = 0;

    // 객체 처리 끝
    virtual void EndObject() = 0;

    // 배열 진입. 개수 처리
    virtual bool BeginArray(const char* Name, uint32& Count) = 0;

    // 배열 원소 진입
    virtual void BeginArrayElement(uint32 Index) = 0;

    // 배열 원소 처리 끝
    virtual void EndArrayElement() = 0;

    // 배열 처리 끝
    virtual void EndArray() = 0;

    // 문자열 키 맵 진입. 원소 개수 처리
    virtual bool BeginMap(const char* Name, uint32& Count) = 0;

    // 맵 원소 진입.
    virtual void BeginMapEntry(uint32 Index, FString& Key) = 0;

    // 맵 원소 처리 끝
    virtual void EndMapEntry() = 0;

    // 맵 처리 끝
    virtual void EndMap() = 0;

    // float 3개를 처리하며, 읽기에서 잘못된 값은 기존 씬의 기본값 규칙을 적용한다.
    virtual void Float3OrDefault(const char* Name, TArray<float>& Values, float Default) = 0;

    // 읽을 배열을 할당하기 전에 원소 개수와 최소 데이터 크기를 검사한다.
    virtual void CheckArraySize(uint32 Count, size_t MinimumElementBytes) const
    {
        // 바이트 기반 검사가 필요한 Archive에서 재정의한다.
    }

protected:
    virtual bool SerializeValue(const char* Name, int32& Value) = 0;
    virtual bool SerializeValue(const char* Name, uint32& Value) = 0;
    virtual bool SerializeValue(const char* Name, float& Value) = 0;
    virtual bool SerializeValue(const char* Name, double& Value) = 0;
    virtual bool SerializeValue(const char* Name, bool& Value) = 0;
    virtual bool SerializeValue(const char* Name, FString& Value) = 0;

    // 기본 타입 배열의 개수와 각 원소를 공통 순서로 처리한다.
    template<typename T>
    bool SerializeValue(const char* Name, TArray<T>& Values)
    {
        // 현재 TArray<bool>은 std::vector<bool>의 참조 형식과 호환되지 않는다.
        static_assert(!std::is_same_v<T, bool>, "TArray<bool> serialization is not supported.");

        // 쓰기에서는 현재 배열 크기를 전달하고 읽기에서는 저장된 크기를 받는다.
        uint32 Count = IsSaving() ? static_cast<uint32>(Values.Num()) : 0;
        if (!BeginArray(Name, Count)) { return false; }

        // TArray의 Num()과 인덱스가 표현할 수 있는 범위를 확인한 뒤 크기를 조정한다.
        if (Count > static_cast<uint32>((std::numeric_limits<int32>::max)()))
            throw std::length_error("Archive array exceeds TArray capacity.");

        if (IsLoading())
        {
            // 기본 숫자는 자체 크기, 문자열과 중첩 배열은 길이 필드가 최소 크기다.
            constexpr size_t MinimumElementBytes = std::is_arithmetic_v<T> ? sizeof(T) : sizeof(uint32);
            CheckArraySize(Count, MinimumElementBytes);
            Values.SetNum(Count);
        }
        // 원소에 진입한 뒤 이름 없이 현재 원소 자체를 직렬화한다.
        for (uint32 Index = 0; Index < Count; ++Index)
        {
            BeginArrayElement(Index);
            Field(nullptr, Values[static_cast<int32>(Index)]);
            EndArrayElement();
        }
        EndArray();
        return true;
    }
};
