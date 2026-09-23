#include "pch.h"
#include "Core/Serialization/JsonWriter.h"
#include "Core/Util/File.h"
#include <cmath>

// 빈 JSON 객체를 만들고 루트를 현재 위치로 설정한다.
FJsonWriter::FJsonWriter() : Root(FJson::object())
{
    // 최초 위치는 문서 전체를 나타내는 루트다.
    Push(Root);
}

// 현재 위치에서 이름에 해당하는 기록 대상을 반환한다.
FJsonWriter::FJson& FJsonWriter::Access(const char* Name)
{
    FJson& Current = *Stack.back().Node;

    // 이름이 없으면 현재 배열 원소나 루트 자체를 사용한다.
    if (Name == nullptr) return Current;

    // 비어 있는 배열 원소에 이름 있는 필드를 쓰면 객체로 전환한다.
    if (Current.is_null()) Current = FJson::object();
    if (!Current.is_object())
        throw std::runtime_error("Named JSON field requires an object.");
    return Current[Name];
}

// 새로운 처리 위치를 스택에 추가한다.
void FJsonWriter::Push(FJson& Node)
{
    // 부모 위치는 스택에 남겨 종료 시 복귀할 수 있게 한다.
    Stack.push_back({ &Node, 0, 0 });
}

// 현재 위치를 제거하고 부모 위치로 돌아간다.
void FJsonWriter::Pop()
{
    // 문서의 루트 위치는 제거하지 않는다.
    if (Stack.size() <= 1)
        throw std::runtime_error("Cannot leave JSON root.");
    Stack.pop_back();
}

// 기본 타입을 기록하고 정상 처리 여부를 반환한다.
template<typename T>
bool FJsonWriter::WriteValue(const char* Name, const T& Value)
{
    // NaN, 무한대값 처리
    if constexpr (std::is_floating_point_v<T>)
    {
        if (!std::isfinite(Value))
            throw std::runtime_error("Cannot write a non-finite JSON number.");
    }
    Access(Name) = Value;
    return true;
}

bool FJsonWriter::SerializeValue(const char* Name, int32& Value) { return WriteValue(Name, Value); }
bool FJsonWriter::SerializeValue(const char* Name, uint32& Value) { return WriteValue(Name, Value); }
bool FJsonWriter::SerializeValue(const char* Name, float& Value) { return WriteValue(Name, Value); }
bool FJsonWriter::SerializeValue(const char* Name, double& Value) { return WriteValue(Name, Value); }
bool FJsonWriter::SerializeValue(const char* Name, bool& Value) { return WriteValue(Name, Value); }
bool FJsonWriter::SerializeValue(const char* Name, FString& Value) { return WriteValue(Name, Value); }

// 이름에 해당하는 객체를 만들고 진입한다.
bool FJsonWriter::BeginObject(const char* Name)
{
    // 새 객체로 초기화한 뒤 해당 객체를 현재 위치로 설정한다.
    FJson& Node = Access(Name);
    Node = FJson::object();
    Push(Node);
    return true;
}

// 지정된 개수의 배열을 만들고 진입한다.
bool FJsonWriter::BeginArray(const char* Name, uint32& Count)
{
    // 현재 컨테이너가 표현할 수 있는 원소 개수인지 확인한다.
    if (Count > static_cast<uint32>((std::numeric_limits<int32>::max)()))
        throw std::length_error("JSON array exceeds TArray capacity.");

    // 원소 공간을 먼저 확보하여 순회 중 배열 재할당을 방지한다.
    FJson& Node = Access(Name);
    Node = FJson::array();
    Node.get_ref<FJson::array_t&>().resize(Count);
    Push(Node);
    Stack.back().ExpectedCount = Count;
    return true;
}

// 배열의 다음 원소에 진입한다.
void FJsonWriter::BeginArrayElement(uint32 Index)
{
    FStackFrame& Frame = Stack.back();

    // 원소를 순서대로 한 번씩 처리하도록 검사한다.
    if (!Frame.Node->is_array() || Index != Frame.NextIndex || Index >= Frame.ExpectedCount)
        throw std::runtime_error("Invalid JSON array element order.");
    FJson& Element = Frame.Node->at(Index);
    ++Frame.NextIndex;
    Push(Element);
}

// 모든 배열 원소를 처리했는지 확인하고 종료한다.
void FJsonWriter::EndArray()
{
    const FStackFrame& Frame = Stack.back();

    // 선언한 개수보다 적게 기록된 배열은 완성된 것으로 취급하지 않는다.
    if (!Frame.Node->is_array() || Frame.NextIndex != Frame.ExpectedCount)
        throw std::runtime_error("JSON array is incomplete.");
    Pop();
}

// 문자열 키 컬렉션을 만들고 예상 개수를 설정한다.
bool FJsonWriter::BeginMap(const char* Name, uint32& Count)
{
    // JSON에서는 키 컬렉션을 객체로 표현한다.
    BeginObject(Name);
    Stack.back().ExpectedCount = Count;
    return true;
}

// 중복되지 않는 키의 원소를 만들고 진입한다.
void FJsonWriter::BeginMapEntry(uint32 Index, FString& Key)
{
    FStackFrame& Frame = Stack.back();

    // 순서와 개수를 확인하고 기존 키를 덮어쓰지 않도록 한다.
    if (!Frame.Node->is_object() || Index != Frame.NextIndex || Index >= Frame.ExpectedCount)
        throw std::runtime_error("Invalid JSON map entry order.");
    if (Frame.Node->contains(Key))
        throw std::runtime_error("Duplicate JSON map key: " + Key);

    FJson& Entry = (*Frame.Node)[Key];
    Entry = FJson::object();
    ++Frame.NextIndex;
    Push(Entry);
}

// 모든 맵 원소를 기록했는지 확인하고 종료한다.
void FJsonWriter::EndMap()
{
    const FStackFrame& Frame = Stack.back();

    // 선언한 개수와 실제 키 개수가 일치해야 저장을 완료한다.
    if (!Frame.Node->is_object() || Frame.NextIndex != Frame.ExpectedCount ||
        Frame.Node->size() != Frame.ExpectedCount)
        throw std::runtime_error("JSON map is incomplete.");
    Pop();
}

// float 세 값을 일반 배열 직렬화로 기록한다.
void FJsonWriter::Float3OrDefault(const char* Name, TArray<float>& Values, float /*Default*/)
{
    if (Values.Num() != 3) throw std::runtime_error("Expected exactly three float values.");

    Field(Name, Values);
}

// 완성된 문서를 JSON 문자열로 변환한다.
FString FJsonWriter::ToString() const
{
    // 아직 닫히지 않은 객체나 배열이 있으면 저장을 거부한다.
    if (Stack.size() != 1)
        throw std::runtime_error("JSON scopes are not closed.");
    return Root.dump(2);
}

// 완성된 JSON 문자열을 파일에 저장한다.
void FJsonWriter::SaveToFile(FStringView Path) const
{
    // 기존 임시 파일 작성 후 교체 저장 방식을 재사용한다.
    File::WriteText(Path, ToString());
}

void FJsonWriter::SaveToFilePath(const std::filesystem::path& Path) const
{
    File::WriteTextToPath(Path, ToString());
}
