#include "pch.h"
#include "Core/Serialization/WindowsBinWriter.h"
#include <Windows.h>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

// 파일 형식에서 사용하는 정수와 실수의 크기를 확인한다.
static_assert(sizeof(int32) == 4 && sizeof(uint32) == 4 && sizeof(uint64) == 8);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559); //부동 소수점 형식이 표준을 따르는지
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

// 원본 바이트를 본문 버퍼에 추가한다.
void FWindowsBinWriter::AppendBytes(const void* Data, size_t Size)
{
    // TArray::Num의 반환 범위 안에서 본문 크기를 유지한다.
    const size_t CurrentSize = static_cast<size_t>(Bytes.Num());
    const size_t Limit = static_cast<size_t>((std::numeric_limits<int32>::max)());
    if (Size > Limit - CurrentSize)
        throw std::length_error("Binary payload exceeds TArray capacity.");
    if (Size == 0) return;

    // 추가 영역을 확보한 뒤 바이트를 한 번에 복사한다.
    Bytes.SetNum(CurrentSize + Size);
    std::memcpy(Bytes.GetData() + CurrentSize, Data, Size);
}

// 정수를 지정한 바이트 수의 little-endian으로 기록한다.
void FWindowsBinWriter::WriteUnsigned(uint64 Value, size_t ByteCount)
{
    // 낮은 자리의 바이트부터 배치하여 CPU 메모리 배치에 의존하지 않는다.
    unsigned char Encoded[8]{};
    for (size_t Index = 0; Index < ByteCount; ++Index)
        Encoded[Index] = static_cast<unsigned char>((Value >> (Index * 8)) & 0xFFu);
    AppendBytes(Encoded, ByteCount);
}

// 문자열의 바이트 길이와 내용을 기록한다.
void FWindowsBinWriter::WriteString(const FString& Value)
{
    // 길이 정보까지 포함한 전체 크기가 본문에 들어가는지 먼저 검사한다.
    const uint64 Required = sizeof(uint32) + static_cast<uint64>(Value.size());
    const uint64 Limit = static_cast<uint64>((std::numeric_limits<int32>::max)());
    const uint64 Remaining = Limit - static_cast<uint64>(Bytes.Num());
    if (Required > Remaining)
        throw std::length_error("Binary string exceeds payload capacity.");

    WriteUnsigned(static_cast<uint32>(Value.size()), sizeof(uint32));
    AppendBytes(Value.data(), Value.size());
}

// 현재 위치에 값을 기록할 수 있는지 확인한다.
void FWindowsBinWriter::CheckValueScope() const
{
    // 배열과 맵에서는 원소 범위에 들어간 뒤 값을 기록해야 한다.
    if (!Stack.IsEmpty() && (Stack.Last().Kind == EScope::Array || Stack.Last().Kind == EScope::Map))
        throw std::runtime_error("Enter a collection element before writing a value.");
}

// 현재 범위의 종류를 확인한다.
void FWindowsBinWriter::RequireScope(EScope Kind) const
{
    // 잘못된 End 호출이나 원소 진입을 차단한다.
    if (Stack.IsEmpty() || Stack.Last().Kind != Kind)
        throw std::runtime_error("Binary archive scope mismatch.");
}

// 배열이나 맵의 개수를 기록하고 범위를 시작한다.
void FWindowsBinWriter::BeginCollection(EScope Kind, uint32 Count)
{
    CheckValueScope();

    // 현재 TArray의 Num 반환 타입과 동일한 개수 범위를 사용한다.
    if (Count > static_cast<uint32>((std::numeric_limits<int32>::max)()))
        throw std::length_error("Binary collection exceeds TArray capacity.");

    WriteUnsigned(Count, sizeof(uint32));
    Stack.Add({ Kind, Count, 0 });
}

// 배열이나 맵의 다음 원소 범위를 시작한다.
void FWindowsBinWriter::BeginElement(EScope ParentKind, EScope ElementKind, uint32 Index)
{
    RequireScope(ParentKind);

    // 원소를 0부터 선언한 개수만큼 순서대로 처리하도록 제한한다.
    if (Index != Stack.Last().NextIndex || Index >= Stack.Last().Count)
        throw std::runtime_error("Invalid binary collection element order.");

    ++Stack.Last().NextIndex;
    Stack.Add({ ElementKind, 0, 0 });
}

// 범위의 종류와 처리 개수를 검사하고 종료한다.
void FWindowsBinWriter::EndScope(EScope Kind)
{
    RequireScope(Kind);

    // 배열과 맵은 선언한 원소 개수를 모두 처리해야 종료할 수 있다.
    if ((Kind == EScope::Array || Kind == EScope::Map)
        && Stack.Last().NextIndex != Stack.Last().Count)
        throw std::runtime_error("Binary collection is incomplete.");

    Stack.Pop();
}

// 객체의 기록 범위를 시작한다.
bool FWindowsBinWriter::BeginObject(const char* /*Name*/)
{
    // 객체 이름과 경계 바이트는 기록하지 않고 호출 순서만 검사한다.
    CheckValueScope();
    Stack.Add({ EScope::Object, 0, 0 });
    return true;
}

// 객체의 기록 범위를 종료한다.
void FWindowsBinWriter::EndObject()
{
    EndScope(EScope::Object);
}

// 배열 개수를 기록하고 배열 범위를 시작한다.
bool FWindowsBinWriter::BeginArray(const char* /*Name*/, uint32& Count)
{
    BeginCollection(EScope::Array, Count);
    return true;
}

// 순서에 맞는 배열 원소의 기록 범위를 시작한다.
void FWindowsBinWriter::BeginArrayElement(uint32 Index)
{
    BeginElement(EScope::Array, EScope::ArrayElement, Index);
}

// 현재 배열 원소의 기록 범위를 종료한다.
void FWindowsBinWriter::EndArrayElement()
{
    EndScope(EScope::ArrayElement);
}

// 모든 원소가 처리된 배열을 종료한다.
void FWindowsBinWriter::EndArray()
{
    EndScope(EScope::Array);
}

// 맵 개수를 기록하고 맵 범위를 시작한다.
bool FWindowsBinWriter::BeginMap(const char* /*Name*/, uint32& Count)
{
    BeginCollection(EScope::Map, Count);
    return true;
}

// 맵 원소의 키를 기록하고 값의 범위를 시작한다.
void FWindowsBinWriter::BeginMapEntry(uint32 Index, FString& Key)
{
    // 맵 키는 필드 이름이 아니라 실제 데이터이므로 파일에 기록한다.
    BeginElement(EScope::Map, EScope::MapEntry, Index);
    WriteString(Key);
}

// 현재 맵 원소의 기록 범위를 종료한다.
void FWindowsBinWriter::EndMapEntry()
{
    EndScope(EScope::MapEntry);
}

// 모든 원소가 처리된 맵을 종료한다.
void FWindowsBinWriter::EndMap()
{
    EndScope(EScope::Map);
}

// 부호 있는 32비트 정수를 기록한다.
bool FWindowsBinWriter::SerializeValue(const char* /*Name*/, int32& Value)
{
    // 음수도 원래 비트 표현을 유지하여 저장한다.
    CheckValueScope();
    WriteUnsigned(std::bit_cast<uint32>(Value), sizeof(uint32));
    return true;
}

// 부호 없는 32비트 정수를 기록한다.
bool FWindowsBinWriter::SerializeValue(const char* /*Name*/, uint32& Value)
{
    CheckValueScope();
    WriteUnsigned(Value, sizeof(uint32));
    return true;
}

// 32비트 실수의 비트 표현을 기록한다.
bool FWindowsBinWriter::SerializeValue(const char* /*Name*/, float& Value)
{
    // 실수를 정수 값으로 변환하지 않고 비트 그대로 기록한다.
    CheckValueScope();
    if (!std::isfinite(Value))
        throw std::runtime_error("Cannot write a non-finite binary float.");
    WriteUnsigned(std::bit_cast<uint32>(Value), sizeof(uint32));
    return true;
}

// 64비트 실수의 비트 표현을 기록한다.
bool FWindowsBinWriter::SerializeValue(const char* /*Name*/, double& Value)
{
    CheckValueScope();
    if (!std::isfinite(Value))
        throw std::runtime_error("Cannot write a non-finite binary double.");
    WriteUnsigned(std::bit_cast<uint64>(Value), sizeof(uint64));
    return true;
}

// 논리값을 한 바이트로 기록한다.
bool FWindowsBinWriter::SerializeValue(const char* /*Name*/, bool& Value)
{
    // C++ bool의 메모리 표현 대신 파일 규칙인 0 또는 1을 사용한다.
    CheckValueScope();
    WriteUnsigned(Value ? 1u : 0u, 1);
    return true;
}

// 문자열의 바이트 길이와 내용을 기록한다.
bool FWindowsBinWriter::SerializeValue(const char* /*Name*/, FString& Value)
{
    CheckValueScope();
    WriteString(Value);
    return true;
}

// 세 개의 float를 공통 배열 형식으로 기록한다.
void FWindowsBinWriter::Float3OrDefault(const char* Name, TArray<float>& Values, float /*Default*/)
{
    // 저장 모드에서는 기본값을 적용하지 않고 입력 크기를 검사한다.
    if (Values.Num() != 3)
        throw std::runtime_error("Expected exactly three float values.");
    Field(Name, Values);
}

// 헤더와 본문을 임시 파일에 기록한 뒤 대상 파일을 교체한다.
void FWindowsBinWriter::SaveToFile(const std::filesystem::path& Path) const
{
    // 범위가 열려 있으면 불완전한 문서이므로 파일을 생성하지 않는다.
    if (!Stack.IsEmpty())
        throw std::runtime_error("Binary archive scopes are not closed.");
    if (Path.empty())
        throw std::runtime_error("Binary output path is empty.");

    // 식별자 4바이트, 버전 4바이트, 본문 크기 4바이트를 구성한다.
    TArray<unsigned char> Header(12);
    Header[0] = 'F';
    Header[1] = 'B';
    Header[2] = 'I';
    Header[3] = 'N';
    constexpr uint32 Version = 1;
    const uint32 PayloadSize = static_cast<uint32>(Bytes.Num());
    for (size_t Index = 0; Index < 4; ++Index)
    {
        Header[4 + Index] = static_cast<unsigned char>((Version >> (Index * 8)) & 0xFFu);
        Header[8 + Index] = static_cast<unsigned char>((PayloadSize >> (Index * 8)) & 0xFFu);
    }

    // 대상 파일과 같은 디렉터리에 임시 파일을 생성한다.
    const std::filesystem::path Target = std::filesystem::absolute(Path);
    wchar_t TempName[MAX_PATH]{};
    if (!GetTempFileNameW(Target.parent_path().c_str(), L"bin", 0, TempName))
        throw std::system_error(GetLastError(), std::system_category());
    const std::filesystem::path Temp(TempName);

    try
    {
        std::ofstream Out;
        Out.exceptions(std::ios::failbit | std::ios::badbit);
        Out.open(Temp, std::ios::binary | std::ios::trunc);
        Out.write(reinterpret_cast<const char*>(Header.GetData()),
            static_cast<std::streamsize>(Header.Num()));

        // Win32의 streamsize 범위를 넘지 않도록 본문을 나누어 기록한다.
        constexpr size_t ChunkSize = 1024 * 1024;
        const size_t TotalSize = static_cast<size_t>(Bytes.Num());
        size_t Offset = 0;
        while (Offset < TotalSize)
        {
            const size_t Count = (std::min)(ChunkSize, TotalSize - Offset);
            Out.write(reinterpret_cast<const char*>(Bytes.GetData() + Offset),
                static_cast<std::streamsize>(Count));
            Offset += Count;
        }
        Out.flush();
        Out.close();

        // 기록을 완료한 파일만 최종 경로로 교체한다.
        if (!MoveFileExW(Temp.c_str(), Target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(), std::system_category());
    }
    catch (...)
    {
        // 실패한 임시 파일을 제거하고 원래 오류를 호출자에게 전달한다.
        std::error_code Ignored;
        std::filesystem::remove(Temp, Ignored);
        throw;
    }
}
