#pragma once
#include <unordered_map>

#include "Engine/Renderer/PipelineState.h"

class FPipelineStateCache {
public:
	FPipelineStateCache() = default;
	~FPipelineStateCache() = default;

	// Non-copyable
	FPipelineStateCache(const FPipelineStateCache&) = default;
	FPipelineStateCache& operator=(const FPipelineStateCache&) = default;

	void Initialize(ID3D11Device* InDevice);
	void Shutdown();

	const FPipelineState* GetOrCreate(const FPipelineKey& Key);
	const FPipelineState* GetById(FPipelineId Id) const {
		if (Id < m_PipelineStates.size()) {
			return m_PipelineStates[Id].get();
		}
		return nullptr;
	}

	size_t GetCount() const { return m_PipelineStates.size(); }
private:
	std::unique_ptr<FPipelineState> CreatePipelineState(const FPipelineKey& Key, FPipelineId AssignedId);
private:
	ID3D11Device* m_Device = nullptr;
	std::unordered_map<FPipelineKey, FPipelineId> m_KeyToIdMap;
	std::vector<std::unique_ptr<FPipelineState>> m_PipelineStates;
};