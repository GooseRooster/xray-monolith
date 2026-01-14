#include "stdafx.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

// OWA Multi-Scale Bloom using Kawase downsample and tent filter upsample
// Replaces old Gaussian bloom with 4-level hierarchical blur pyramid

extern float ps_r2_bloom_threshold;
extern float ps_r2_bloom_intensity;
extern float ps_r2_bloom_radius;
extern float ps_r2_ls_bloom_speed;

void CRenderTarget::phase_bloom()
{
	PIX_EVENT(phase_bloom);
	u32 Offset;

	// Disable depth testing for all bloom passes
	RCache.set_Z(FALSE);

	// Update bloom adaptation factor
	f_bloom_factor = .9f * f_bloom_factor + .1f * ps_r2_ls_bloom_speed * Device.fTimeDelta;

	// Set bloom parameters uniform for all passes
	// bloom_params: x=threshold, y=intensity, z=radius, w=adaptation_factor
	Fvector4 bloom_params;
	bloom_params.set(ps_r2_bloom_threshold, ps_r2_bloom_intensity, ps_r2_bloom_radius, f_bloom_factor);

	//=============================================================================
	// PASS 1: Bloom extraction and initial downsample to rt_Bloom_1 (used for luminance)
	// Uses bloom_build shader with threshold extraction
	//=============================================================================
	{
		float _w = float(Device.dwWidth);
		float _h = float(Device.dwHeight);
		float _2w = _w / 2;
		float tw = BLOOM_size_X;
		float _2h = _h / 2;
		float th = BLOOM_size_Y;
		float _aspect_w = _2w / tw;
		float _aspect_h = _2h / th;
		Fvector2 one = {1.f / _w, 1.f / _h};
		one.x *= _aspect_w;
		one.y *= _aspect_h;
		Fvector2 half = {.5f / _w, .5f / _h};
		Fvector2 a_0 = {half.x + 0, half.y + 0};
		Fvector2 a_1 = {half.x + one.x, half.y + 0};
		Fvector2 a_2 = {half.x + 0, half.y + one.y};
		Fvector2 a_3 = {half.x + one.x, half.y + one.y};
		Fvector2 b_0 = {1 + a_0.x, 1 + a_0.y};
		Fvector2 b_1 = {1 + a_1.x, 1 + a_1.y};
		Fvector2 b_2 = {1 + a_2.x, 1 + a_2.y};
		Fvector2 b_3 = {1 + a_3.x, 1 + a_3.y};

		// Fill vertex buffer using existing g_bloom_build geometry
		struct v_build { Fvector4 p; Fvector2 uv0; Fvector2 uv1; Fvector2 uv2; Fvector2 uv3; };
		v_build* pv = (v_build*)RCache.Vertex.Lock(4, g_bloom_build->vb_stride, Offset);
		pv->p.set(EPS, float(th + EPS), EPS, 1.f);
		pv->uv0.set(a_0.x, b_0.y); pv->uv1.set(a_1.x, b_1.y); pv->uv2.set(a_2.x, b_2.y); pv->uv3.set(a_3.x, b_3.y);
		pv++;
		pv->p.set(EPS, EPS, EPS, 1.f);
		pv->uv0.set(a_0.x, a_0.y); pv->uv1.set(a_1.x, a_1.y); pv->uv2.set(a_2.x, a_2.y); pv->uv3.set(a_3.x, a_3.y);
		pv++;
		pv->p.set(float(tw + EPS), float(th + EPS), EPS, 1.f);
		pv->uv0.set(b_0.x, b_0.y); pv->uv1.set(b_1.x, b_1.y); pv->uv2.set(b_2.x, b_2.y); pv->uv3.set(b_3.x, b_3.y);
		pv++;
		pv->p.set(float(tw + EPS), EPS, EPS, 1.f);
		pv->uv0.set(b_0.x, a_0.y); pv->uv1.set(b_1.x, a_1.y); pv->uv2.set(b_2.x, a_2.y); pv->uv3.set(b_3.x, a_3.y);
		pv++;
		RCache.Vertex.Unlock(4, g_bloom_build->vb_stride);

		// Render bloom extraction pass (this writes to rt_Bloom_1 for luminance calculation)
		u_setrt(rt_Bloom_1, NULL, NULL, NULL);
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Element(s_bloom->E[0]);
		else
			RCache.set_Element(s_bloom_msaa->E[0]);
		RCache.set_c("bloom_params", bloom_params);
		RCache.set_c("b_params", ps_r2_bloom_threshold, ps_r2_bloom_threshold, ps_r2_bloom_threshold, f_bloom_factor);
		RCache.set_Geometry(g_bloom_build);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	// Capture luminance values (uses rt_Bloom_1 which now has extracted bloom)
	phase_luminance();

	//=============================================================================
	// Multi-Scale Bloom Pyramid using screen-space rendering
	// 4 Kawase downsample passes: Full->D2->D4->D8->D16
	// 3 Tent filter upsample passes: D16->D8->D4->D2
	//=============================================================================

	// Helper: fullscreen quad with pixel offset for proper texel sampling
	auto draw_fullscreen = [&](u32 w, u32 h)
	{
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, float(h), 0, 1, 0, 0, 1);
		pv++;
		pv->set(0, 0, 0, 1, 0, 0, 0);
		pv++;
		pv->set(float(w), float(h), 0, 1, 0, 1, 1);
		pv++;
		pv->set(float(w), 0, 0, 1, 0, 1, 0);
		pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	};

	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);
	float radius = ps_r2_bloom_radius;

	//-------------------------------------------------------------------------
	// DOWNSAMPLE PASSES (Kawase blur)
	// bloom_texel_size: xy = 1/source_width, 1/source_height, z = radius
	//-------------------------------------------------------------------------

	// D2: rt_Bloom_1 (BLOOM_size) -> rt_Bloom_D2 (1/2 res)
	// Source is BLOOM_size (fixed 256x256 typically)
	// Threshold already applied in bloom_build pass
	{
		u_setrt(rt_Bloom_D2, NULL, NULL, NULL);
		RCache.set_Element(s_bloom_downsample->E[0]);
		RCache.set_c("bloom_texel_size", 1.f / BLOOM_size_X, 1.f / BLOOM_size_Y, radius, 0);
		draw_fullscreen(u32(w / 2), u32(h / 2));
	}

	// D4: rt_Bloom_D2 -> rt_Bloom_D4 (1/4 res)
	// Source is w/2 x h/2
	{
		u_setrt(rt_Bloom_D4, NULL, NULL, NULL);
		RCache.set_Element(s_bloom_downsample->E[1]);
		RCache.set_c("bloom_texel_size", 2.f / w, 2.f / h, radius, 0);
		draw_fullscreen(u32(w / 4), u32(h / 4));
	}

	// D8: rt_Bloom_D4 -> rt_Bloom_D8 (1/8 res)
	// Source is w/4 x h/4
	{
		u_setrt(rt_Bloom_D8, NULL, NULL, NULL);
		RCache.set_Element(s_bloom_downsample->E[2]);
		RCache.set_c("bloom_texel_size", 4.f / w, 4.f / h, radius, 0);
		draw_fullscreen(u32(w / 8), u32(h / 8));
	}

	// D16: rt_Bloom_D8 -> rt_Bloom_D16 (1/16 res)
	// Source is w/8 x h/8
	{
		u_setrt(rt_Bloom_D16, NULL, NULL, NULL);
		RCache.set_Element(s_bloom_downsample->E[3]);
		RCache.set_c("bloom_texel_size", 8.f / w, 8.f / h, radius, 0);
		draw_fullscreen(u32(w / 16), u32(h / 16));
	}

	// D32: rt_Bloom_D16 -> rt_Bloom_D32 (1/32 res)
	// Source is w/16 x h/16
	{
		u_setrt(rt_Bloom_D32, NULL, NULL, NULL);
		RCache.set_Element(s_bloom_downsample->E[4]);
		RCache.set_c("bloom_texel_size", 16.f / w, 16.f / h, radius, 0);
		draw_fullscreen(u32(w / 32), u32(h / 32));
	}

	//-------------------------------------------------------------------------
	// UPSAMPLE PASSES (Tent filter with pyramid accumulation)
	// bloom_texel_size: xy = 1/output_width, 1/output_height, z = radius
	// Now 4 upsample passes: D32->D16->D8->D4->D2
	//-------------------------------------------------------------------------

	// U16: D32 + D16 -> D16 (1/16 res output)
	{
		u_setrt(rt_Bloom_D16, NULL, NULL, NULL);
		RCache.set_Element(s_bloom_upsample->E[0]);
		RCache.set_c("bloom_texel_size", 16.f / w, 16.f / h, radius, 0);
		draw_fullscreen(u32(w / 16), u32(h / 16));
	}

	// U8: D16 + D8 -> D8 (1/8 res output)
	{
		u_setrt(rt_Bloom_D8, NULL, NULL, NULL);
		RCache.set_Element(s_bloom_upsample->E[1]);
		RCache.set_c("bloom_texel_size", 8.f / w, 8.f / h, radius, 0);
		draw_fullscreen(u32(w / 8), u32(h / 8));
	}

	// U4: D8 + D4 -> D4 (1/4 res output)
	{
		u_setrt(rt_Bloom_D4, NULL, NULL, NULL);
		RCache.set_Element(s_bloom_upsample->E[2]);
		RCache.set_c("bloom_texel_size", 4.f / w, 4.f / h, radius, 0);
		draw_fullscreen(u32(w / 4), u32(h / 4));
	}

	// U2: D4 + D2 -> D2 (1/2 res output - final bloom)
	{
		u_setrt(rt_Bloom_D2, NULL, NULL, NULL);
		RCache.set_Element(s_bloom_upsample->E[3]);
		RCache.set_c("bloom_texel_size", 2.f / w, 2.f / h, radius, 0);
		draw_fullscreen(u32(w / 2), u32(h / 2));
	}

	// Note: rt_Bloom_D2 now contains the final accumulated multi-scale bloom
	// It will be sampled in combine pass via s_bloom_d2 texture

	//=============================================================================
	// POST-BLOOM SETUP
	//=============================================================================

	// Handle menu post-processing if needed
	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
	if (_menu_pp)
	{
		FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		HW.pContext->ClearRenderTargetView(RCache.get_RT(), ColorRGBA);
	}

	// Re-enable depth testing
	RCache.set_Z(TRUE);
}
