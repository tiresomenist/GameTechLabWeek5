#include "pch.h"
#include "Core/Serialization/Archive.h"

// 읽기 모드의 반대 값을 사용해 쓰기 모드를 반환한다.
bool FArchive::IsSaving() const
{
    // Reader와 Writer가 각각 읽기 여부만 정의하도록 공통 처리한다.
    return !IsLoading();
}

// Archive가 추상 기반 클래스이며 안전한 가상 소멸자를 제공하는지 확인한다.
static_assert(std::is_abstract_v<FArchive>);
static_assert(std::has_virtual_destructor_v<FArchive>);