#pragma once


struct FMeshSection
{
    // Indices 배열의 시작 위치. 바이트 단위가 아니다.
    uint32 FirstIndex = 0;

    // 삼각형 수가 아닌 인덱스 수.
    uint32 IndexCount = 0;

    // Materials 배열의 인덱스.
    uint32 MaterialIndex = 0;

    //객체 인덱스
    int32 ObjectIndex = -1;

};