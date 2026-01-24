#include "stdafx.h"
#include "../xrRender/du_cone.h"

//extern Fvector du_cone_vertices[DU_CONE_NUMVERTEX];

void CRenderTarget::accum_spot(light* L)
{
	phase_accumulator();
	RImplementation.stats.l_visible ++;

	// *** assume accumulator already setup ***
	// *****************************	Mask by stencil		*************************************
	ref_shader shader;
	ref_shader* shader_msaa;
	if (IRender_Light::OMNIPART == L->flags.type)
	{
		shader = L->s_point;
		shader_msaa = L->s_point_msaa;
		if (!shader)
		{
			shader = s_accum_point;
			shader_msaa = s_accum_point_msaa;
		}
	}
	else
	{
		shader = L->s_spot;
		shader_msaa = L->s_spot_msaa;
		if (!shader)
		{
			shader = s_accum_spot;
			shader_msaa = s_accum_spot_msaa;
		}
	}

	BOOL bIntersect = FALSE; //enable_scissor(L);
	{
		// setup xform
		L->xform_calc();
		RCache.set_xform_world(L->m_xform);
		RCache.set_xform_view(Device.mView);
		RCache.set_xform_project(Device.mProject);
		bIntersect = enable_scissor(L);
		enable_dbt_bounds(L);

		// *** similar to "Carmack's reverse", but assumes convex, non intersecting objects,
		// *** thus can cope without stencil clear with 127 lights
		// *** in practice, 'cause we "clear" it back to 0x1 it usually allows us to > 200 lights :)
		//	Done in blender!
		//RCache.set_ColorWriteEnable		(FALSE);
		RCache.set_Element(s_accum_mask->E[SE_MASK_SPOT]); // masker

		// backfaces: if (stencil>=1 && zfail)			stencil = light_id
		RCache.set_CullMode(CULL_CW);
		if (! RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0x01, 0xff, D3DSTENCILOP_KEEP,
			                   D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE);
		else
			RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0x01, 0x7f, D3DSTENCILOP_KEEP,
			                   D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE);
		draw_volume(L);

		// frontfaces: if (stencil>=light_id && zfail)	stencil = 0x1
		RCache.set_CullMode(CULL_CCW);
		if (! RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP,
			                   D3DSTENCILOP_REPLACE);
		else
			RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0x7f, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP,
			                   D3DSTENCILOP_REPLACE);
		draw_volume(L);
	}

	// nv-stencil recompression
	if (RImplementation.o.nvstencil) u_stencil_optimize();

	// *****************************	Minimize overdraw	*************************************
	// Select shader (front or back-faces), *** back, if intersect near plane
	RCache.set_ColorWriteEnable();
	RCache.set_CullMode(CULL_CW); // back

	// 2D texgens 
	Fmatrix m_Texgen;
	u_compute_texgen_screen(m_Texgen);
	Fmatrix m_Texgen_J;
	u_compute_texgen_jitter(m_Texgen_J);

	// Shadow xform (+texture adjustment matrix)
	Fmatrix m_Shadow, m_Lmap;
	{
		float smapsize = float(RImplementation.o.smapsize);
		float fTexelOffs = (.5f / smapsize);
		float view_dim = float(L->X.S.size - 2) / smapsize;
		float view_sx = float(L->X.S.posX + 1) / smapsize;
		float view_sy = float(L->X.S.posY + 1) / smapsize;
		float fRange = float(1.f) * ps_r2_ls_depth_scale;
		float fBias = ps_r2_ls_depth_bias;
		Fmatrix m_TexelAdjust = {
			view_dim / 2.f, 0.0f, 0.0f, 0.0f,
			0.0f, -view_dim / 2.f, 0.0f, 0.0f,
			0.0f, 0.0f, fRange, 0.0f,
			view_dim / 2.f + view_sx + fTexelOffs, view_dim / 2.f + view_sy + fTexelOffs, fBias, 1.0f
		};

		// compute xforms
		Fmatrix xf_view = L->X.S.view;
		Fmatrix xf_project;
		xf_project.mul(m_TexelAdjust, L->X.S.project);
		m_Shadow.mul(xf_view, Device.mInvView);
		m_Shadow.mulA_44(xf_project);

		// lmap
		view_dim = 1.f;
		view_sx = 0.f;
		view_sy = 0.f;
		Fmatrix m_TexelAdjust2 = {
			view_dim / 2.f, 0.0f, 0.0f, 0.0f,
			0.0f, -view_dim / 2.f, 0.0f, 0.0f,
			0.0f, 0.0f, fRange, 0.0f,
			view_dim / 2.f + view_sx + fTexelOffs, view_dim / 2.f + view_sy + fTexelOffs, fBias, 1.0f
		};

		// compute xforms
		xf_project.mul(m_TexelAdjust2, L->X.S.project);
		m_Lmap.mul(xf_view, Device.mInvView);
		m_Lmap.mulA_44(xf_project);
	}

	// Common constants
	Fvector L_clr, L_pos; // L_dir
	float L_spec;
	L_clr.set(L->color.r, L->color.g, L->color.b);
	L_clr.mul(L->get_LOD());
	L_spec = u_diffuse2s(L_clr);
	Device.mView.transform_tiny(L_pos, L->position);
	//Device.mView.transform_dir(L_dir, L->direction);
	//L_dir.normalize();

	// Draw volume with projective texgen
	{
		// Select shader
		u32 _id = 0;
		// OWA: Force unshadowed path in static lighting mode (R1 aesthetic - no dynamic shadows for local lights)
		if (L->flags.bShadow && !RImplementation.o.staticlighting)
		{
			bool bFullSize = (L->X.S.size == RImplementation.o.smapsize);
			if (L->X.S.transluent) _id = SE_L_TRANSLUENT;
			else if (bFullSize) _id = SE_L_FULLSIZE;
			else _id = SE_L_NORMAL;
		}
		else
		{
			_id = SE_L_UNSHADOWED;
			m_Shadow = m_Lmap;
		}
		RCache.set_Element(shader->E[_id]);

		RCache.set_CullMode(CULL_CW); // back

		// Constants
		float att_R = L->range * .95f;
		float att_factor = 1.f / (att_R * att_R);
		RCache.set_c("Ldynamic_pos", L_pos.x, L_pos.y, L_pos.z, att_factor);
		RCache.set_c("Ldynamic_color", L_clr.x, L_clr.y, L_clr.z, L_spec);
		RCache.set_c("m_texgen", m_Texgen);
		RCache.set_c("m_texgen_J", m_Texgen_J);
		RCache.set_c("m_shadow", m_Shadow);
		RCache.set_ca("m_lmap", 0, m_Lmap._11, m_Lmap._21, m_Lmap._31, m_Lmap._41);
		RCache.set_ca("m_lmap", 1, m_Lmap._12, m_Lmap._22, m_Lmap._32, m_Lmap._42);

		if (!Device.m_SecondViewport.IsSVPFrame())
			RCache.set_c("sss_id", L->sss_id);
		else
			RCache.set_c("sss_id", -1);

		// Fetch4 : enable
		//		if (RImplementation.o.HW_smap_FETCH4)	{
		//. we hacked the shader to force smap on S0
		//#			define FOURCC_GET4  MAKEFOURCC('G','E','T','4') 
		//			HW.pDevice->SetSamplerState	( 0, D3DSAMP_MIPMAPLODBIAS, FOURCC_GET4 );
		//		}

		if (! RImplementation.o.dx10_msaa)
		{
			RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0xff, 0x00);
			draw_volume(L);
		}
		else
		{
			// per pixel
			RCache.set_Element(shader->E[_id]);
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID, 0xff, 0x00);
			RCache.set_CullMode(D3DCULL_CW);
			draw_volume(L);
			// per sample		
			if (RImplementation.o.dx10_msaa_opt)
			{
				RCache.set_Element(shader_msaa[0]->E[_id]);
				RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID | 0x80, 0xff, 0x00);
				RCache.set_CullMode(D3DCULL_CW);
				draw_volume(L);
			}
			else // checked Holger
			{
				for (u32 i = 0; i < RImplementation.o.dx10_msaa_samples; ++i)
				{
					RCache.set_Element(shader_msaa[i]->E[_id]);
					StateManager.SetSampleMask(u32(1) << i);
					RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID | 0x80, 0xff, 0x00);
					RCache.set_CullMode(D3DCULL_CW);
					draw_volume(L);
				}
				StateManager.SetSampleMask(0xffffffff);
			}
			RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0xff, 0x00);
		}

		// Fetch4 : disable
		//		if (RImplementation.o.HW_smap_FETCH4)	{
		//. we hacked the shader to force smap on S0
		//#			define FOURCC_GET1  MAKEFOURCC('G','E','T','1') 
		//			HW.pDevice->SetSamplerState	( 0, D3DSAMP_MIPMAPLODBIAS, FOURCC_GET1 );
		//		}
	}

	// blend-copy
	if (!RImplementation.o.fp16_blend)
	{
		if (!RImplementation.o.dx10_msaa)
			u_setrt(rt_Accumulator,NULL,NULL, HW.pBaseZB);
		else
			u_setrt(rt_Accumulator,NULL,NULL, rt_MSAADepth->pZRT);
		RCache.set_Element(s_accum_mask->E[SE_MASK_ACCUM_VOL]);
		RCache.set_c("m_texgen", m_Texgen);
		RCache.set_c("m_texgen_J", m_Texgen_J);
		if (!RImplementation.o.dx10_msaa)
		{
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID, 0xff, 0x00);
			draw_volume(L);
		}
		else // checked Holger
		{
			// per pixel
			RCache.set_Element(s_accum_mask->E[SE_MASK_ACCUM_VOL]);
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID, 0xff, 0x00);
			draw_volume(L);
			// per sample
			if (RImplementation.o.dx10_msaa_opt)
			{
				RCache.set_Element(s_accum_mask_msaa[0]->E[SE_MASK_ACCUM_VOL]);
				RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID | 0x80, 0xff, 0x00);
				draw_volume(L);
			}
			else // checked Holger
			{
				for (u32 i = 0; i < RImplementation.o.dx10_msaa_samples; ++i)
				{
					RCache.set_Element(s_accum_mask_msaa[i]->E[SE_MASK_ACCUM_VOL]);
					StateManager.SetSampleMask(u32(1) << i);
					RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID | 0x80, 0xff, 0x00);
					draw_volume(L);
				}
				StateManager.SetSampleMask(0xffffffff);
			}
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, dwLightMarkerID, 0xff, 0x00);
		}
	}

	RCache.set_Scissor(0);
	//CHK_DX		(HW.pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
	//dwLightMarkerID					+=	2;	// keep lowest bit always setted up
	increment_light_marker();

	u_DBT_disable();
}

void CRenderTarget::accum_volumetric(light* L)
{
	// OWA: Raymarched volumetric spotlight (Peak Volumetric Lights port)
	// Replaces the vanilla slice-based approach with screen-space raymarching
	// for smoother, more physically accurate volumetric lighting.

	// Null check
	if (L == nullptr)
		return;

	// [ SSS ] Fade through distance volumetric lights.
	if (ps_ssfx_volumetric.x > 0)
	{
		float Falloff = ps_ssfx_volumetric.y - std::min(std::max((L->vis.distance - 20) * 0.01f, 0.0f), 1.0f) * ps_ssfx_volumetric.y;
		L->m_volumetric_intensity = Falloff;
		L->flags.bVolumetric = Falloff <= 0 ? false : true;
	}

	if (!L->flags.bVolumetric)
		return;

	// Setup render target
	if (!RImplementation.o.ssfx_volumetric)
	{
		phase_vol_accumulator();
	}
	else
	{
		if (!m_bHasActiveVolumetric_spot)
		{
			m_bHasActiveVolumetric_spot = true;

			FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			HW.pContext->ClearRenderTargetView(rt_ssfx_volumetric->pRT, ColorRGBA);
		}

		u_setrt(rt_ssfx_volumetric, NULL, NULL, NULL);

		RCache.set_Stencil(FALSE);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_ColorWriteEnable();
	}

	// Set transforms - geometry transformed in VS
	L->xform_calc();
	RCache.set_xform_world(L->m_xform);
	RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);

	// Shadow xform (+texture adjustment matrix)
	Fmatrix m_Shadow;
	{
		float smapsize = float(RImplementation.o.smapsize);
		float fTexelOffs = (.5f / smapsize);
		float view_dim = float(L->X.S.size - 2) / smapsize;
		float view_sx = float(L->X.S.posX + 1) / smapsize;
		float view_sy = float(L->X.S.posY + 1) / smapsize;
		float fRange = float(1.f) * ps_r2_ls_depth_scale;
		float fBias = ps_r2_ls_depth_bias;

		Fmatrix m_TexelAdjust = {
			view_dim / 2.f, 0.0f, 0.0f, 0.0f,
			0.0f, -view_dim / 2.f, 0.0f, 0.0f,
			0.0f, 0.0f, fRange, 0.0f,
			view_dim / 2.f + view_sx + fTexelOffs, view_dim / 2.f + view_sy + fTexelOffs, fBias, 1.0f
		};

		// Compute xforms - world space shadow matrix for raymarching shader
		Fmatrix xf_world;
		xf_world.invert(Device.mView);
		Fmatrix xf_view = L->X.S.view;
		Fmatrix xf_project;
		xf_project.mul(m_TexelAdjust, L->X.S.project);

		m_Shadow.set(xf_view);
		m_Shadow.mulA_44(xf_project);
	}

	// Light direction (spot), color, and position - all in WORLD SPACE for raymarching
	Fvector L_dir, L_clr, L_pos;
	L_clr.set(L->color.r, L->color.g, L->color.b);
	L_clr.mul(L->m_volumetric_intensity);
	L_clr.mul(L->m_volumetric_distance);
	L_clr.mul(L->get_LOD());

	L_pos.set(L->position);
	L_dir.set(L->direction);
	L_dir.normalize();

	// Attenuation - range-based falloff
	float att_R = L->m_volumetric_distance * L->range * .95f;
	float att_factor = 1.f / (att_R * att_R);

	// Set the raymarching shader (s_combine element 5)
	RCache.set_Element(s_combine->E[5]);
	RCache.set_CullMode(CULL_CW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	// Set shader constants
	// Note: These are in WORLD SPACE, unlike the vanilla volumetric which uses view space
	RCache.set_c("Ldynamic_pos", L_pos.x, L_pos.y, L_pos.z, att_factor);
	RCache.set_c("Ldynamic_dir", L_dir.x, L_dir.y, L_dir.z, -cosf(L->cone * 0.5f));
	RCache.set_c("Ldynamic_color", L_clr.x, L_clr.y, L_clr.z, L->get_LOD());
	RCache.set_c("m_shadow", m_Shadow);

	// Render the light volume geometry - raymarching happens in pixel shader
	draw_volume(L);

	RCache.set_Scissor(0);
}
