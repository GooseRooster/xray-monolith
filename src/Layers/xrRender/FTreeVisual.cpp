#include "stdafx.h"
#pragma hdrstop

#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/IGame_Level.h"
#include "../../xrEngine/Environment.h"
#include "../../xrEngine/Fmesh.h"

#include "../../build_config_defines.h"

#include "FTreeVisual.h"

//-----------------------------------------------------------------------------
// Vertex declaration for instanced tree rendering (DX11)
// Stream 0: Base tree geometry (32 bytes per vertex)
// Stream 1: Per-instance data (FloraVertData, 80 bytes per instance)
//-----------------------------------------------------------------------------
#ifdef USE_DX11
static D3DVERTEXELEMENT9 FTreeVisualDecl[] = {
	// Stream 0: Base geometry
	{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
	{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
	{0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0},
	{0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL, 0},
	{0, 24, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
	// Stream 1: Per-instance data (4x float4 = 64 bytes used by shader)
	{1, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
	{1, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1},
	{1, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 2},
	{1, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 3},
	D3DDECL_END()
};
constexpr u32 FTreeVisualVertBufferSize = 32;
#endif

//-----------------------------------------------------------------------------
// Hash helper for CRC calculation (used for batching identical geometries)
//-----------------------------------------------------------------------------
static inline void q_hash(u32& seed, const u32 v)
{
	seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// Shader constant names (shared_str for fast comparison)
shared_str m_xform;
shared_str m_xform_v;
shared_str c_consts;
shared_str c_wave;
shared_str c_wind;
shared_str c_c_bias;
shared_str c_c_scale;
shared_str c_c_sun;

shared_str c_prev_wave;
shared_str c_prev_wind;

shared_str c_c_PrevBendersPos;
shared_str c_c_BendersPos;
shared_str c_c_BendersSetup;

FTreeVisual::FTreeVisual(void)
{
}

FTreeVisual::~FTreeVisual(void)
{
}

void FTreeVisual::Release()
{
	dxRender_Visual::Release();
}

void FTreeVisual::Load(const char* N, IReader* data, u32 dwFlags)
{
	dxRender_Visual::Load(N, data, dwFlags);

	D3DVERTEXELEMENT9* vFormat = NULL;

	// read vertices
	R_ASSERT(data->find_chunk(OGF_GCONTAINER));
	{
		// verts
		u32 ID = data->r_u32();
		vBase = data->r_u32();
		vCount = data->r_u32();

		// Build CRC hash from geometry identifiers for batching
		q_hash(crc, ID);
		q_hash(crc, vBase);
		q_hash(crc, vCount);

		vFormat = RImplementation.getVB_Format(ID);

		VERIFY(NULL == p_rm_Vertices);

		p_rm_Vertices = RImplementation.getVB(ID);
		p_rm_Vertices->AddRef();

		// indices
		dwPrimitives = 0;
		ID = data->r_u32();
		iBase = data->r_u32();
		iCount = data->r_u32();

		q_hash(crc, ID);
		q_hash(crc, iBase);
		q_hash(crc, iCount);

		dwPrimitives = iCount / 3;

		VERIFY(NULL == p_rm_Indices);
		p_rm_Indices = RImplementation.getIB(ID);
		p_rm_Indices->AddRef();
	}

	// load tree-def and compute tree_data for instancing
	R_ASSERT(data->find_chunk(OGF_TREEDEF2));

#ifdef USE_DX11
	// For DX11 instanced rendering: create geometry with instance stream declaration
	rm_geom.create(FTreeVisualDecl, p_rm_Vertices, p_rm_Indices);

	// Mark instance data elements (stream 1) for per-instance stepping
	for (size_t it = 5; it <= 8; ++it)
	{
		auto& dcl = rm_geom->dcl->dx10_dcl_code.at(it);
		dcl.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
		dcl.InstanceDataStepRate = 1;
	}

	{
		// Pre-compute tree_data for GPU instancing
		constexpr float ps_r__Tree_SBC = 1.5f; // scale bias correct
		constexpr float s = ps_r__Tree_SBC * 1.3333f;

		struct _5color
		{
			Fvector rgb;
			float hemi;
			float sun;
		} c_scale{}, c_bias{};

		Fmatrix xform{};

		data->r(&xform, sizeof(xform));
		data->r(&c_scale, sizeof(c_scale));
		c_scale.hemi *= .5f;
		c_scale.hemi *= s;

		data->r(&c_bias, sizeof(c_bias));
		c_bias.hemi *= .5f;
		c_bias.hemi *= s;

		constexpr float FTreeVisual_tile_f = 16.f;
		constexpr float FTreeVisual_quant_f = 32768.f / FTreeVisual_tile_f;
		constexpr float FTreeVisual_scale = 1.f / FTreeVisual_quant_f;

		// Store transform matrix (transposed for shader) and lighting data
		tree_data.data[0] = xform._11;
		tree_data.data[1] = xform._21;
		tree_data.data[2] = xform._31;
		tree_data.data[3] = xform._41;
		tree_data.data[4] = xform._12;
		tree_data.data[5] = xform._22;
		tree_data.data[6] = xform._32;
		tree_data.data[7] = xform._42;
		tree_data.data[8] = xform._13;
		tree_data.data[9] = xform._23;
		tree_data.data[10] = xform._33;
		tree_data.data[11] = xform._43;
		tree_data.data[12] = FTreeVisual_scale;
		tree_data.data[13] = FTreeVisual_scale;
		tree_data.data[14] = c_scale.hemi;
		tree_data.data[15] = c_bias.hemi;
	}
#else
	// Non-DX11: use original vertex format
	{
		struct _5color
		{
			Fvector rgb;
			float hemi;
			float sun;
		} c_scale{}, c_bias{};
		Fmatrix xform{};

		data->r(&xform, sizeof(xform));
		data->r(&c_scale, sizeof(c_scale));
		c_scale.rgb.mul(.5f);
		c_scale.hemi *= .5f;
		c_scale.sun *= .5f;
		data->r(&c_bias, sizeof(c_bias));
		c_bias.rgb.mul(.5f);
		c_bias.hemi *= .5f;
		c_bias.sun *= .5f;
	}

	// Geom
	rm_geom.create(vFormat, p_rm_Vertices, p_rm_Indices);
#endif

	// Get constants
	m_xform = "m_xform";
	m_xform_v = "m_xform_v";
	c_consts = "consts";
	c_wave = "wave";
	c_wind = "wind";
	c_c_bias = "c_bias";
	c_c_scale = "c_scale";
	c_c_sun = "c_sun";

	c_prev_wave = "prev_wave";
	c_prev_wind = "prev_wind";

	c_c_PrevBendersPos = "benders_prevpos";
	c_c_BendersPos = "benders_pos";
	c_c_BendersSetup = "benders_setup";
}

//-----------------------------------------------------------------------------
// FTreeVisual_setup - Wind animation parameters
//-----------------------------------------------------------------------------
struct FTreeVisual_setup
{
	u32 dwFrame;
	float scale;
	Fvector4 wave;
	Fvector4 wind;

	FTreeVisual_setup()
	{
		dwFrame = 0;
	}

	void calculate()
	{
		dwFrame = Device.dwFrame;

		// Calc wind-vector3, scale
		float tm_rot = PI_MUL_2 * Device.fTimeGlobal / ps_r__Tree_w_rot;

#ifdef TREE_WIND_EFFECT
		CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
		float fValue = E.m_fTreeAmplitudeIntensity;
		wind.set(_sin(tm_rot), 0, _cos(tm_rot), 0);
		wind.normalize();
#if RENDER != R_R1
		wind.mul(fValue); // dir1*amplitude
#else // R1
		wind.mul(ps_r__Tree_w_amp); // dir1*amplitude
#endif //-RENDER!=R_R1
#else //!TREE_WIND_EFFECT
		wind.set(_sin(tm_rot), 0, _cos(tm_rot), 0);
		wind.normalize();
		wind.mul(ps_r__Tree_w_amp); // dir1*amplitude
#endif //-TREE_WIND_EFFECT
		scale = 1.f / float(FTreeVisual_quant);

		// setup constants
		wave.set(ps_r__Tree_Wave.x, ps_r__Tree_Wave.y, ps_r__Tree_Wave.z,
		         Device.fTimeGlobal * ps_r__Tree_w_speed); // wave
		wave.div(PI_MUL_2);
	}
};

//-----------------------------------------------------------------------------
// FTreeVisual::Render - Non-instanced rendering path (for compatibility)
//-----------------------------------------------------------------------------
void FTreeVisual::Render(float LOD)
{
	static FTreeVisual_setup tvs, prev_tvs;
	if (tvs.dwFrame != Device.dwFrame)
	{
		prev_tvs = tvs; // Save previous frame calculations
		tvs.calculate();
	}

	RCache.tree.set_consts(tvs.scale, tvs.scale, 0, 0); // consts/scale
	RCache.tree.set_wave(tvs.wave); // wave
	RCache.tree.set_wind(tvs.wind); // wind

	RCache.set_c(c_prev_wave, prev_tvs.wave);
	RCache.set_c(c_prev_wind, prev_tvs.wind);

#if RENDER == R_R4 || RENDER == R_R3
	if (ps_ssfx_grass_interactive.y > 0)
	{
		// Inter grass Settings
		RCache.set_c(c_c_BendersSetup, ps_ssfx_int_grass_params_1);

		// Grass benders data ( Player + Characters )
		IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
		Fvector4 player_pos = {0, 0, 0, 0};
		int BendersQty = _min(16, ps_ssfx_grass_interactive.y + 1);

		// Add Player?
		if (ps_ssfx_grass_interactive.x > 0)
		{
			player_pos.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);
		}

		Fvector4* c_grass;
		{
			void* GrassData;
			RCache.get_ConstantDirect(c_c_BendersPos, BendersQty * sizeof(Fvector4) * 2, &GrassData, 0, 0);

			c_grass = (Fvector4*)GrassData;
		}
		VERIFY(c_grass);

		if (c_grass)
		{
			c_grass[0].set(player_pos);
			c_grass[16].set(0.0f, -99.0f, 0.0f, 1.0f);

			for (int Bend = 1; Bend < BendersQty; Bend++)
			{
				c_grass[Bend].set(GData.pos[Bend].x, GData.pos[Bend].y, GData.pos[Bend].z, GData.radius_curr[Bend]);
				c_grass[Bend + 16].set(GData.dir[Bend].x, GData.dir[Bend].y, GData.dir[Bend].z, GData.str[Bend]);
			}
		}

		Fvector4* c_prevgrass;
		{
			void* prevGrassData;
			RCache.get_ConstantDirect(c_c_PrevBendersPos, BendersQty * sizeof(Fvector4) * 2, &prevGrassData, 0, 0);

			c_prevgrass = (Fvector4*)prevGrassData;
		}
		VERIFY(c_prevgrass);

		if (c_prevgrass)
		{
			for (int Bend = 0; Bend < BendersQty; Bend++)
			{
				c_prevgrass[Bend].set(GData.prev_pos[Bend]);
				c_prevgrass[Bend + 16].set(GData.prev_dir[Bend]);
			}
		}
	}
#endif
}

#ifdef USE_DX11
//-----------------------------------------------------------------------------
// DoRenderInstanced - Core instanced rendering implementation
// Maps instance data to vertex buffer and issues DrawIndexedInstanced call.
//-----------------------------------------------------------------------------
void FTreeVisual::DoRenderInstanced(const xr_vector<FloraVertData*>& data, u32 countV, u32 startI, u32 PC)
{
	u32 sizeof_vbuffer = static_cast<u32>(data.size());
	ID3DVertexBuffer* current_vbuffer = RCache.GetFloraVbuff(sizeof_vbuffer);

	constexpr u32 vb_stride[] = {FTreeVisualVertBufferSize, sizeof(FloraVertData)};
	constexpr u32 i_offset[] = {0, 0};
	ID3DVertexBuffer* final_vbuffer[] = {rm_geom->vb, current_vbuffer};

	RCache.set_Format(&*rm_geom->dcl);
	RCache.set_Vertices_Forced(std::size(final_vbuffer), final_vbuffer, vb_stride, i_offset);
	RCache.set_Indices(rm_geom->ib);

	// Map the instance buffer and copy data
	D3D11_MAPPED_SUBRESOURCE pSubRes{};
	R_CHK(HW.pContext->Map(current_vbuffer, 0, D3D_MAP_WRITE_DISCARD, 0, &pSubRes));

	u32 flora_count = 0;
	auto c_storage = reinterpret_cast<FloraVertData*>(pSubRes.pData);

	for (const auto* draw_data : data)
	{
		std::memcpy(&c_storage[flora_count++], draw_data, sizeof(*draw_data));
	}

	HW.pContext->Unmap(current_vbuffer, 0);

	// Issue instanced draw call
	RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, countV, startI, PC, flora_count);
	RCache.stat.r.s_flora.add(flora_count);
}
#else
void FTreeVisual::DoRenderInstanced(const xr_vector<FloraVertData*>& data, u32 countV, u32 startI, u32 PC)
{
	// Non-DX11: not implemented
}
#endif

#define PCOPY(a) a = pFrom->a

void FTreeVisual::Copy(dxRender_Visual* pSrc)
{
	dxRender_Visual::Copy(pSrc);

	FTreeVisual* pFrom = fast_dynamic_cast<FTreeVisual*>(pSrc);

	PCOPY(rm_geom);

	PCOPY(p_rm_Vertices);
	if (p_rm_Vertices) p_rm_Vertices->AddRef();

	PCOPY(vBase);
	PCOPY(vCount);

	PCOPY(p_rm_Indices);
	if (p_rm_Indices) p_rm_Indices->AddRef();

	PCOPY(iBase);
	PCOPY(iCount);
	PCOPY(dwPrimitives);

	// Copy tree instancing data
	PCOPY(tree_data);
	PCOPY(crc);
}

//-----------------------------------------------------------------------------------
// Stripified Tree
//-----------------------------------------------------------------------------------
FTreeVisual_ST::FTreeVisual_ST(void)
{
}

FTreeVisual_ST::~FTreeVisual_ST(void)
{
}

void FTreeVisual_ST::Release()
{
	inherited::Release();
}

void FTreeVisual_ST::Load(const char* N, IReader* data, u32 dwFlags)
{
	inherited::Load(N, data, dwFlags);
}

void FTreeVisual_ST::Render(float LOD)
{
	inherited::Render(LOD);
	RCache.set_Geometry(rm_geom);
	RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount, iBase, dwPrimitives);
	RCache.stat.r.s_flora.add(vCount);
}

#ifdef USE_DX11
void FTreeVisual_ST::RenderInstanced(const xr_vector<FloraVertData*>& data)
{
	inherited::Render(-1.f); // Setup wind constants
	inherited::DoRenderInstanced(data, vCount, iBase, dwPrimitives);
}
#else
void FTreeVisual_ST::RenderInstanced(const xr_vector<FloraVertData*>& data)
{
	// Non-DX11: fallback to individual rendering
	for (const auto* inst_data : data)
	{
		Render(-1.f);
	}
}
#endif

void FTreeVisual_ST::Copy(dxRender_Visual* pSrc)
{
	inherited::Copy(pSrc);
}

//-----------------------------------------------------------------------------------
// Progressive Tree
//-----------------------------------------------------------------------------------
FTreeVisual_PM::FTreeVisual_PM(void)
{
	pSWI = 0;
	last_lod = 0;
}

FTreeVisual_PM::~FTreeVisual_PM(void)
{
}

void FTreeVisual_PM::Release()
{
	inherited::Release();
}

void FTreeVisual_PM::Load(const char* N, IReader* data, u32 dwFlags)
{
	inherited::Load(N, data, dwFlags);
	R_ASSERT(data->find_chunk(OGF_SWICONTAINER));
	{
		u32 ID = data->r_u32();

		// Include SWI ID in the CRC
		q_hash(crc, ID);

		pSWI = RImplementation.getSWI(ID);
	}
}

void FTreeVisual_PM::Render(float LOD)
{
	inherited::Render(LOD);
	int lod_id = last_lod;
	if (LOD >= 0.f)
	{
		lod_id = iFloor((1.f - LOD) * float(pSWI->count - 1) + 0.5f);
		last_lod = lod_id;
	}
	VERIFY(lod_id >= 0 && lod_id < int(pSWI->count));
	FSlideWindow& SW = pSWI->sw[lod_id];
	RCache.set_Geometry(rm_geom);
	RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, SW.num_verts, iBase + SW.offset, SW.num_tris);
	RCache.stat.r.s_flora.add(SW.num_verts);
}

#ifdef USE_DX11
void FTreeVisual_PM::RenderInstanced(const xr_vector<FloraVertData*>& data)
{
	inherited::Render(-1.f); // Setup wind constants
	// Use last_lod which was set during insertion
	VERIFY(last_lod >= 0 && last_lod < int(pSWI->count));
	const FSlideWindow& SW = pSWI->sw[last_lod];
	inherited::DoRenderInstanced(data, SW.num_verts, iBase + SW.offset, SW.num_tris);
}
#else
void FTreeVisual_PM::RenderInstanced(const xr_vector<FloraVertData*>& data)
{
	// Non-DX11: fallback to individual rendering
	for (const auto* inst_data : data)
	{
		Render(-1.f);
	}
}
#endif

void FTreeVisual_PM::Copy(dxRender_Visual* pSrc)
{
	inherited::Copy(pSrc);
	FTreeVisual_PM* pFrom = fast_dynamic_cast<FTreeVisual_PM*>(pSrc);
	PCOPY(pSWI);
}
