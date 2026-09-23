#include "pch.h"
#include "Core/Serialization/WindowsBinReader.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>

// Writer와 동일한 숫자 표현을 사용하는지 확인한다.
static_assert(sizeof(int32) == 4 && sizeof(uint32) == 4 && sizeof(uint64) == 8);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

namespace
{
    // 헤더의 little-endian 네 바이트를 uint32로 변환한다.
    uint32 DecodeUInt32(const unsigned char* Data)
    {
        // 각 바이트를 원래 비트 위치로 이동하여 결합한다.
        uint32 Value = 0;
        for (size_t Index = 0; Index < 4; ++Index)
            Value |= static_cast<uint32>(Data[Index]) << (Index * 8);
        return Value;
    }
}

// 파일 헤더를 검사하고 바이너리 본문을 메모리에 읽는다.
FWindowsBinReader::FWindowsBinReader(const std::filesystem::path& Path)
{
    // 파일 끝에서 크기를 확인한 뒤 헤더 위치로 돌아간다.
    std::ifstream In;
    In.exceptions(std::ios::failbit | std::ios::badbit);
    In.open(Path, std::ios::binary | std::ios::ate);
    const std::streamoff FileSize = In.tellg();
    constexpr size_t HeaderSize = 12;
    if (FileSize < static_cast<std::streamoff>(HeaderSize))
        throw std::runtime_error("Binary file header is incomplete.");
    In.seekg(0, std::ios::beg);

    TArray<unsigned char> Header(HeaderSize);
    In.read(reinterpret_cast<char*>(Header.GetData()),
        static_cast<std::streamsize>(HeaderSize));

    // 식별자와 버전을 확인하여 다른 파일이나 지원하지 않는 형식을 거부한다.
    if (Header[0] != 'F' || Header[1] != 'B' || Header[2] != 'I' || Header[3] != 'N')
        throw std::runtime_error("Invalid binary file signature.");
    const uint32 Version = DecodeUInt32(Header.GetData() + 4);
    const uint32 PayloadSize = DecodeUInt32(Header.GetData() + 8);
    if (Version != 1)
        throw std::runtime_error("Unsupported binary archive version.");

    // 실제 파일 크기와 TArray가 보관할 수 있는 크기를 할당 전에 검사한다.
    if (static_cast<uint64>(FileSize) != static_cast<uint64>(HeaderSize) + PayloadSize)
        throw std::runtime_error("Binary file size does not match its header.");
    if (PayloadSize > static_cast<uint32>((std::numeric_limits<int32>::max)()))
        throw std::length_error("Binary payload exceeds TArray capacity.");

    // 본문만 보관하고 이후 필드 읽기는 메모리에서 처리한다.
    Bytes.SetNum(PayloadSize);
    constexpr size_t ChunkSize = 1024 * 1024;
    const size_t TotalSize = static_cast<size_t>(Bytes.Num());
    size_t Offset = 0;
    while (Offset < TotalSize)
    {
        const size_t Count = (std::min)(ChunkSize, TotalSize - Offset);
        In.read(reinterpret_cast<char*>(Bytes.GetData() + Offset),
            static_cast<std::streamsize>(Count));
        Offset += Count;
    }
}

// 파일에서 Reader를 생성하고 소유권을 반환한다.
std::unique_ptr<FWindowsBinReader> FWindowsBinReader::FromFile(const std::filesystem::path& Path)
{
    // Reader 자체를 복사하지 않고 최종 위치에 직접 생성한다.
    return std::make_unique<FWindowsBinReader>(Path);
}

// 요청한 바이트가 본문에 남아 있는지 검사한다.
void FWindowsBinReader::RequireBytes(size_t Size) const
{
    // 덧셈 대신 남은 크기와 비교하여 위치 계산의 오버플로를 피한다.
    const size_t TotalSize = static_cast<size_t>(Bytes.Num());
    if (Position > TotalSize || Size > TotalSize - Position)
        throw std::runtime_error("Unexpected end of binary payload.");
}

// little-endian 바이트를 부호 없는 정수로 읽는다.
uint64 FWindowsBinReader::ReadUnsigned(size_t ByteCount)
{
    // 접근할 전체 범위를 확인한 뒤 바이트를 결합한다.
    RequireBytes(ByteCount);
    uint64 Value = 0;
    for (size_t Index = 0; Index < ByteCount; ++Index)
        Value |= static_cast<uint64>(Bytes[Position + Index]) << (Index * 8);
    Position += ByteCount;
    return Value;
}

// 길이를 검사한 뒤 문자열 내용을 읽는다.
FString FWindowsBinReader::ReadString()
{
    // 파일에 실제 문자열 바이트가 있는지 확인한 뒤 메모리를 할당한다.
    const uint32 Length = static_cast<uint32>(ReadUnsigned(sizeof(uint32)));
    RequireBytes(Length);
    if (Length == 0) return {};

    FString Value(reinterpret_cast<const char*>(Bytes.GetData() + Position),
        static_cast<size_t>(Length));
    Position += Length;
    return Value;
}

// 배열을 할당하기 전에 최소 필요 바이트 수를 검사한다.
void FWindowsBinReader::CheckArraySize(uint32 Count, size_t MinimumElementBytes) const
{
    if (Count > static_cast<uint32>((std::numeric_limits<int32>::max)()))
        throw std::length_error("Binary collection exceeds TArray capacity.");
    if (MinimumElementBytes == 0)
        throw std::invalid_argument("Minimum element size must be positive.");

    // 곱셈 없이 비교하여 잘못된 개수로 인한 오버플로와 선행 할당을 방지한다.
    RequireBytes(0);
    const size_t Remaining = static_cast<size_t>(Bytes.Num()) - Position;
    if (static_cast<size_t>(Count) > Remaining / MinimumElementBytes)
        throw std::runtime_error("Binary collection count exceeds remaining data.");
}

// 현재 위치에서 값을 읽을 수 있는지 확인한다.
void FWindowsBinReader::CheckValueScope() const
{
    // 배열과 맵에서는 원소 범위에 들어간 뒤 값을 읽어야 한다.
    if (!Stack.IsEmpty() && (Stack.Last().Kind == EScope::Array || Stack.Last().Kind == EScope::Map))
        throw std::runtime_error("Enter a collection element before reading a value.");
}

// 현재 범위의 종류를 확인한다.
void FWindowsBinReader::RequireScope(EScope Kind) const
{
    // 잘못된 End 호출이나 원소 진입을 차단한다.
    if (Stack.IsEmpty() || Stack.Last().Kind != Kind)
        throw std::runtime_error("Binary archive scope mismatch.");
}

// 배열이나 맵의 개수를 읽고 범위를 시작한다.
void FWindowsBinReader::BeginCollection(EScope Kind, uint32& Count)
{
    CheckValueScope();

    // 저장된 개수를 읽되 TArray의 개수 범위를 넘는 값은 거부한다.
    const uint32 ReadCount = static_cast<uint32>(ReadUnsigned(sizeof(uint32)));
    if (ReadCount > static_cast<uint32>((std::numeric_limits<int32>::max)()))
        throw std::length_error("Binary collection exceeds TArray capacity.");

    // 맵은 원소마다 최소한 문자열 키의 길이 필드가 존재한다.
    if (Kind == EScope::Map)
        CheckArraySize(ReadCount, sizeof(uint32));

    Stack.Add({ Kind, ReadCount, 0 });
    Count = ReadCount;
}

// 배열이나 맵의 다음 원소 범위를 시작한다.
void FWindowsBinReader::BeginElement(EScope ParentKind, EScope ElementKind, uint32 Index)
{
    RequireScope(ParentKind);

    // 파일이 순서대로 기록되어 있으므로 임의 순서의 접근을 허용하지 않는다.
    if (Index != Stack.Last().NextIndex || Index >= Stack.Last().Count)
        throw std::runtime_error("Invalid binary collection element order.");

    ++Stack.Last().NextIndex;
    Stack.Add({ ElementKind, 0, 0 });
}

// 범위의 종류와 처리 개수를 검사하고 종료한다.
void FWindowsBinReader::EndScope(EScope Kind)
{
    RequireScope(Kind);

    // 선언된 원소를 모두 처리한 컬렉션만 종료한다.
    if ((Kind == EScope::Array || Kind == EScope::Map)
        && Stack.Last().NextIndex != Stack.Last().Count)
        throw std::runtime_error("Binary collection is incomplete.");

    Stack.Pop();
}

// 객체의 읽기 범위를 시작한다.
bool FWindowsBinReader::BeginObject(const char* /*Name*/)
{
    // 객체 이름이나 경계 바이트는 없으므로 읽기 범위만 추가한다.
    CheckValueScope();
    Stack.Add({ EScope::Object, 0, 0 });
    return true;
}

// 배열 개수를 읽고 배열 범위를 시작한다.
bool FWindowsBinReader::BeginArray(const char* /*Name*/, uint32& Count)
{
    BeginCollection(EScope::Array, Count);
    return true;
}

// 순서에 맞는 배열 원소의 읽기 범위를 시작한다.
void FWindowsBinReader::BeginArrayElement(uint32 Index)
{
    BeginElement(EScope::Array, EScope::ArrayElement, Index);
}

// 맵 개수를 읽고 맵 범위를 시작한다.
bool FWindowsBinReader::BeginMap(const char* /*Name*/, uint32& Count)
{
    BeginCollection(EScope::Map, Count);
    return true;
}

// 맵 원소의 키를 읽고 값의 범위를 시작한다.
void FWindowsBinReader::BeginMapEntry(uint32 Index, FString& Key)
{
    // 현재 원소의 키를 읽은 후 이어지는 바이트를 값으로 처리한다.
    BeginElement(EScope::Map, EScope::MapEntry, Index);
    Key = ReadString();
}

// 부호 있는 32비트 정수를 복원한다.
bool FWindowsBinReader::SerializeValue(const char* /*Name*/, int32& Value)
{
    // 저장된 비트를 유지하여 음수까지 원래 값으로 복원한다.
    CheckValueScope();
    const uint32 Bits = static_cast<uint32>(ReadUnsigned(sizeof(uint32)));
    Value = std::bit_cast<int32>(Bits);
    return true;
}

// 부호 없는 32비트 정수를 복원한다.
bool FWindowsBinReader::SerializeValue(const char* /*Name*/, uint32& Value)
{
    CheckValueScope();
    Value = static_cast<uint32>(ReadUnsigned(sizeof(uint32)));
    return true;
}

// 32비트 실수의 비트 표현을 복원한다.
bool FWindowsBinReader::SerializeValue(const char* /*Name*/, float& Value)
{
    // 비트를 실수로 해석한 뒤 유효한 수치인 경우에만 반영한다.
    CheckValueScope();
    const uint32 Bits = static_cast<uint32>(ReadUnsigned(sizeof(uint32)));
    const float ReadValue = std::bit_cast<float>(Bits);
    if (!std::isfinite(ReadValue))
        throw std::runtime_error("Non-finite binary float.");
    Value = ReadValue;
    return true;
}

// 64비트 실수의 비트 표현을 복원한다.
bool FWindowsBinReader::SerializeValue(const char* /*Name*/, double& Value)
{
    CheckValueScope();
    const double ReadValue = std::bit_cast<double>(ReadUnsigned(sizeof(uint64)));
    if (!std::isfinite(ReadValue))
        throw std::runtime_error("Non-finite binary double.");
    Value = ReadValue;
    return true;
}

// 한 바이트로 기록된 논리값을 복원한다.
bool FWindowsBinReader::SerializeValue(const char* /*Name*/, bool& Value)
{
    // 파일 규칙에서 허용하는 0과 1만 논리값으로 받아들인다.
    CheckValueScope();
    const uint64 ReadValue = ReadUnsigned(1);
    if (ReadValue > 1)
        throw std::runtime_error("Invalid binary bool.");
    Value = ReadValue != 0;
    return true;
}

// 길이와 바이트로 기록된 문자열을 복원한다.
bool FWindowsBinReader::SerializeValue(const char* /*Name*/, FString& Value)
{
    CheckValueScope();
    Value = ReadString();
    return true;
}

// 세 개의 float를 읽으며 잘못된 바이너리는 예외로 처리한다.
void FWindowsBinReader::Float3OrDefault(const char* Name, TArray<float>& Values, float /*Default*/)
{
    // 일반 배열 형식으로 기록된 원소 개수가 정확히 세 개인지 먼저 검사한다.
    uint32 Count = 0;
    BeginArray(Name, Count);
    if (Count != 3)
        throw std::runtime_error("Expected exactly three binary float values.");
    CheckArraySize(Count, sizeof(float));

    // 세 성분을 모두 복원한 뒤 호출자의 배열에 반영한다.
    TArray<float> Loaded(3);
    for (uint32 Index = 0; Index < Count; ++Index)
    {
        BeginArrayElement(Index);
        Field(nullptr, Loaded[Index]);
        EndArrayElement();
    }
    EndArray();
    Values = std::move(Loaded);
}

// 모든 범위와 본문을 끝까지 읽었는지 확인한다.
void FWindowsBinReader::RequireEnd() const
{
    // 호출자가 닫지 않은 범위와 해석하지 않은 잔여 데이터를 검사한다.
    if (!Stack.IsEmpty())
        throw std::runtime_error("Binary archive scopes are not closed.");
    if (Position != static_cast<size_t>(Bytes.Num()))
        throw std::runtime_error("Unread bytes remain in binary payload.");
}