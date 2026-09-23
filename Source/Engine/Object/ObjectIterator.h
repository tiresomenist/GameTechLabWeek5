#pragma once

#include <concepts>

#include "Object.h"
#include "ObjectStatics.h"
#include "Core/Container/Array.h"

class UObject;

// 엔진 전체에 있는 T 타입의 오브젝트들을 순회하는 반복자
// 순서 보장 없음
// 생성 시점의 스냅샷을 순회함
template <typename T> requires std::derived_from<T, UObject>
class TObjectIterator final
{
public:
	enum EEndTagType { EndTag }; // TObjectRange::end()를 만들기 위한 태그

	explicit TObjectIterator(bool bIncludeDerivedClasses = true);
	TObjectIterator(EEndTagType, const TObjectIterator& Other) : Index(Other.ObjectArray.Num()) {} // TObjectRange::end()

	void operator++() { Index++; }
	explicit operator bool() const { return Index >= 0 && Index < ObjectArray.Num(); }
	bool operator!() const { return !static_cast<bool>(*this); }
	T* operator*() const { return static_cast<T*>(ObjectArray[Index]); }
	T* operator->() const { return static_cast<T*>(ObjectArray[Index]); }
	bool operator==(const TObjectIterator& Other) const { return Index == Other.Index; }
	bool operator!=(const TObjectIterator& Other) const { return !(*this == Other); }

protected:
	TArray<UObject*> ObjectArray;
	int32 Index;
};

template <typename T> requires std::derived_from<T, UObject>
TObjectIterator<T>::TObjectIterator(bool bIncludeDerivedClasses) : Index(0)
{
	ObjectArray = GObjectStatics::GetObjectsOfClass(T::GetClass(), bIncludeDerivedClasses);
}

// 엔진 전체에 있는 T 타입의 오브젝트들을 range-for로 순회할 수 있는 객체
// 순서 보장 없음
// 생성 시점의 스냅샷을 순회함
template <typename T> requires std::derived_from<T, UObject>
struct TObjectRange
{
	TObjectRange(bool bIncludeDerivedClasses = true) : Iterator(bIncludeDerivedClasses) {}

	friend TObjectIterator<T> begin(TObjectRange& Range) { return Range.Iterator; }
	friend TObjectIterator<T> end(TObjectRange& Range) { return TObjectIterator<T>(TObjectIterator<T>::EndTag, Range.Iterator); }

	TObjectIterator<T> Iterator;
};
