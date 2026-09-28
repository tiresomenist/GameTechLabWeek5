#include "pch.h"
#include "Core/Core.h"
#include "Engine/Renderer/VertexSimple.h"
#include "Occlusion.h"

bool FOcclusionCuller::CreateProxyMesh(ID3D11Device* Device)
{
    if (!Device)
    {
        return false;
    }

    const FVertexSimple Vertices[] =
    {
        { -1, -1, -1, 1, 1, 1, 1 }, // 0
        { 1, -1, -1, 1, 1, 1, 1 }, // 1
        { 1,  1, -1, 1, 1, 1, 1 }, // 2
        { -1,  1, -1, 1, 1, 1, 1 }, // 3
        { -1, -1,  1, 1, 1, 1, 1 }, // 4
        { 1, -1,  1, 1, 1, 1, 1 }, // 5
        { 1,  1,  1, 1, 1, 1, 1 }, // 6
        { -1,  1,  1, 1, 1, 1, 1 }, // 7
    };

    const uint32 Indices[] =
    {
        0, 2, 1,  0, 3, 2, // -Z
        4, 5, 6,  4, 6, 7, // +Z
        0, 1, 5,  0, 5, 4, // -Y
        2, 3, 7,  2, 7, 6, // +Y
        0, 4, 7,  0, 7, 3, // -X
        1, 2, 6,  1, 6, 5, // +X
    };

    D3D11_BUFFER_DESC VertexDesc{};
    VertexDesc.ByteWidth = sizeof(Vertices);
    VertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    VertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA VertexData{};
    VertexData.pSysMem = Vertices;
    const HRESULT VertexResult = Device->CreateBuffer(&VertexDesc, &VertexData, ProxyVertexBuffer.GetAddressOf());
    if (FAILED(VertexResult))
    {
        return false;
    }

    D3D11_BUFFER_DESC IndexDesc{};
    IndexDesc.ByteWidth = sizeof(Indices);
    IndexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    IndexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA IndexData{};
    IndexData.pSysMem = Indices;
    const HRESULT IndexResult = Device->CreateBuffer(&IndexDesc, &IndexData, ProxyIndexBuffer.GetAddressOf());
    if (FAILED(IndexResult))
    {
        ProxyVertexBuffer.Reset();
        return false;
    }

    return true;
}


FOcclusionState& FOcclusionCuller::Create(ID3D11Device* Device, uint64 CellKey)
{
    FOcclusionState& State = States[CellKey];

    // 이미 이 Primitive의 query가 만들어졌다면 재사용
    if (State.Query)
    {
        return State;
    }

    D3D11_QUERY_DESC Desc{};
    Desc.Query = D3D11_QUERY_OCCLUSION;
    Desc.MiscFlags = 0;

    const HRESULT Result = Device->CreateQuery(&Desc, State.Query.GetAddressOf());

    if (FAILED(Result))
    {
        State.bVisibleLastFrame = true;
        State.bQueryPending = false;
    }

    return State;
}

void FOcclusionCuller::UpdateQueryResults(ID3D11DeviceContext* Context)
{
    for (auto& Pair : States)
    {
        FOcclusionState& State = Pair.second;

        if (!State.Query || !State.bQueryPending)
        {
            continue;
        }

        UINT64 PixelCount = 0;

        const HRESULT Result = Context->GetData(
            State.Query.Get(),
            &PixelCount,
            sizeof(PixelCount),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);

        if (Result == S_OK)
        {
            State.bVisibleLastFrame = (PixelCount > 0);
            State.bQueryPending = false;
        }
        else if (FAILED(Result))
        {
            State.bVisibleLastFrame = true;
            State.bQueryPending = false;
        }
    }
}

bool FOcclusionCuller::VisibleLastFrame(uint64 CellKey) const
{
    const FOcclusionState* State = States.Find(CellKey);

    if (State == nullptr || !State->Query)
    {
        return true;
    }

    return State->bVisibleLastFrame;
}

void FOcclusionCuller::Clear()
{
    States.Empty();
}
