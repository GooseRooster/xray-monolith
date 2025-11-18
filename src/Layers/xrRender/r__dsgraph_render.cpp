#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "../../xrEngine/irenderable.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/CustomHUD.h"

#include "FBasicVisual.h"
#include "CHudInitializer.h"

#include "fhierrarhyvisual.h"
#include "SkeletonCustom.h"
#include "../../xrEngine/fmesh.h"
#include "flod.h"

#include "../../xrEngine/xr_object.h"

using namespace R_dsgraph;

extern float r_ssaDISCARD;
extern float r_ssaDONTSORT;
extern float r_ssaHZBvsTEX;
extern float r_ssaGLOD_start, r_ssaGLOD_end;

ICF float calcLOD(float ssa/*fDistSq*/, float R)
{
	return _sqrt(clampr((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
}

// ALPHA
void __fastcall sorted_L1(mapSorted_Node* N)
{
	PROF_EVENT("sorted_L1");
	VERIFY(N);
	dxRender_Visual* V = N->val.pVisual;
	VERIFY(V && V->shader._get());
	RCache.set_Element(N->val.se);
	RCache.set_xform_world(N->val.Matrix);
	RImplementation.apply_object(N->val.pObject);
	RImplementation.apply_lmaterial();
	V->Render(calcLOD(N->key, V->vis.sphere.R));
}

void __fastcall water_node_ssr(mapSorted_Node* N)
{
#ifdef USE_DX11
	VERIFY(N);
	dxRender_Visual* V = N->val.pVisual;
	VERIFY(V);

	RCache.set_Shader(RImplementation.Target->s_ssfx_water_ssr);

	RCache.set_xform_world(N->val.Matrix);
	RImplementation.apply_object(N->val.pObject);
	RImplementation.apply_lmaterial();

	RCache.set_c("cam_pos", RImplementation.Target->Position_previous.x, RImplementation.Target->Position_previous.y, RImplementation.Target->Position_previous.z, 0.0f);

	// Previous matrix data
	RCache.set_c("m_current", RImplementation.Target->Matrix_current);
	RCache.set_c("m_previous", RImplementation.Target->Matrix_previous);

	V->Render(calcLOD(N->key, V->vis.sphere.R));
#endif
}

void __fastcall water_node(mapSorted_Node* N)
{
	VERIFY(N);
	dxRender_Visual* V = N->val.pVisual;
	VERIFY(V);

#ifdef USE_DX11
	if (RImplementation.o.ssfx_water)
	{
		RCache.set_Shader(RImplementation.Target->s_ssfx_water);
	}
#endif

	RCache.set_xform_world(N->val.Matrix);
	RImplementation.apply_object(N->val.pObject);
	RImplementation.apply_lmaterial();

	// Wind settings
	float WindDir = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
	float WindVel = g_pGamePersistent->Environment().CurrentEnv->wind_velocity;
	RCache.set_c("wind_setup", WindDir, WindVel, 0, 0);

	V->Render(calcLOD(N->key, V->vis.sphere.R));
}

void R_dsgraph_structure::r_dsgraph_render_graph(u32 _priority, bool _clear)
{
	PROF_EVENT("r_dsgraph_render_graph");
	Device.Statistic->RenderDUMP.Begin();

	// **************************************************** NORMAL
	// Perform sorting based on ScreenSpaceArea
	// Sorting by SSA and changes minimizations
	{
		RCache.set_xform_world(Fidentity);

		// Render several passes
		PROF_EVENT("NORMAL_SHADER_PASSES");
		for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
		{
			//mapNormalVS&	vs				= mapNormal	[_priority];
			mapNormalVS& vs = mapNormalPasses[_priority][iPass];
			for (mapNormalVS::TNode& Nvs : vs)
			{
				RCache.set_VS(Nvs.key);

#if defined(USE_DX10) || defined(USE_DX11)
				//	GS setup
				mapNormalGS& gs = Nvs.val;

				for (mapNormalGS::TNode& Ngs : gs)
				{
					RCache.set_GS(Ngs.key);

					mapNormalPS& ps = Ngs.val;
#else	//	USE_DX10
				mapNormalPS& ps = Nvs.val;
#endif	//	USE_DX10

				for (mapNormalPS::TNode& Nps : ps)
				{
					RCache.set_PS(Nps.key);
#ifdef USE_DX11
					mapNormalCS& cs = Nps.val.mapCS;
					RCache.set_HS(Nps.val.hs);
					RCache.set_DS(Nps.val.ds);
#else
					mapNormalCS& cs = Nps.val;
#endif
					for (mapNormalCS::TNode& Ncs : cs)
					{
						RCache.set_Constants(Ncs.key);

						mapNormalStates& states = Ncs.val;
						for (mapNormalStates::TNode& Nstate : states)
						{
							RCache.set_States(Nstate.key);

							mapNormalTextures& tex = Nstate.val;
							for (mapNormalTextures::TNode& Ntex : tex)
							{
								RCache.set_Textures(Ntex.key);
								RImplementation.apply_lmaterial();

								mapNormalItems& items = Ntex.val;
								for (_NormalItem& Ni : items)
								{
									float LOD = calcLOD(Ni.ssa, Ni.pVisual->vis.sphere.R);
#ifdef USE_DX11
									RCache.LOD.set_LOD(LOD);
#endif
									Ni.pVisual->Render(LOD);
								}
								if (_clear) items.clear();
							}
							if (_clear) tex.clear();
						}
						if (_clear) states.clear();
					}
					if (_clear) cs.clear();
				}
				if (_clear) ps.clear();
#if defined(USE_DX10) || defined(USE_DX11)
				}
			if (_clear) gs.clear();
#endif
			}
		if (_clear) vs.clear();
		}
	}

	// **************************************************** MATRIX
	// Perform sorting based on ScreenSpaceArea
	// Sorting by SSA and changes minimizations
	// Render several passes
	PROF_EVENT("MATRIX_SHADER_PASSES");
	for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
	{
		//mapMatrixVS&	vs				= mapMatrix	[_priority];
		mapMatrixVS& vs = mapMatrixPasses[_priority][iPass];
		for (mapMatrixVS::TNode& Nvs : vs)
		{
			RCache.set_VS(Nvs.key);

#if defined(USE_DX10) || defined(USE_DX11)
			mapMatrixGS& gs = Nvs.val;

			for (mapMatrixGS::TNode& Ngs : gs)
			{
				RCache.set_GS(Ngs.key);

				mapMatrixPS& ps = Ngs.val;
#else	//	USE_DX10
			mapMatrixPS& ps = Nvs.val;
#endif	//	USE_DX10

			for (mapMatrixPS::TNode& Nps : ps)
			{
				RCache.set_PS(Nps.key);

#ifdef USE_DX11
				mapMatrixCS& cs = Nps.val.mapCS;
				RCache.set_HS(Nps.val.hs);
				RCache.set_DS(Nps.val.ds);
#else
				mapMatrixCS& cs = Nps.val;
#endif
				for (mapMatrixCS::TNode& Ncs : cs)
				{
					RCache.set_Constants(Ncs.key);

					mapMatrixStates& states = Ncs.val;
					for (mapMatrixStates::TNode& Nstate : states)
					{
						RCache.set_States(Nstate.key);

						mapMatrixTextures& tex = Nstate.val;
						for (mapMatrixTextures::TNode& Ntex : tex)
						{
							RCache.set_Textures(Ntex.key);
							RImplementation.apply_lmaterial();

							mapMatrixItems& items = Ntex.val;
							for (_MatrixItem& Ni : items)
							{
								if (Ni.pVisual->shader == nullptr)
								{
									continue;
								}
								RCache.set_xform_world(Ni.Matrix);
								RImplementation.apply_object(Ni.pObject);
								RImplementation.apply_lmaterial();

								float LOD = calcLOD(Ni.ssa, Ni.pVisual->vis.sphere.R);
#ifdef USE_DX11
								RCache.LOD.set_LOD(LOD);
#endif
								Ni.pVisual->Render(LOD);
							}
							if (_clear) items.clear();
						}
						if (_clear) tex.clear();
					}
					if (_clear) states.clear();
				}
				if (_clear) cs.clear();
			}
			if (_clear) ps.clear();
#if defined(USE_DX10) || defined(USE_DX11)
			}
		if (_clear) gs.clear();
#endif
		}
	if (_clear) vs.clear();
	}

	Device.Statistic->RenderDUMP.End();
}

//////////////////////////////////////////////////////////////////////////
// HUD render
void R_dsgraph_structure::r_dsgraph_render_hud(bool NoPS)
{
	PROF_EVENT("r_dsgraph_render_hud");
	CHudInitializer initializer(true);

	// Rendering
	rmNear();
	if (!NoPS)
	{
		mapHUD.traverseLR(sorted_L1);
		mapHUD.clear();

		rmNormal();
		
#if defined(USE_DX11) //  Redotix99: for 3D Shader Based Scopes 		

		if (scope_3D_fake_enabled)
		{
			RCache.set_RT(RImplementation.Target->rt_ssfx_temp->pRT, 3); // Render scope_3D to any buffer
			
			mapScopeHUD.traverseLR(sorted_L1);

			if (!RImplementation.o.ssfx_motionvectors)
				RCache.set_RT(NULL, 3);
			else
				RCache.set_RT(RImplementation.Target->rt_ssfx_motion_vectors->pRT, 3);
		}
		mapScopeHUD.clear();
#endif

		if (mapCamAttached.size())
		{
			rmNear();

			// Change projection
			initializer.SetCamMode();

			// Rendering
			mapCamAttached.traverseLR(sorted_L1);
			mapCamAttached.clear();

			rmNormal();
		}
	}
	/*else
	{
		HUDMask.traverseLR(hud_node);
		HUDMask.clear();

		if (HUDMaskCamAttached.size())
		{
			rmNear();

			// Change projection
			initializer.SetCamMode();

			// Rendering
			HUDMaskCamAttached.traverseLR(hud_node);
			HUDMaskCamAttached.clear();
		}

		rmNormal();
	}*/
}

void R_dsgraph_structure::r_dsgraph_render_hud_ui()
{
	PROF_EVENT("r_dsgraph_render_hud_ui");
	CHudInitializer initializer(true);

	// Rendering
	rmNear();
	g_hud->RenderActiveItemUI();
	rmNormal();
}

void R_dsgraph_structure::r_dsgraph_render_cam_ui()
{
	// Change projection
	CHudInitializer initializer(2);
	
	// Rendering
	rmNear();
	g_hud->RenderCamAttachedUI();
	rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void R_dsgraph_structure::r_dsgraph_render_sorted()
{
	PROF_EVENT("r_dsgraph_render_sorted");
	// Sorted (back to front)
	mapSorted.traverseRL(sorted_L1);
	mapSorted.clear();

	CHudInitializer initializer(true);

	// Rendering
	rmNear();
	mapHUDSorted.traverseRL(sorted_L1);
	mapHUDSorted.clear();

	// Camera Script Attachments
	if (mapCamAttachedSorted.size())
	{
		// Change projection
		initializer.SetCamMode();
		
		// Rendering
		mapCamAttachedSorted.traverseRL(sorted_L1);
		mapCamAttachedSorted.clear();
	}

	rmNormal();
}

#if defined(USE_DX11)
//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void R_dsgraph_structure::r_dsgraph_render_ScopeSorted()  //  Redotix99: for 3D Shader Based Scopes 	
{
	// Change projection
	CHudInitializer initializer(true);

	// Rendering
	rmNear();
	mapScopeHUDSorted.traverseRL(sorted_L1);
	mapScopeHUDSorted.clear();
	rmNormal();
}
#endif

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void R_dsgraph_structure::r_dsgraph_render_emissive(bool clear, bool renderHUD)
{
	PROF_EVENT("r_dsgraph_render_emissive");
#if	RENDER!=R_R1
	// Sorted (back to front)
	mapEmissive.traverseLR(sorted_L1);
	if (clear)
		mapEmissive.clear();

	CHudInitializer initializer(true);

	// Rendering
	rmNear();
	// Sorted (back to front)
	mapHUDEmissive.traverseLR(sorted_L1);
	
	if (clear)
		mapHUDEmissive.clear();

	if (renderHUD)
		mapHUDSorted.traverseRL(sorted_L1);

	rmNormal();
#endif
}

void R_dsgraph_structure::r_dsgraph_render_water_ssr()
{
	mapWater.traverseLR(water_node_ssr);
}

void R_dsgraph_structure::r_dsgraph_render_water()
{
	mapWater.traverseLR(water_node);
	mapWater.clear();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void R_dsgraph_structure::r_dsgraph_render_wmarks()
{
	PROF_EVENT("r_dsgraph_render_wmarks");
#if	RENDER!=R_R1
	// Sorted (back to front)
	mapWmark.traverseLR(sorted_L1);
	mapWmark.clear();
#endif
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void R_dsgraph_structure::r_dsgraph_render_distort()
{
	PROF_EVENT("r_dsgraph_render_distort");
	// Sorted (back to front)
	mapDistort.traverseRL(sorted_L1);
	mapDistort.clear();

	// Change projection
	CHudInitializer initializer(true);

	rmNear();
	mapHUDDistort.traverseLR(sorted_L1);
	mapHUDDistort.clear();
	rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// sub-space rendering - shortcut to render with frustum extracted from matrix
void R_dsgraph_structure::r_dsgraph_render_subspace(IRender_Sector* _sector, Fmatrix& mCombined, Fvector& _cop,
                                                    BOOL _dynamic, BOOL _precise_portals, CObject* O)
{
	if(!_sector) return;
	PROF_EVENT("r_dsgraph_render_subspace");
	CFrustum temp;
	temp.CreateFromMatrix(mCombined, FRUSTUM_P_ALL & (~FRUSTUM_P_NEAR));
	r_dsgraph_render_subspace(_sector, &temp, mCombined, _cop, _dynamic, _precise_portals, O);
}

// sub-space rendering - main procedure
extern float IK_CALC_DIST;
extern float IK_CALC_SSA;
void R_dsgraph_structure::r_dsgraph_render_subspace(IRender_Sector* _sector, CFrustum* _frustum, Fmatrix& mCombined,
                                                    Fvector& _cop, BOOL _dynamic, BOOL _precise_portals, CObject* O)
{
	VERIFY(_sector);
	RImplementation.marker ++; // !!! critical here

	// Save and build new frustum, disable HOM
	CFrustum ViewSave = ViewBase;
	ViewBase = *_frustum;
	View = &ViewBase;

	if (_precise_portals && RImplementation.rmPortals)
	{
		PROF_EVENT("precise_portals");
		// Check if camera is too near to some portal - if so force DualRender
		Fvector box_radius;
		box_radius.set(EPS_L * 20, EPS_L * 20, EPS_L * 20);
		RImplementation.Sectors_xrc.box_options(CDB::OPT_FULL_TEST);
		RImplementation.Sectors_xrc.box_query(RImplementation.rmPortals, _cop, box_radius);
		for (int K = 0; K < RImplementation.Sectors_xrc.r_count(); K++)
		{
			CPortal* pPortal = (CPortal*)RImplementation.Portals[RImplementation.rmPortals->get_tris()[RImplementation
			                                                                                           .Sectors_xrc.
			                                                                                           r_begin()[K].id].
				dummy];
			pPortal->bDualRender = TRUE;
		}
	}

	// Traverse sector/portal structure
	PortalTraverser.traverse(_sector, ViewBase, _cop, mCombined, 0);

	// Determine visibility for static geometry hierrarhy
	{
		PROF_EVENT("add_static")
		for (u32 s_it = 0; s_it < PortalTraverser.r_sectors.size(); s_it++)
		{
			CSector* sector = (CSector*)PortalTraverser.r_sectors[s_it];
			dxRender_Visual* root = sector->root();
			for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
			{
				set_Frustum(&(sector->r_frustums[v_it]));
				add_Geometry(root);
			}
		}
	}

	if (_dynamic)
	{
		PROF_EVENT("add_dynamic");
		set_Object(0);

		// Traverse object database
		g_SpatialSpace->q_frustum
		(
			lstRenderables,
			ISpatial_DB::O_ORDERED,
			STYPE_RENDERABLE,
			ViewBase
		);

		// Determine visibility for dynamic part of scene
		for (u32 o_it = 0; o_it < lstRenderables.size(); o_it++)
		{
			ISpatial* spatial = lstRenderables[o_it].get();
			CSector* sector = (CSector*)spatial->spatial.sector;
			if (0 == sector) continue; // disassociated from S/P structure
			if (PortalTraverser.i_marker != sector->r_marker) continue; // inactive (untouched) sector
			for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
			{
				set_Frustum(&(sector->r_frustums[v_it]));
				if (!View->testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R)) continue;

				// renderable
				IRenderable* renderable = spatial->dcast_Renderable();
				if (0 == renderable) continue; // unknown, but renderable object (r1_glow???)
#if RENDER!=R_R1
				float ssa = Device.CalcSSADynamic(spatial->spatial.sphere.P, spatial->spatial.sphere.R);
				if (ssa >= IK_CALC_SSA)
				{
					CKinematics* pKin = (CKinematics*)renderable->renderable.visual;
					if(pKin)
					{
						if(spatial->spatial.type&STYPE_RENDERABLESHADOW)
						{
							pKin->CalculateBones(TRUE);
						}
						if(spatial->spatial.type&STYPE_RENDERABLE)
						{
							if(0==ViewSave.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
							{
								pKin->CalculateBones(TRUE);
							}
						}
					}
				}
#endif
				if (O && O->dcast_Renderable() == renderable) continue;

				renderable->renderable_Render();
			}
		}
	}

	// Restore
	ViewBase = ViewSave;
	View = 0;
}

void R_dsgraph_structure::r_dsgraph_render_R1_box(IRender_Sector* _S, Fbox& BB, int sh)
{
	CSector* S = (CSector*)_S;
	lstVisuals.clear();
	lstVisuals.push_back(S->root());

	for (u32 test = 0; test < lstVisuals.size(); test++)
	{
		dxRender_Visual* V = lstVisuals[test];

		// Visual is 100% visible - simply add it
		xr_vector<IRenderVisual*>::iterator I, E; // it may be usefull for 'hierrarhy' visuals

		switch (V->Type)
		{
		case MT_HIERRARHY:
			{
				// Add all children
				FHierrarhyVisual* pV = (FHierrarhyVisual*)V;
				I = pV->children.begin();
				E = pV->children.end();
				for (; I != E; ++I)
				{
					dxRender_Visual* T = (dxRender_Visual*)*I;
					if (BB.intersect(T->vis.box)) lstVisuals.push_back(T);
				}
			}
			break;
		case MT_SKELETON_ANIM:
		case MT_SKELETON_RIGID:
			{
				// Add all children	(s)
				CKinematics* pV = (CKinematics*)V;
				pV->CalculateBones(TRUE);
				I = pV->children.begin();
				E = pV->children.end();
				for (; I != E; ++I)
				{
					dxRender_Visual* T = (dxRender_Visual*)*I;
					if (BB.intersect(T->vis.box)) lstVisuals.push_back(T);
				}
			}
			break;
		case MT_LOD:
			{
				FLOD* pV = (FLOD*)V;
				I = pV->children.begin();
				E = pV->children.end();
				for (; I != E; ++I)
				{
					dxRender_Visual* T = (dxRender_Visual*)*I;
					if (BB.intersect(T->vis.box)) lstVisuals.push_back(T);
				}
			}
			break;
		default:
			{
				// Renderable visual
				ShaderElement* E = V->shader->E[sh]._get();
				if (E && !(E->flags.bDistort))
				{
					for (u32 pass = 0; pass < E->passes.size(); pass++)
					{
						RCache.set_Element(E, pass);
						V->Render(-1.f);
					}
				}
			}
			break;
		}
	}
}
