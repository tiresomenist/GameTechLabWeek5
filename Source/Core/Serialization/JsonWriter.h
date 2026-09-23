#pragma once

#include "Core/Serialization/Archive.h"
#include "nlohmann/json.hpp"
#include <vector>

class FJsonWriter final : public FArchive
{
public:
    // 빈 JSON 문서를 생성하고 루트에서 기록을 시작한다.
    FJsonWriter();
    
    // 복사 및 대입 금지
    FJsonWriter(const FJsonWriter&) = delete;
    FJsonWriter& operator=(const FJsonWriter&) = delete;

    // 쓰기 모드 명시적 선언
    bool IsLoading() const override { return false; }

    // 객체 생성 시작
    bool BeginObject(const char* Name) override;
    // 객체 종료
    void EndObject() override { Pop(); }

    // 배열 Resize후 진입
    bool BeginArray(const char* Name, uint32& Count) override;
    // 지정한 배열 원소 진입
    void BeginArrayElement(uint32 Index) override;
    // 배열 원소 종료
    void EndArrayElement() override { Pop(); }
    // 배열 종료
    void EndArray() override;

    // 맵 객체 생성, 기록 개수 설정
    bool BeginMap(const char* Name, uint32& Count) override;
    // 키에 대응되는 원소 생성
    void BeginMapEntry(uint32 Index, FString& Key) override;
    // 원소 종료
    void EndMapEntry() override { Pop(); }
    // 맵 종료
    void EndMap() override;

    // 벡터 -> JSON 배열
    void Float3OrDefault(const char* Name, TArray<float>& Values, float Default) override;

    // 완성된 JSON 문서를 문자열로 반환한다.
    FString ToString() const;

    // 완성된 JSON 문서를 기존 파일 저장 함수로 기록한다.
    void SaveToFile(FStringView Path) const;
    void SaveToFilePath(const std::filesystem::path& Path) const;

protected:
    bool SerializeValue(const char* Name, int32& Value) override;
    bool SerializeValue(const char* Name, uint32& Value) override;
    bool SerializeValue(const char* Name, float& Value) override;
    bool SerializeValue(const char* Name, double& Value) override;
    bool SerializeValue(const char* Name, bool& Value) override;
    bool SerializeValue(const char* Name, FString& Value) override;

private:
    using FJson = nlohmann::json;

    // 현재 객체 처리에 필요한 상태 묶음
    struct FStackFrame
    {
        FJson* Node;
        uint32 ExpectedCount = 0;
        uint32 NextIndex = 0;
    };

    FJson Root;
    std::vector<FStackFrame> Stack;

    // 현재 위치에서 기록할 필드를 찾거나 생성한다.
    FJson& Access(const char* Name);
    // 처리 위치를 스택에 추가한다.
    void Push(FJson& Node);
    // 현재 처리 위치를 제거한다.
    void Pop();

    // 기본 타입의 공통 기록과 실수 유효성 검사를 수행한다.
    template<typename T>
    bool WriteValue(const char* Name, const T& Value);
};