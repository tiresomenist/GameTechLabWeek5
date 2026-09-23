#pragma once

#include <filesystem>

#include "Core/Container/String.h"
#include "Core/Container/Array.h"
#include "Core/Math/Vector.h"
#include "Engine/Resource/StaticMeshData.h"

// Obj파일을 읽어온 결과값을 담는 구조체
// 
// 한 꼭짓점이 참조하는 위치·UV·법선.
// 파싱 후에는 0 기반 인덱스.
// UVIndex/NormalIndex의 -1은 해당 정보가 없다는 뜻.
struct FObjVertexIndex
{
    int32 PositionIndex = -1;
    int32 UVIndex = -1;
    int32 NormalIndex = -1;
};

// OBJ의 다각형 면을 삼각분할한 결과.
struct FObjTriangle
{
    //정점,uv,법선의 인덱스값
    FObjVertexIndex Corners[3]{};
    
    // Objects 배열의 인덱스.
    int32 ObjectIndex = -1;

    // -1이면 머티리얼 미지정.
    int32 MaterialIndex = -1;

    // 0이면 smoothing off.
    uint32 SmoothingGroup = 0;
};

// MTL에서 읽은 CPU 머티리얼 정보.
// 공통 CPU 머티리얼 데이터에 MTL 파싱 중 필요한 정의 여부를 추가한다.
struct FObjMaterialInfo : FStaticMeshMaterial
{
    //그외필드는 FStaticMeshMaterial 확인할것.
    // 이후 머티리얼 필드 추가로 저장할 때, FStaticMeshMaterial만 고치면 됨.
    // usemtl로 등록된 이름이 실제 newmtl 정의를 만났는지 구분한다.
    bool bDefined = false;
};

// OBJ의 o 선언으로 구분한 객체 정보.
struct FObjObjectInfo
{
    // 이름이 없는 객체는 빈 문자열.
    FString Name;
};

struct FObjInfo
{
    //파일 경로
    std::filesystem::path PathFileName;

    //.obj, .mtl의 경로 모음
    TArray<std::filesystem::path> SourceFiles;

    // 객체들을 담는 배열
    TArray<FObjObjectInfo> Objects;

    //정점 위치
    TArray<FVector> Positions;

    //색상
    TArray<FVector4> VertexColors;

    // vt u [v [w]]를 보존한다.
    // Cook할 때 2D UV에는 X/Y를 사용.
    TArray<FVector> TexCoords;

    //법선
    TArray<FVector> Normals;
    //삼각형 폴리곤 정보
    TArray<FObjTriangle> Triangles;
    //머티리얼
    TArray<FObjMaterialInfo> Materials;
};