#include "pch.h"
#include "Engine/Util/DebugCpuStats.h"
#include "OpaqueDrawSorter.h"

void FOpaqueDrawSorter::SortOpaqueDraws(TArray<FPreparedDraw>& Draws, TArray<FPreparedDraw>& Scratch)
{
	FScopedDebugCpuTime CpuTime(EDebugCpuStat::Sort);
	const size_t Count = Draws.Num();
	if (Count <= 1) return;

	const uint64 FirstKey = Draws.First().SortKey;
	uint64 VaryingBits = 0;

	for (const FPreparedDraw& Draw : Draws)
		VaryingBits |= Draw.SortKey ^ FirstKey;

	if (VaryingBits == 0) return;

	Scratch.resize(Count);

	auto* Src = &Draws;
	auto* Dst = &Scratch;

	for (uint32 Shift = 0; Shift < 64; Shift += 8) {
		if (((VaryingBits >> Shift) & 0xffull) == 0) continue;
		size_t Counts[256]{};
		for (const FPreparedDraw& Draw : *Src) {
			const uint32 Digit = static_cast<uint32>((Draw.SortKey >> Shift) & 0xffull);
			++Counts[Digit];
		}
		
		size_t Offset = 0;
		for (size_t& Bucket : Counts) {
			const size_t BucketCount = Bucket;
			Bucket = Offset;
			Offset += BucketCount;
		}

		for (const FPreparedDraw& Draw : *Src) {
			const uint32 Digit = static_cast<uint32>((Draw.SortKey >> Shift) & 0xffull);
			(*Dst)[Counts[Digit]++] = Draw;
		}

		std::swap(Src, Dst);
	}
	if (Src != &Draws) Draws.Swap(Scratch);
}

bool FOpaqueDrawSorter::IsSorted(const TArray<FPreparedDraw>& Draws)
{
	return true;
}
