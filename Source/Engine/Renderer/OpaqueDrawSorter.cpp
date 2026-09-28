#include "pch.h"
#include "OpaqueDrawSorter.h"

void FOpaqueDrawSorter::SortOpaqueDraws(std::vector<FPreparedDraw>& InOutOpaqueDraws)
{
	const size_t Count = InOutOpaqueDraws.size();

	if (Count <= 1) return;
	std::vector<FPreparedDraw> Temp(Count);

	std::vector<FPreparedDraw>* Src = &InOutOpaqueDraws;
	std::vector<FPreparedDraw>* Dst = &Temp;

	constexpr uint32 RadixBits = 8;
	constexpr uint32 RadixSize = 1u << RadixBits;
	constexpr uint32 RadixMask = RadixSize - 1;

	uint32 Counts[RadixSize] = {};

	for (uint32 Shift = 0; Shift < 64; Shift += RadixBits) {
		std::memset(Counts, 0, sizeof(Counts));

		for (const FPreparedDraw& Draw : *Src) {
			const uint32 Digit =
				static_cast<uint32>((Draw.SortKey >> Shift) & RadixMask);

			++Counts[Digit];
		}

		uint32 Offset = 0;

		for (uint32 i = 0; i < RadixSize; ++i) {
			const uint32 CountForBucket = Counts[i];
			Counts[i] = Offset;
			Offset += CountForBucket;
		}

		for (const FPreparedDraw& Draw : *Src) {
			const uint32 Digit =
				static_cast<uint32>((Draw.SortKey >> Shift) & RadixMask);

			(*Dst)[Counts[Digit]++] = Draw;
		}

		std::swap(Src, Dst);
	}

	assert(Src == &InOutOpaqueDraws);
}

bool FOpaqueDrawSorter::IsSorted(const std::vector<FPreparedDraw>& Draws)
{
	for (size_t i = 1; i < Draws.size(); ++i) {
		if (Draws[i - 1].SortKey > Draws[i].SortKey) return false;
	}
	return true;
}
