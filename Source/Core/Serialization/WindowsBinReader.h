#pragma once
#include "Core/Serialization/Archive.h"
#include "Core/Container/Array.h"
#include <cstddef>
#include <filesystem>
#include <memory>

class FWindowsBinReader final : public FArchive
{
public:
    // 파일 헤더를 검사하고 바이너리 본문을 메모리에 읽는다.
    explicit FWindowsBinReader(const std::filesystem::path& Path);
    // Reader의 데이터와 읽기 상태 복사를 금지한다.
    FWindowsBinReader(const FWindowsBinReader&) = delete;
    // Reader의 복사 대입을 금지한다.
    FWindowsBinReader& operator=(const FWindowsBinReader&) = delete;

    // 파일에서 Reader를 생성하고 소유권을 반환한다.
    static std::unique_ptr<FWindowsBinReader> FromFile(const std::filesystem::path& Path);
    // Reader는 항상 읽기 모드로 동작한다.
    bool IsLoading() const override { return true; }
    // 모든 범위와 본문을 끝까지 읽었는지 확인한다.
    void RequireEnd() const;
    // 배열을 할당하기 전에 최소 필요 바이트 수를 검사한다.
    void CheckArraySize(uint32 Count, size_t MinimumElementBytes) const override;

    // 객체의 읽기 범위를 시작한다.
    bool BeginObject(const char* Name) override;
    // 객체의 읽기 범위를 종료한다.
    void EndObject() override { EndScope(EScope::Object); }
    // 배열 개수를 읽고 배열 범위를 시작한다.
    bool BeginArray(const char* Name, uint32& Count) override;
    // 순서에 맞는 배열 원소의 읽기 범위를 시작한다.
    void BeginArrayElement(uint32 Index) override;
    // 현재 배열 원소의 읽기 범위를 종료한다.
    void EndArrayElement() override { EndScope(EScope::ArrayElement); }
    // 모든 원소를 처리한 배열을 종료한다.
    void EndArray() override { EndScope(EScope::Array); }
    // 맵 개수를 읽고 맵 범위를 시작한다.
    bool BeginMap(const char* Name, uint32& Count) override;
    // 맵 원소의 키를 읽고 값의 범위를 시작한다.
    void BeginMapEntry(uint32 Index, FString& Key) override;
    // 현재 맵 원소의 읽기 범위를 종료한다.
    void EndMapEntry() override { EndScope(EScope::MapEntry); }
    // 모든 원소를 처리한 맵을 종료한다.
    void EndMap() override { EndScope(EScope::Map); }

    // 세 개의 float를 읽으며 잘못된 바이너리는 예외로 처리한다.
    void Float3OrDefault(const char* Name, TArray<float>& Values, float Default) override;

protected:
    // 부호 있는 32비트 정수를 복원한다.
    bool SerializeValue(const char* Name, int32& Value) override;
    // 부호 없는 32비트 정수를 복원한다.
    bool SerializeValue(const char* Name, uint32& Value) override;
    // 32비트 실수의 비트 표현을 복원한다.
    bool SerializeValue(const char* Name, float& Value) override;
    // 64비트 실수의 비트 표현을 복원한다.
    bool SerializeValue(const char* Name, double& Value) override;
    // 한 바이트로 기록된 논리값을 복원한다.
    bool SerializeValue(const char* Name, bool& Value) override;
    // 길이와 바이트로 기록된 문자열을 복원한다.
    bool SerializeValue(const char* Name, FString& Value) override;

private:
    enum class EScope
    {
        Object,
        Array,
        ArrayElement,
        Map,
        MapEntry
    };

    struct FStackFrame
    {
        EScope Kind;
        uint32 Count = 0;
        uint32 NextIndex = 0;
    };

    TArray<unsigned char> Bytes;
    TArray<FStackFrame> Stack;
    size_t Position = 0;

    // 요청한 바이트가 본문에 남아 있는지 검사한다.
    void RequireBytes(size_t Size) const;
    // little-endian 바이트를 부호 없는 정수로 읽는다.
    uint64 ReadUnsigned(size_t ByteCount);
    // 길이를 검사한 뒤 문자열 내용을 읽는다.
    FString ReadString();
    // 현재 위치에서 값을 읽을 수 있는지 확인한다.
    void CheckValueScope() const;
    // 현재 범위의 종류를 확인한다.
    void RequireScope(EScope Kind) const;
    // 배열이나 맵의 개수를 읽고 범위를 시작한다.
    void BeginCollection(EScope Kind, uint32& Count);
    // 배열이나 맵의 다음 원소 범위를 시작한다.
    void BeginElement(EScope ParentKind, EScope ElementKind, uint32 Index);
    // 범위의 종류와 처리 개수를 검사하고 종료한다.
    void EndScope(EScope Kind);
};