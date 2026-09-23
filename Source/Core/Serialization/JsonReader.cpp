#include "pch.h"
#include "Core/Serialization/JsonReader.h"
#include "Core/Util/File.h"
#include <cmath>

// JSON 문자열을 파싱하고 최초 읽기 위치를 설정한다.
FJsonReader::FJsonReader(FStringView Text)
    : Root(FJson::parse(Text.begin(), Text.end()))
{
    // 입력 문자열의 수명과 무관하게 Reader가 파싱 결과를 소유한다.
    ResetToRoot();
}

// 파일 내용을 읽어 Reader를 직접 생성한다.
std::unique_ptr<FJsonReader> FJsonReader::FromFile(const std::filesystem::path& Path)
{
    // path 기반 읽기를 사용하고 반환 객체는 복사 없이 직접 생성한다.
    const FString Text = File::ReadTextFromPath(Path);
    return std::make_unique<FJsonReader>(Text);
}

// 같은 JSON 문서를 루트부터 다시 읽도록 설정한다.
void FJsonReader::ResetToRoot()
{
    // 이전 탐색 위치만 제거하고 파싱된 문서는 유지한다.
    Stack.clear();
    Push(Root);
}

// 현재 위치에서 필드를 찾고 누락이면 nullptr를 반환한다.
const FJsonReader::FJson* FJsonReader::Find(const char* Name) const
{
    const FJson& Current = *Stack.back().Node;

    // 이름이 없으면 현재 배열 원소나 루트 자체를 사용한다.
    if (Name == nullptr) return &Current;
    if (!Current.is_object())
        throw std::runtime_error("Named JSON field requires an object.");

    const auto It = Current.find(Name);
    return It == Current.end() ? nullptr : &It.value();
}

// 읽기 위치와 맵 순회 상태를 스택에 추가한다.
void FJsonReader::Push(const FJson& Node)
{
    // 각 범위가 자신의 순회 위치를 보관하도록 한다.
    Stack.push_back({ &Node, Node.cbegin(), 0 });
}

// 현재 위치를 제거하고 부모 위치로 돌아간다.
void FJsonReader::Pop()
{
    // 문서의 루트 위치는 제거하지 않는다.
    if (Stack.size() <= 1)
        throw std::runtime_error("Cannot leave JSON root.");
    Stack.pop_back();
}

// 숫자의 타입과 범위를 확인한 뒤 값을 반영한다.
template<typename T>
bool FJsonReader::ReadNumber(const char* Name, T& Value)
{
    const FJson* Node = Find(Name);
    if (Node == nullptr) return false;

    // 정수 필드에는 정수만 허용하고 실수 필드에는 모든 숫자를 허용한다.
    if constexpr (std::is_integral_v<T>)
    {
        if (!Node->is_number_integer())
            throw std::runtime_error("JSON field must be an integer.");
    }
    else
    {
        if (!Node->is_number())
            throw std::runtime_error("JSON field must be a number.");
    }

    // 현재 지원하는 32비트 정수와 실수 타입의 범위를 검사한다.
    const double Number = Node->get<double>();
    if (!std::isfinite(Number) ||
        Number < static_cast<double>((std::numeric_limits<T>::lowest)()) ||
        Number > static_cast<double>((std::numeric_limits<T>::max)()))
        throw std::runtime_error("JSON number is outside the target range.");

    Value = static_cast<T>(Number);
    return true;
}

// int32 읽기를 공통 숫자 처리로 전달한다.
bool FJsonReader::SerializeValue(const char* Name, int32& Value) { return ReadNumber(Name, Value); }
// uint32 읽기를 공통 숫자 처리로 전달한다.
bool FJsonReader::SerializeValue(const char* Name, uint32& Value) { return ReadNumber(Name, Value); }
// float 읽기를 공통 숫자 처리로 전달한다.
bool FJsonReader::SerializeValue(const char* Name, float& Value) { return ReadNumber(Name, Value); }
// double 읽기를 공통 숫자 처리로 전달한다.
bool FJsonReader::SerializeValue(const char* Name, double& Value) { return ReadNumber(Name, Value); }

// bool 타입을 확인한 뒤 값을 읽는다.
bool FJsonReader::SerializeValue(const char* Name, bool& Value)
{
    const FJson* Node = Find(Name);
    if (Node == nullptr) return false;

    // 숫자나 문자열을 bool로 암묵 변환하지 않는다.
    if (!Node->is_boolean())
        throw std::runtime_error("JSON field must be a boolean.");
    Value = Node->get<bool>();
    return true;
}

// 문자열 타입을 확인한 뒤 값을 읽는다.
bool FJsonReader::SerializeValue(const char* Name, FString& Value)
{
    const FJson* Node = Find(Name);
    if (Node == nullptr) return false;

    // 문자열이 아닌 값은 형식 오류로 처리한다.
    if (!Node->is_string())
        throw std::runtime_error("JSON field must be a string.");
    Value = Node->get<FString>();
    return true;
}

// 객체나 배열을 확인하고 읽기 범위에 진입한다.
bool FJsonReader::BeginContainer(const char* Name, bool bArray, uint32* Count)
{
    const FJson* Node = Find(Name);
    if (Node == nullptr) return false;

    // 필드 누락과 잘못된 컨테이너 타입을 구분한다.
    if (bArray ? !Node->is_array() : !Node->is_object())
        throw std::runtime_error("JSON container type mismatch.");

    // 개수를 요청한 경우 변환 가능한 범위인지 먼저 검사한다.
    if (Count != nullptr)
    {
        if (Node->size() > static_cast<size_t>((std::numeric_limits<int32>::max)()))
            throw std::length_error("JSON container exceeds TArray capacity.");
        *Count = static_cast<uint32>(Node->size());
    }
    Push(*Node);
    return true;
}

// 이름에 해당하는 객체로 진입한다.
bool FJsonReader::BeginObject(const char* Name) { return BeginContainer(Name, false, nullptr); }
// 배열로 진입하고 개수를 읽는다.
bool FJsonReader::BeginArray(const char* Name, uint32& Count) { return BeginContainer(Name, true, &Count); }
// 문자열 키 컬렉션으로 진입하고 개수를 읽는다.
bool FJsonReader::BeginMap(const char* Name, uint32& Count) { return BeginContainer(Name, false, &Count); }

// 배열의 지정된 원소에 진입한다.
void FJsonReader::BeginArrayElement(uint32 Index)
{
    const FJson& Array = *Stack.back().Node;

    // 배열 범위를 확인한 뒤 원소를 현재 위치로 설정한다.
    if (!Array.is_array() || Index >= Array.size())
        throw std::runtime_error("JSON array index is out of range.");
    Push(Array.at(Index));
}

// 맵의 다음 원소에 진입하고 키를 반환한다.
void FJsonReader::BeginMapEntry(uint32 Index, FString& Key)
{
    FStackFrame& Frame = Stack.back();

    // 저장된 키의 순서대로 한 번씩 방문하도록 한다.
    if (!Frame.Node->is_object() || Index != Frame.NextIndex ||
        Frame.Iterator == Frame.Node->cend())
        throw std::runtime_error("Invalid JSON map entry order.");

    Key = Frame.Iterator.key();
    const FJson& Entry = Frame.Iterator.value();

    // 자식 위치 추가 전에 부모의 순회 상태를 갱신한다.
    ++Frame.Iterator;
    ++Frame.NextIndex;
    Push(Entry);
}

// 기존 씬의 기본값 규칙으로 float 세 값을 복원한다.
void FJsonReader::Float3OrDefault(const char* Name, TArray<float>& Values, float Default)
{
    // 누락이나 배열 형태 오류는 전체 기본값을 유지한다.
    Values.SetNum(3);
    for (int32 Index = 0; Index < 3; ++Index)
        Values[Index] = Default;
    const FJson* Node = Find(Name);
    if (Node == nullptr || !Node->is_array() || Node->size() != 3) return;

    // 정상적인 성분만 덮어써서 잘못된 성분의 기본값을 보존한다.
    const double Limit = static_cast<double>((std::numeric_limits<float>::max)());
    for (size_t Index = 0; Index < 3; ++Index)
    {
        const FJson& Item = (*Node)[Index];
        if (!Item.is_number()) continue;
        const double Number = Item.get<double>();
        if (std::isfinite(Number) && Number >= -Limit && Number <= Limit)
            Values[Index] = static_cast<float>(Number);
    }
}