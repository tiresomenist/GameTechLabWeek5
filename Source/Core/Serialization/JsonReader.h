#pragma once

#include "Core/Serialization/Archive.h"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <vector>
#include <memory>
class FJsonReader final : public FArchive
{
public:
    // JSON 문자열을 파싱하고 루트에서 읽기를 시작한다.
    explicit FJsonReader(FStringView Text);

    // 내부 위치가 자신의 문서를 가리키므로 복사를 금지한다.
    FJsonReader(const FJsonReader&) = delete;
    // 다른 문서의 위치 정보가 복사되지 않도록 대입을 금지한다.
    FJsonReader& operator=(const FJsonReader&) = delete;

    // Reader는 항상 읽기 모드로 동작한다.
    bool IsLoading() const override { return true; }

    // 파일을 읽어 JSON Reader를 생성한다.
    static std::unique_ptr<FJsonReader> FromFile(const std::filesystem::path& Path);

    // 같은 문서를 처음부터 다시 읽도록 위치를 초기화한다.
    void ResetToRoot();

    // 이름에 해당하는 객체를 찾아 진입한다.
    bool BeginObject(const char* Name) override;
    // 현재 객체에서 부모 위치로 돌아간다.
    void EndObject() override { Pop(); }

    // 배열을 찾아 진입하고 저장된 원소 개수를 반환한다.
    bool BeginArray(const char* Name, uint32& Count) override;
    // 지정한 배열 원소에 진입한다.
    void BeginArrayElement(uint32 Index) override;
    // 현재 배열 원소에서 배열 위치로 돌아간다.
    void EndArrayElement() override { Pop(); }
    // 현재 배열에서 부모 위치로 돌아간다.
    void EndArray() override { Pop(); }

    // 문자열 키 컬렉션에 진입하고 원소 개수를 반환한다.
    bool BeginMap(const char* Name, uint32& Count) override;
    // 다음 맵 원소에 진입하고 실제 키를 반환한다.
    void BeginMapEntry(uint32 Index, FString& Key) override;
    // 현재 맵 원소에서 맵 위치로 돌아간다.
    void EndMapEntry() override { Pop(); }
    // 현재 맵에서 부모 위치로 돌아간다.
    void EndMap() override { Pop(); }

    // float 세 값을 읽으며 잘못된 값에는 기존 기본값 규칙을 적용한다.
    void Float3OrDefault(const char* Name, TArray<float>& Values, float Default) override;

protected:
    // int32 값을 읽고 범위를 검사한다.
    bool SerializeValue(const char* Name, int32& Value) override;
    // uint32 값을 읽고 범위를 검사한다.
    bool SerializeValue(const char* Name, uint32& Value) override;
    // float 값을 읽고 유효성을 검사한다.
    bool SerializeValue(const char* Name, float& Value) override;
    // double 값을 읽고 유효성을 검사한다.
    bool SerializeValue(const char* Name, double& Value) override;
    // bool 값을 읽고 타입을 검사한다.
    bool SerializeValue(const char* Name, bool& Value) override;
    // 문자열을 읽고 타입을 검사한다.
    bool SerializeValue(const char* Name, FString& Value) override;

private:
    using FJson = nlohmann::json;

    struct FStackFrame
    {
        const FJson* Node;
        FJson::const_iterator Iterator;
        uint32 NextIndex = 0;
    };

    FJson Root;
    std::vector<FStackFrame> Stack;

    // 현재 위치에서 이름에 해당하는 값을 찾는다.
    const FJson* Find(const char* Name) const;
    // 처리 위치와 순회 상태를 스택에 추가한다.
    void Push(const FJson& Node);
    // 현재 처리 위치를 제거한다.
    void Pop();
    // 객체나 배열의 존재와 타입을 확인하고 진입한다.
    bool BeginContainer(const char* Name, bool bArray, uint32* Count);

    // 숫자를 읽고 목적 타입의 범위 안에서 변환한다.
    template<typename T>
    bool ReadNumber(const char* Name, T& Value);
};