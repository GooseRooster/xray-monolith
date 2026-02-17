#include "stdafx.h"
#pragma hdrstop

#if defined(USE_DX10) || defined(USE_DX11)
#include "../xrRenderDX10/dx10BufferUtils.h"
#endif	//	USE_DX11

#ifdef USE_DX11
#include "FBasicVisual.h"  // For FloraVertData struct
#include "../xrRenderDX10/StateManager/dx10ShaderResourceStateCache.h"
#endif

CBackend RCache;

// Create Quad-IB
#if defined(USE_DX10) || defined(USE_DX11)

void CBackend::RestoreQuadIBData()
{
}

void CBackend::CreateQuadIB()
{
	static const u32 dwTriCount = 4 * 1024;
	static const u32 dwIdxCount = dwTriCount * 2 * 3;
	u16 IndexBuffer[dwIdxCount];
	u16* Indices = IndexBuffer;

	D3D_BUFFER_DESC desc;
	desc.ByteWidth = dwIdxCount * 2;

	desc.Usage = D3D_USAGE_DEFAULT;
	desc.BindFlags = D3D_BIND_INDEX_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	D3D_SUBRESOURCE_DATA subData;
	subData.pSysMem = IndexBuffer;

	{
		int Cnt = 0;
		int ICnt = 0;
		for (int i = 0; i < dwTriCount; i++)
		{
			Indices[ICnt++] = u16(Cnt + 0);
			Indices[ICnt++] = u16(Cnt + 1);
			Indices[ICnt++] = u16(Cnt + 2);

			Indices[ICnt++] = u16(Cnt + 3);
			Indices[ICnt++] = u16(Cnt + 2);
			Indices[ICnt++] = u16(Cnt + 1);

			Cnt += 4;
		}
	}

	R_CHK(HW.pDevice->CreateBuffer ( &desc, &subData, &QuadIB));
	HW.stats_manager.increment_stats_ib(QuadIB);
}

#else	//	USE_DX11

void CBackend::RestoreQuadIBData()
{
	const u32 dwTriCount = 4 * 1024;
	u16* Indices = 0;
	R_CHK(QuadIB->Lock(0,0,(void**)&Indices,0));
	{
		int Cnt = 0;
		int ICnt = 0;
		for (int i = 0; i < dwTriCount; i++)
		{
			Indices[ICnt++] = u16(Cnt + 0);
			Indices[ICnt++] = u16(Cnt + 1);
			Indices[ICnt++] = u16(Cnt + 2);

			Indices[ICnt++] = u16(Cnt + 3);
			Indices[ICnt++] = u16(Cnt + 2);
			Indices[ICnt++] = u16(Cnt + 1);

			Cnt += 4;
		}
	}
	R_CHK(QuadIB->Unlock());
}

void CBackend::CreateQuadIB()
{
	const u32 dwTriCount = 4 * 1024;
	const u32 dwIdxCount = dwTriCount * 2 * 3;
	u16* Indices = 0;
	u32 dwUsage = D3DUSAGE_WRITEONLY;
	if (HW.Caps.geometry.bSoftware) dwUsage |= D3DUSAGE_SOFTWAREPROCESSING;
	R_CHK(HW.pDevice->CreateIndexBuffer (dwIdxCount*2,dwUsage,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&QuadIB,NULL));
	HW.stats_manager.increment_stats_ib(QuadIB);

	R_CHK(QuadIB->Lock(0,0,(void**)&Indices,0));
	{
		int Cnt = 0;
		int ICnt = 0;
		for (int i = 0; i < dwTriCount; i++)
		{
			Indices[ICnt++] = u16(Cnt + 0);
			Indices[ICnt++] = u16(Cnt + 1);
			Indices[ICnt++] = u16(Cnt + 2);

			Indices[ICnt++] = u16(Cnt + 3);
			Indices[ICnt++] = u16(Cnt + 2);
			Indices[ICnt++] = u16(Cnt + 1);

			Cnt += 4;
		}
	}
	R_CHK(QuadIB->Unlock());
}

#endif	//	USE_DX11

// Device dependance
void CBackend::OnDeviceCreate()
{
	CreateQuadIB();

	// streams
	Vertex.Create();
	Index.Create();

	InitDebugDraw();

#ifdef USE_DX11
	// Create flora/tree instancing vertex buffers
	// Pre-allocate buffers of various sizes for GPU instancing
	for (const auto& size : FloraVbufSizes)
	{
		D3D11_BUFFER_DESC buff_desc{};
		buff_desc.ByteWidth = size * sizeof(FloraVertData);
		buff_desc.Usage = D3D_USAGE_DYNAMIC;
		buff_desc.BindFlags = D3D_BIND_VERTEX_BUFFER;
		buff_desc.CPUAccessFlags = D3D_CPU_ACCESS_WRITE;
		buff_desc.MiscFlags = 0;
		buff_desc.StructureByteStride = sizeof(FloraVertData);

		ID3DVertexBuffer* buff{};
		R_CHK(HW.pDevice->CreateBuffer(&buff_desc, nullptr, &buff));
		FloraVbuffers.emplace(size, buff);
	}
#endif

	// invalidate caching
	Invalidate();
}

void CBackend::OnDeviceDestroy()
{
	// streams
	Index.Destroy();
	Vertex.Destroy();

	DestroyDebugDraw();

#ifdef USE_DX11
	// Release flora instancing buffers
	for (auto& it : FloraVbuffers)
		_RELEASE(it.second);
	FloraVbuffers.clear();
#endif

	// Quad
	HW.stats_manager.decrement_stats_ib(QuadIB);
	_RELEASE(QuadIB);
}

#ifdef USE_DX11
//-----------------------------------------------------------------------------
// GetFloraVbuff - Returns a pre-allocated vertex buffer for flora instancing
// The buffer can hold at least 'size' instances of FloraVertData.
// The size parameter is updated to the actual buffer capacity.
//-----------------------------------------------------------------------------
ID3DVertexBuffer* CBackend::GetFloraVbuff(u32& size)
{
	auto it = FloraVbuffers.lower_bound(size);
	if (it == FloraVbuffers.end())
	{
		// Requested size exceeds our largest buffer - use the max
		constexpr u32 max_buf = FloraVbufSizes[std::size(FloraVbufSizes) - 1];
		it = FloraVbuffers.find(max_buf);
	}

	size = it->first;
	return it->second;
}

void CBackend::InvalidateCSTextureCache()
{
	for (u32 i = 0; i < mtMaxComputeShaderTextures; ++i)
		textures_cs[i] = 0;
	SRVSManager.InvalidateCSViews();
}
#endif
