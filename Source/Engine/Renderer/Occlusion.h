#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "Core/Container/Map.h"
#include "Core/Container/Array.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Box.h"
#include "Engine/Renderer/PrimitiveRenderData.h"

struct FHZBCellData
{
	FVector4 BoundsMin;
	FVector4 BoundsMax;
};

struct FGridCellCandidate;

class FHZBOcclusionCuller
{
public:
	bool UploadCells(ID3D11Device* Device, ID3D11DeviceContext* Context, const TArray<FHZBCellData>& Cells);
	void Release();

	void QueueReadback(ID3D11DeviceContext* Context, const TArray<FGridCellCandidate>& Cells);
	bool TryReadback(ID3D11DeviceContext* Context);
	bool IsVisibleLastFrame(uint64 CellKey) const;
	void InvalidateCells(const TArray<uint64>& CellKeys);

	ID3D11ShaderResourceView* GetCellSRV() const { return CellSRV.Get(); }
	uint32 GetCellCount() const { return CellCount; }
	ID3D11UnorderedAccessView* GetVisibilityUAV() const { return VisibilityUAV.Get(); }
	ID3D11Buffer* GetVisibilityBuffer() const { return VisibilityBuffer.Get(); }
	bool IsReadbackPending() const { return bReadbackPending; }

	void InvalidateHistory();

private:
	uint32 CellCount = 0;
	uint32 CellCapacity = 0;

	Microsoft::WRL::ComPtr<ID3D11Buffer> CellBuffer;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CellSRV;
	Microsoft::WRL::ComPtr<ID3D11Buffer> VisibilityBuffer;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> VisibilityUAV;
	Microsoft::WRL::ComPtr<ID3D11Buffer> ReadbackBuffer;

	bool bReadbackPending = false;
	TArray<uint64> PendingCellKeys;
	TMap<uint64, bool> LastFrameVisibility;
	bool bDiscardPendingReadback = false;
};
