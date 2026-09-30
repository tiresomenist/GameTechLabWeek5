#include "pch.h"
#include "Engine/Util/DebugCpuStats.h"
#include "OpaqueDrawSorter.h"

void FOpaqueDrawSorter::SortOpaqueDraws(
	TArray<FPreparedDraw>& Draws,
	TArray<FPreparedDraw>& DrawScratch,
	TArray<uint32>& IndexScratchA,
	TArray<uint32>& IndexScratchB)
{
	FScopedDebugCpuTime CpuTime(EDebugCpuStat::Sort);

	const uint32 Count = static_cast<uint32>(Draws.Num());
	if (Count <= 1)
		return;

	const uint64 FirstKey = Draws.First().SortKey;
	uint64 VaryingBits = 0;

	for (const FPreparedDraw& Draw : Draws)
	{
		VaryingBits |= Draw.SortKey ^ FirstKey;
	}

	if (VaryingBits == 0)
		return;

	IndexScratchA.resize(Count);
	IndexScratchB.resize(Count);

	for (uint32 Index = 0; Index < Count; ++Index)
	{
		IndexScratchA[Index] = Index;
	}

	TArray<uint32>* Src = &IndexScratchA;
	TArray<uint32>* Dst = &IndexScratchB;

	for (uint32 Shift = 0; Shift < 64; Shift += 8)
	{
		if (((VaryingBits >> Shift) & 0xffull) == 0)
			continue;

		uint32 Counts[256] = {};

		for (uint32 i = 0; i < Count; ++i)
		{
			const uint32 DrawIndex = (*Src)[i];
			const uint32 Digit =
				static_cast<uint32>((Draws[DrawIndex].SortKey >> Shift) & 0xffull);

			++Counts[Digit];
		}

		uint32 Offset = 0;

		for (uint32 i = 0; i < 256; ++i)
		{
			const uint32 BucketCount = Counts[i];
			Counts[i] = Offset;
			Offset += BucketCount;
		}

		for (uint32 i = 0; i < Count; ++i)
		{
			const uint32 DrawIndex = (*Src)[i];
			const uint32 Digit =
				static_cast<uint32>((Draws[DrawIndex].SortKey >> Shift) & 0xffull);

			(*Dst)[Counts[Digit]++] = DrawIndex;
		}

		TArray<uint32>* Temp = Src;
		Src = Dst;
		Dst = Temp;
	}

	// 정렬된 인덱스 순서대로 FPreparedDraw를 한 번만 재배치
	DrawScratch.resize(Count);

	for (uint32 i = 0; i < Count; ++i)
	{
		DrawScratch[i] = Draws[(*Src)[i]];
	}

	Draws.Swap(DrawScratch);
}

bool FOpaqueDrawSorter::IsSorted(const TArray<FPreparedDraw>& Draws)
{
	return true;
}