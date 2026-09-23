#pragma once
#include "Core/Serialization/Archive.h"
#include "Core/Container/Array.h"
#include <cstddef>
#include <filesystem>

class FWindowsBinWriter final : public FArchive
{
public:
    // 빈 바이너리 본문을 가진 Writer를 생성한다.
    FWindowsBinWriter() = default;
    // Writer의 복사와 소유 데이터 복제를 금지한다.
    FWindowsBinWriter(const FWindowsBinWriter&) = delete;
    FWindowsBinWriter& operator=(const FWindowsBinWriter&) = delete;

    bool IsLoading() const override { return false; }

    // 객체의 기록 범위를 시작한다.
    bool BeginObject(const char* Name) override;
    // 객체의 기록 범위를 종료한다.
    void EndObject() override;
    // 배열 개수를 기록하고 배열 범위를 시작한다.
    bool BeginArray(const char* Name, uint32& Count) override;
    // 순서에 맞는 배열 원소의 기록 범위를 시작한다.
    void BeginArrayElement(uint32 Index) override;
    // 현재 배열 원소의 기록 범위를 종료한다.
    void EndArrayElement() override;
    // 모든 원소가 처리된 배열을 종료한다.
    void EndArray() override;
    // 맵 개수를 기록하고 맵 범위를 시작한다.
    bool BeginMap(const char* Name, uint32& Count) override;
    // 맵 원소의 키를 기록하고 값의 범위를 시작한다.
    void BeginMapEntry(uint32 Index, FString& Key) override;
    // 현재 맵 원소의 기록 범위를 종료한다.
    void EndMapEntry() override;
    // 모든 원소가 처리된 맵을 종료한다.
    void EndMap() override;

    // 세 개의 float를 공통 배열 형식으로 기록한다.
    void Float3OrDefault(const char* Name, TArray<float>& Values, float Default) override;
    // 헤더와 본문을 임시 파일에 기록한 뒤 대상 파일을 교체한다.
    void SaveToFile(const std::filesystem::path& Path) const;

protected:
    bool SerializeValue(const char* Name, int32& Value) override;
    bool SerializeValue(const char* Name, uint32& Value) override;
    bool SerializeValue(const char* Name, float& Value) override;
    bool SerializeValue(const char* Name, double& Value) override;
    bool SerializeValue(const char* Name, bool& Value) override;
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

    // 원본 바이트를 본문 버퍼에 추가한다.
    void AppendBytes(const void* Data, size_t Size);
    // 정수를 지정한 바이트 수의 little-endian으로 기록한다.
    void WriteUnsigned(uint64 Value, size_t ByteCount);
    // 문자열 길이와 원본 바이트를 기록한다.
    void WriteString(const FString& Value);
    // 현재 위치에 값을 기록할 수 있는지 확인한다.
    void CheckValueScope() const;
    // 현재 범위의 종류를 확인한다.
    void RequireScope(EScope Kind) const;
    // 배열이나 맵의 개수를 기록하고 범위를 시작한다.
    void BeginCollection(EScope Kind, uint32 Count);
    // 배열이나 맵의 다음 원소 범위를 시작한다.
    void BeginElement(EScope ParentKind, EScope ElementKind, uint32 Index);
    // 범위의 종류와 처리 개수를 검사하고 종료한다.
    void EndScope(EScope Kind);
};
