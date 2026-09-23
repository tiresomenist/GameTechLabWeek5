#pragma once

#include <filesystem>

#include "Engine/Resource/ObjInfo.h"
#include "Engine/Resource/StaticMeshData.h"

struct FObjImporter
{
    // OBJ와 참조된 MTL을 읽어 Raw 데이터를 반환한다.
    // 파일 열기 실패 또는 잘못된 입력은 예외로 전달한다.
    static FObjInfo Import(const std::filesystem::path& Path);

    // Raw 데이터를 엔진에서 사용할 메시 데이터로 변환.
    // 위치·법선 인덱스와 위치별 색상이 유효하고, 법선 생성은 완료된 입력을 받는다.
    // UV/머티리얼 인덱스의 -1은 각각 기본 UV(0, 0)/기본 머티리얼로 변환한다.
    static FStaticMeshData Cook(const FObjInfo& Info);

};
