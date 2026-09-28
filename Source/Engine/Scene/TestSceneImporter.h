#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <memory>

class FJsonReader;

//AI GENERATED FILE

// 외부 테스트 씬을 현재 엔진의 JSON 형식으로 변환한다. 씬 객체는 생성하지 않는다.
class FTestSceneImporter
{
public:
    using FMeshPathMap = std::unordered_map<std::string, std::filesystem::path>;

    // 원본을 변경하지 않고 변환 문서를 소유한 Reader를 반환한다. 엔진 형식은 그대로 사용한다.
    // .scene/.json 확장자와 무관하게 내용을 판별하며 원본 Version 필드는 요구하지 않는다.
    // 상대 매핑 경로는 작업 디렉터리, 매핑 없는 경로는 원본 씬 폴더 기준이다.
    // 위치/스케일은 유지하고, 회전은 XYZ 라디안, FOV는 도 단위로 해석한다.
    // 메시도 카메라와 같은 축·부호 규칙으로 가정하여 Roll=X, Pitch=-Y, Yaw=Z로 변환한다.
    static std::unique_ptr<FJsonReader> Load(const std::filesystem::path& ScenePath, const FMeshPathMap& MeshPaths = {});
};
