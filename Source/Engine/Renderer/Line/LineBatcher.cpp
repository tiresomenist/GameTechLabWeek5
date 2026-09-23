#include "pch.h"
#include "Engine/Renderer/Device.h"
#include "LineBatcher.h"
#include "Engine/Log.h"
#include "Engine/Renderer/Context.h"
#include <cmath>
#include <limits>

namespace
{
	bool ReserveLineBuffer(ID3D11Buffer*& Buffer,size_t& Capacity,size_t Need,UINT ElementSize,UINT BindFlags)
	{
		if (Buffer && Need <= Capacity)
		{
			return true;
		}

		const size_t MaxCount =	static_cast<size_t>((std::numeric_limits<UINT>::max)()) / ElementSize;
		
		if (Need == 0 || Need > MaxCount)
		{
			return false;
		}

		const size_t GrownCapacity = Capacity > MaxCount / 2 ? MaxCount : Capacity * 2;

		const size_t NewCapacity = (std::max)(Need, GrownCapacity);

		D3D11_BUFFER_DESC Desc{};
		Desc.ByteWidth = static_cast<UINT>(NewCapacity * ElementSize);
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = BindFlags;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		ID3D11Buffer* NewBuffer = nullptr;

		const HRESULT Result = GDevice::GetInstance()->GetDevice()->CreateBuffer(&Desc, nullptr, &NewBuffer);
		if (FAILED(Result))
		{
			return false;
		}
		if (Buffer)
		{
			Buffer->Release();
		}

		Buffer = NewBuffer;
		Capacity = NewCapacity;
		return true;
	}
}

bool FLineBatcher::AddRequest(const FLineDrawRequest& Request)
{
	const size_t AddedVertexCount = static_cast<size_t>(Request.Vertices.Num());

	const size_t AddedIndexCount = static_cast<size_t>(Request.Indices.Num());

	if (AddedVertexCount == 0 && AddedIndexCount == 0) return true;
	if (AddedVertexCount == 0 || AddedIndexCount == 0 || AddedIndexCount % 2 != 0) {
		UE_LOG("선을 그리는 인덱스의 개수가 짝수가 아닙니다.\n");
		return false;
	}
	const size_t MaxVertices = (std::min)(static_cast<size_t>((std::numeric_limits<int>::max)()),static_cast<size_t>((std::numeric_limits<UINT>::max)())/sizeof(FVertexSimple));
	const size_t MaxIndices = (std::min)(static_cast<size_t>((std::numeric_limits<int>::max)()),static_cast<size_t>((std::numeric_limits<UINT>::max)())/ sizeof(uint32));

	const size_t CurrentVertexCount = static_cast<size_t>(Vertices.Num());
	const size_t CurrentIndexCount = static_cast<size_t>(Indices.Num());

	if (CurrentVertexCount > MaxVertices || AddedVertexCount > MaxVertices - CurrentVertexCount ||
		CurrentIndexCount > MaxIndices || AddedIndexCount > MaxIndices - CurrentIndexCount)
	{
		UE_LOG("한번에 너무 많은 라인 드로우 요청\n");
		return false;
	}

	for (uint32 Index : Request.Indices){
		if (Index >= AddedVertexCount){
			UE_LOG("인덱스가 잘못된 버텍스를 가리키고 있습니다.\n");
			return false;
		}
	}
	for (const FVertexSimple& Vertex : Request.Vertices)
	{
		if (!std::isfinite(Vertex.x) ||	!std::isfinite(Vertex.y) ||	!std::isfinite(Vertex.z) ||
			!std::isfinite(Vertex.r) ||	!std::isfinite(Vertex.g) ||	!std::isfinite(Vertex.b) ||
			!std::isfinite(Vertex.a))
		{
			UE_LOG("잘못된 버텍스 정보\n");
			return false;
		}
	}
	try
	{
		// 두 배열의 공간을 먼저 확보하여 부분 추가를 방지함
		Vertices.Reserve(CurrentVertexCount + AddedVertexCount);
		Indices.Reserve(CurrentIndexCount + AddedIndexCount);
	}
		catch (const std::bad_alloc&)
	{
			UE_LOG("라인 배처 잘못된 메모리 할당\n");
		return false;
	}

	const uint32 VertexBase = static_cast<uint32>(CurrentVertexCount);

	for (const FVertexSimple& Vertex : Request.Vertices)
	{
		Vertices.Add(Vertex);
	}

	for (uint32 Index : Request.Indices)
	{
		// 요청 내부 번호를 배치 전체 번호로 변환함
		Indices.Add(VertexBase + Index);
	}
	return true;
}

bool FLineBatcher::Build()
{
	size_t N = Vertices.Num();

	if (N > VertexBufferCapacity)
	{
		if (!ReserveVertexBuffer(N))
		{
			return false;
		}
	}

	size_t M = Indices.Num();

	if (M > IndexBufferCapacity)
	{
		if (!ReserveIndexBuffer(M))
		{
			return false;
		}
	}

	if (!VertexBuffer || !IndexBuffer)
	{
		return false;
	}

	ID3D11DeviceContext* Context = GContext::GetInstance()->GetNative();
	if (!Context) return false;
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(Context->Map(VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}
	memcpy(Mapped.pData, Vertices.GetData(), sizeof(FVertexSimple) * N);
	Context->Unmap(VertexBuffer, 0);

	Mapped = {};
	if (FAILED(Context->Map(IndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}
	memcpy(Mapped.pData, Indices.GetData(), sizeof(uint32) * M);
	Context->Unmap(IndexBuffer, 0);

	return true;
}

void FLineBatcher::Clear()
{
	Vertices.Empty();
	Indices.Empty();
}

void FLineBatcher::Release()
{
	if (VertexBuffer)
	{
		VertexBuffer->Release();
		VertexBuffer = nullptr;
	}
	if (IndexBuffer)
	{
		IndexBuffer->Release();
		IndexBuffer = nullptr;
	}
	VertexBufferCapacity = 0;
	IndexBufferCapacity = 0;
}

ID3D11Buffer* FLineBatcher::GetVertexBuffer() const
{
	return VertexBuffer;
}

ID3D11Buffer* FLineBatcher::GetIndexBuffer() const
{
	return IndexBuffer;
}

UINT FLineBatcher::GetVertexCount() const
{
	return static_cast<UINT>(Vertices.Num());
}

UINT FLineBatcher::GetIndexCount() const
{
	return static_cast<UINT>(Indices.Num());
}

bool FLineBatcher::ReserveVertexBuffer(size_t Need)
{
	return ReserveLineBuffer(VertexBuffer, VertexBufferCapacity, Need, sizeof(FVertexSimple), D3D11_BIND_VERTEX_BUFFER);
}

bool FLineBatcher::ReserveIndexBuffer(size_t Need)
{
	return ReserveLineBuffer(IndexBuffer,IndexBufferCapacity,Need,sizeof(uint32),D3D11_BIND_INDEX_BUFFER);
}
