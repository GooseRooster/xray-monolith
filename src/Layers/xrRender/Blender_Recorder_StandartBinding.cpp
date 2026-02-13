#include "stdafx.h"
#pragma hdrstop

#pragma warning(push)
#pragma warning(disable:4995)
#include <d3dx9.h>
#pragma warning(pop)

#include "ResourceManager.h"
#include "blenders\Blender_Recorder.h"
#include "blenders\Blender.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "dxRenderDeviceRender.h"

// matrices
#define	BIND_DECLARE(xf)	\
class cl_xform_##xf	: public R_constant_setup {	virtual void setup (R_constant* C) { RCache.xforms.set_c_##xf (C); } }; \
	static cl_xform_##xf	binder_##xf
BIND_DECLARE(w);
BIND_DECLARE(invw);
BIND_DECLARE(v);
BIND_DECLARE(p);
BIND_DECLARE(wv);
BIND_DECLARE(vp);
BIND_DECLARE(wvp);

BIND_DECLARE(v_prev);
BIND_DECLARE(p_prev);
BIND_DECLARE(wv_prev);
BIND_DECLARE(vp_prev);
BIND_DECLARE(wvp_prev);

#define DECLARE_TREE_BIND(c)	\
	class cl_tree_##c: public R_constant_setup	{virtual void setup(R_constant* C) {RCache.tree.set_c_##c(C);} };	\
	static cl_tree_##c	tree_binder_##c

DECLARE_TREE_BIND(m_xform_v);
DECLARE_TREE_BIND(m_xform);
DECLARE_TREE_BIND(consts);
DECLARE_TREE_BIND(wave);
DECLARE_TREE_BIND(wind);
DECLARE_TREE_BIND(c_scale);
DECLARE_TREE_BIND(c_bias);
DECLARE_TREE_BIND(c_sun);

class cl_hemi_cube_pos_faces : public R_constant_setup
{
	virtual void setup(R_constant* C) { RCache.hemi.set_c_pos_faces(C); }
};

static cl_hemi_cube_pos_faces binder_hemi_cube_pos_faces;

class cl_hemi_cube_neg_faces : public R_constant_setup
{
	virtual void setup(R_constant* C) { RCache.hemi.set_c_neg_faces(C); }
};

static cl_hemi_cube_neg_faces binder_hemi_cube_neg_faces;

class cl_material : public R_constant_setup
{
	virtual void setup(R_constant* C) { RCache.hemi.set_c_material(C); }
};

static cl_material binder_material;

class cl_texgen : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f
		};
#else	//	USE_DX10
		float _w = float(RDEVICE.dwWidth);
		float _h = float(RDEVICE.dwHeight);
		float o_w = (.5f / _w);
		float o_h = (.5f / _h);
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f
		};
#endif	//	USE_DX10

		mTexgen.mul(mTexelAdjust, RCache.xforms.m_wvp);

		RCache.set_c(C, mTexgen);
	}
};

static cl_texgen binder_texgen;

class cl_VPtexgen : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f
		};
#else	//	USE_DX10
		float _w = float(RDEVICE.dwWidth);
		float _h = float(RDEVICE.dwHeight);
		float o_w = (.5f / _w);
		float o_h = (.5f / _h);
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f
		};
#endif	//	USE_DX10

		mTexgen.mul(mTexelAdjust, RCache.xforms.m_vp);

		RCache.set_c(C, mTexgen);
	}
};

static cl_VPtexgen binder_VPtexgen;

// fog
#ifndef _EDITOR
class cl_fog_plane : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			// Plane
			Fvector4 plane;
			Fmatrix& M = Device.mFullTransform;
			plane.x = -(M._14 + M._13);
			plane.y = -(M._24 + M._23);
			plane.z = -(M._34 + M._33);
			plane.w = -(M._44 + M._43);
			float denom = -1.0f / _sqrt(_sqr(plane.x) + _sqr(plane.y) + _sqr(plane.z));
			plane.mul(denom);

			// Near/Far
			float A = g_pGamePersistent->Environment().CurrentEnv->fog_near;
			float B = 1 / (g_pGamePersistent->Environment().CurrentEnv->fog_far - A);
			result.set(-plane.x * B, -plane.y * B, -plane.z * B, 1 - (plane.w - A) * B); // view-plane
		}
		RCache.set_c(C, result);
	}
};

static cl_fog_plane binder_fog_plane;

// fog-params
class cl_fog_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			// Near/Far
			float n = g_pGamePersistent->Environment().CurrentEnv->fog_near;
			float f = g_pGamePersistent->Environment().CurrentEnv->fog_far;
			float r = 1 / (f - n);
			result.set(-n * r, n, f, r);
		}
		RCache.set_c(C, result);
	}
};

static cl_fog_params binder_fog_params;

// fog-color
class cl_fog_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.fog_color.x, desc.fog_color.y, desc.fog_color.z, desc.fog_density);
		}
		RCache.set_c(C, result);
	}
};

static cl_fog_color binder_fog_color;

static class cl_wind_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(E.wind_direction, E.wind_velocity, 0.0f, 0.0f);
		}
		RCache.set_c(C, result);
	}
} binder_wind_params;

#endif


// times
class cl_times : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		float t = RDEVICE.fTimeGlobal;
		RCache.set_c(C, t, t * 10, t / 10, _sin(t));
	}
};

static cl_times binder_times;

// game time (uses visual time override when weather editor is active)
class cl_game_times : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		float t = g_pGamePersistent->Environment().GetVisualTime();
		RCache.set_c(C, t, t / DAY_LENGTH, t / (DAY_LENGTH / 24), floor(t / (DAY_LENGTH / 24)));
	}
};

static cl_game_times binder_game_times;

// eye-params
class cl_eye_P : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraPosition;
		RCache.set_c(C, V.x, V.y, V.z, 1);
	}
};
static cl_eye_P binder_eye_P;

// interpolated eye position (crookr scope parallax)
// We can improve this by clamping the magnitude of the travel here instead of in-shader.
// it would fix the issue with the fog "sticking" when moving too far off center
extern float scope_fog_interp;
extern float scope_fog_travel;
class cl_eye_PL : public R_constant_setup
{
	Fvector tV;
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraPosition;
		tV = tV.lerp(tV, V, scope_fog_interp);

		RCache.set_c(C, tV.x, tV.y, tV.z, 1);
	}
};
static cl_eye_PL binder_eye_PL;

// eye-params
class cl_eye_D : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraDirection;
		RCache.set_c(C, V.x, V.y, V.z, 0);
	}
};
static cl_eye_D binder_eye_D;

// interpolated eye direction (crookr scope parallax)
class cl_eye_DL : public R_constant_setup
{
	Fvector tV;
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraDirection;
		tV = tV.lerp(tV, V, scope_fog_interp);

		RCache.set_c(C, tV.x, tV.y, tV.z, 0);
	}
};
static cl_eye_DL binder_eye_DL;

// eye-params
class cl_eye_N : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraTop;
		RCache.set_c(C, V.x, V.y, V.z, 0);
	}
};

static cl_eye_N binder_eye_N;


// fake scope params (crookr)
extern float scope_outerblur;
extern float scope_innerblur;
extern float scope_scrollpower;
extern float scope_brightness;
class cl_fakescope_params : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scope_scrollpower, scope_innerblur, scope_outerblur, scope_brightness);
	}
};
static cl_fakescope_params binder_fakescope_params;

extern float scope_ca;
extern float scope_fog_attack;
extern float scope_fog_mattack;
//extern float scope_fog_travel;
class cl_fakescope_ca : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scope_ca, scope_fog_attack, scope_fog_mattack, scope_fog_travel);
	}
};
static cl_fakescope_ca binder_fakescope_ca;

extern float scope_radius;
extern float scope_fog_radius;
extern float scope_fog_sharp;
//extern float scope_drift_amount;
class cl_fakescope_params3 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scope_radius, scope_fog_radius, scope_fog_sharp, 0.0f);
	}
};
static cl_fakescope_params3 binder_fakescope_params3;

// Mark Switch
extern int ps_markswitch_current;
extern int ps_markswitch_count;
extern Fvector4 ps_markswitch_color;

static class markswitch_current : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_markswitch_current, 0, 0, 0);
	}
}    markswitch_current;

static class markswitch_count : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_markswitch_count, 0, 0, 0);
	}
}    markswitch_count;

static class markswitch_color : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_markswitch_color.x, ps_markswitch_color.y, ps_markswitch_color.z, ps_markswitch_color.w);
	}
}    markswitch_color;

// Shader 3D Scopes
extern Fvector4 ps_s3ds_param_1;
extern Fvector4 ps_s3ds_param_2;
extern Fvector4 ps_s3ds_param_3;
extern Fvector4 ps_s3ds_param_4;

static class s3ds_param_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_s3ds_param_1.x, ps_s3ds_param_1.y, ps_s3ds_param_1.z, ps_s3ds_param_1.w);
	}
}    s3ds_param_1;

static class s3ds_param_2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_s3ds_param_2.x, ps_s3ds_param_2.y, ps_s3ds_param_2.z, ps_s3ds_param_2.w);
	}
}    s3ds_param_2;

static class s3ds_param_3 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_s3ds_param_3.x, ps_s3ds_param_3.y, ps_s3ds_param_3.z, ps_s3ds_param_3.w);
	}
}    s3ds_param_3;

static class s3ds_param_4 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_s3ds_param_4.x, ps_s3ds_param_4.y, ps_s3ds_param_4.z, ps_s3ds_param_4.w);
	}
}    s3ds_param_4;

//--DSR-- SilencerOverheat_start
static class cl_silencer_glowing : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.hemi.set_c_glowing(C);
	}
} binder_silencer_glowing;
//--DSR-- SilencerOverheat_end

//--DSR-- HeatVision_start
extern float heat_vision_mode;
extern Fvector4 heat_vision_steps;
extern Fvector4 heat_vision_blurring;
extern Fvector4 heat_vision_args_1;
extern Fvector4 heat_vision_args_2;

static class cl_heatvision_hotness : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.hemi.set_c_hotness(C);
	}
} binder_heatvision_hotness;

static class cl_heatvision_steps : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_r2_heatvision, heat_vision_steps.x, heat_vision_steps.y, heat_vision_steps.z);
	}
} binder_heatvision_params1;

static class cl_heatvision_blurring : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, heat_vision_blurring.x, heat_vision_blurring.y, heat_vision_blurring.z, heat_vision_mode);
	}
} binder_heatvision_params2;

static class cl_heatvision_args1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, heat_vision_args_1.x, heat_vision_args_1.y, heat_vision_args_1.z, heat_vision_args_1.w);
	}
} binder_heatvision_args1;

static class cl_heatvision_args2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, heat_vision_args_2.x, heat_vision_args_2.y, heat_vision_args_2.z, heat_vision_args_2.w);
	}
} binder_heatvision_args2;

//--DSR-- HeatVision_end


#ifndef _EDITOR
// D-Light0
class cl_sun0_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			#if RENDER==R_R1 //Lumscale control for R1
				result.set(desc.sun_color.x * ps_r2_sun_lumscale, desc.sun_color.y * ps_r2_sun_lumscale, desc.sun_color.z * ps_r2_sun_lumscale, 0);
			#else
				result.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z, 0);
			#endif
		}
		RCache.set_c(C, result);
	}
};

static cl_sun0_color binder_sun0_color;

class cl_sun0_dir_w : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.sun_dir.x, desc.sun_dir.y, desc.sun_dir.z, 0);
		}
		RCache.set_c(C, result);
	}
};

static cl_sun0_dir_w binder_sun0_dir_w;

class cl_sun0_dir_e : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			Fvector D;
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			Device.mView.transform_dir(D, desc.sun_dir);
			D.normalize();
			result.set(D.x, D.y, D.z, 0);
		}
		RCache.set_c(C, result);
	}
};

static cl_sun0_dir_e binder_sun0_dir_e;

//
class cl_amb_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptorMixer& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.ambient.x, desc.ambient.y, desc.ambient.z, desc.weight);
		}
		RCache.set_c(C, result);
	}
};

static cl_amb_color binder_amb_color;

class cl_hemi_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z, desc.hemi_color.w);
		}
		RCache.set_c(C, result);
	}
};

static cl_hemi_color binder_hemi_color;


// OWA: Expose lumscale console variables to shaders
// x = ps_r2_sun_lumscale (sun brightness)
// y = ps_r2_sun_lumscale_hemi (hemisphere/ambient brightness)
// z = ps_r2_sun_lumscale_amb (ambient brightness)
// w = reserved
class cl_lumscale : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, ps_r2_sun_lumscale, ps_r2_sun_lumscale_hemi, ps_r2_sun_lumscale_amb, 0.0f);
	}
};

static cl_lumscale binder_lumscale;

class cl_sky_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.sky_color.x, desc.sky_color.y, desc.sky_color.z, desc.sky_rotation);
		}
		RCache.set_c(C, result);
	}
};
static cl_sky_color binder_sky_color;

// OWA: Individual sky rotations for fog shader per-cubemap sampling
// x = rotation for sky_s0 (state A / "from" state)
// y = rotation for sky_s1 (state B / "to" state)
// z = lerp paused flag (1.0 = weather editor active, 0.0 = normal gameplay)
// w = reserved
class cl_sky_rotations : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C) override
	{
		if (marker != Device.dwFrame)
		{
			marker = Device.dwFrame;
			CEnvDescriptorMixer& desc = *g_pGamePersistent->Environment().CurrentEnv;
			bool lerp_paused = g_pGamePersistent->Environment().m_lerp_paused;
			// When lerp is paused (weather editor mode), use the single sky_rotation for both
			// The editor sets sky_rotation directly, so both cubemaps should use the same value
			// z component = 1.0 signals editor mode to the shader (skip brightness multiplier)
			if (lerp_paused)
			{
				result.set(desc.sky_rotation, desc.sky_rotation, 1.0f, 0.0f);
			}
			else
			{
				result.set(desc.sky_rotation_0, desc.sky_rotation_1, 0.0f, 0.0f);
			}
		}
		RCache.set_c(C, result);
	}
};
static cl_sky_rotations binder_sky_rotations;
#endif

static class cl_screen_res : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, (float)RDEVICE.dwWidth, (float)RDEVICE.dwHeight, 1.0f / (float)RDEVICE.dwWidth,
		             1.0f / (float)RDEVICE.dwHeight);
	}
} binder_screen_res;

static class cl_screen_params : public R_constant_setup
{
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float fov = float(Device.fFOV);
		float aspect = float(Device.fASPECT);
		result.set(fov, aspect, tan(deg2rad(fov) / 2), g_pGamePersistent->Environment().CurrentEnv->far_plane * 0.75f);
		RCache.set_c(C, result);
	}
};

static cl_screen_params binder_screen_params;

static class cl_hud_params : public R_constant_setup //--#SM+#--
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->hud_params);
	}
}	binder_hud_params;

static class cl_hud_fov_params : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->hud_fov_params);
	}
}	binder_hud_fov_params;

static class cl_script_params : public R_constant_setup //--#SM+#--
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->m_script_params);
	}
}	binder_script_params;

static class cl_blend_mode : public R_constant_setup //--#SM+#--
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->m_blender_mode);
	}
}	binder_blend_mode;

static class cl_rain_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float rainDensity = g_pGamePersistent->Environment().CurrentEnv->rain_density;
		float rainWetness = g_pGamePersistent->Environment().wetness_factor;

		RCache.set_c(C, rainDensity, rainWetness, 0.0f, 0.0f);
	}
} binder_rain_params;

static class cl_actor_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float actorHealth = g_pGamePersistent->actor_data.health;
		float actorStamina = g_pGamePersistent->actor_data.stamina;
		float actorBleeding = g_pGamePersistent->actor_data.bleeding;
		int actorHelmet = g_pGamePersistent->actor_data.helmet;

		RCache.set_c(C, actorHealth, actorStamina, actorBleeding, actorHelmet);
	}
} binder_actor_data;

static class cl_inv_v : public R_constant_setup
{
	u32	marker;

	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, Device.mInvView);
	}
} binder_inv_v;

// OWA: pp_image_corrections and pp_color_grading removed - img_corrections() never called in R4

static class cl_pda_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float pda_factor = g_pGamePersistent->pda_shader_data.pda_display_factor;
		float pda_psy_factor = g_pGamePersistent->pda_shader_data.pda_psy_influence;
		float pda_display_brightness = g_pGamePersistent->pda_shader_data.pda_displaybrightness;
		RCache.set_c(C, pda_factor, pda_psy_factor, pda_display_brightness, 0.0f);
	}

} binder_pda_params;

static class cl_near_far_plane : public R_constant_setup
{
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float nearPlane = float(VIEWPORT_NEAR);
		float farPlane = g_pGamePersistent->Environment().CurrentEnv->far_plane;
		result.set(nearPlane, farPlane, 0, 0);
		RCache.set_c(C, result);
	}
} binder_near_far_plane;

// Screen Space Shaders Stuff
extern Fvector4 ps_ssfx_floravariation;
extern Fvector4 ps_ssfx_fog;

extern Fvector4 ps_ssfx_pom;
extern Fvector4 ps_ssfx_terrain_pom;
extern u32 ps_r3_terrain_quality;

extern Fvector4 ps_ssfx_il_setup1;

extern float ps_ssfx_hud_hemi;

// OWA retro shader constants
extern float ps_r__tf_contrast;
extern float ps_r2_auto_fog;
extern int   ps_r3_fog_temporal;
// OWA Multi-Scale Bloom parameters
extern float ps_r2_bloom_threshold;
extern float ps_r2_bloom_intensity;
extern float ps_r2_bloom_radius;
extern Fvector4 ps_ssfx_il;
extern Fvector4 ps_ssfx_il_setup1;
extern Fvector4 ps_ssfx_water;
extern Fvector4 ps_ssfx_water_setup1;
extern Fvector4 ps_ssfx_water_setup2;

extern Fvector4 ps_ssfx_volumetric;
extern Fvector4 ps_ssfx_terrain_offset;

extern Fvector3 ps_ssfx_shadow_bias;
extern int ps_r3_ssfx_shadows;
extern int ps_r3_ssfx_fog;
extern int ps_r3_ssfx_water;
extern int ps_r3_ssfx_taa;
extern int ps_r3_ssfx_il;
extern Fvector4 ps_ssfx_wind_grass;
extern Fvector4 ps_ssfx_wind_trees;

extern Fvector4 ps_ssfx_florafixes_1;
extern Fvector4 ps_ssfx_florafixes_2;

extern int ps_ssfx_is_underground;
extern Fvector4 ps_ssfx_lightsetup_1;
extern Fvector4 ps_ssfx_hud_drops_1;
extern Fvector4 ps_ssfx_hud_drops_2;
extern Fvector4 ps_ssfx_blood_decals;

//Sneaky debug stuff
extern Fvector4 ps_dev_param_1;
extern Fvector4 ps_dev_param_2;
extern Fvector4 ps_dev_param_3;
extern Fvector4 ps_dev_param_4;
extern Fvector4 ps_dev_param_5;
extern Fvector4 ps_dev_param_6;
extern Fvector4 ps_dev_param_7;
extern Fvector4 ps_dev_param_8;

static class dev_param_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_1.x, ps_dev_param_1.y, ps_dev_param_1.z, ps_dev_param_1.w);
	}
}    dev_param_1;

static class dev_param_2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_2.x, ps_dev_param_2.y, ps_dev_param_2.z, ps_dev_param_2.w);
	}
}    dev_param_2;

static class dev_param_3 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_3.x, ps_dev_param_3.y, ps_dev_param_3.z, ps_dev_param_3.w);
	}
}    dev_param_3;

static class dev_param_4 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_4.x, ps_dev_param_4.y, ps_dev_param_4.z, ps_dev_param_4.w);
	}
}    dev_param_4;

static class dev_param_5 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_5.x, ps_dev_param_5.y, ps_dev_param_5.z, ps_dev_param_5.w);
	}
}    dev_param_5;

static class dev_param_6 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_6.x, ps_dev_param_6.y, ps_dev_param_6.z, ps_dev_param_6.w);
	}
}    dev_param_6;

static class dev_param_7 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_7.x, ps_dev_param_7.y, ps_dev_param_7.z, ps_dev_param_7.w);
	}
}    dev_param_7;

static class dev_param_8 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_8.x, ps_dev_param_8.y, ps_dev_param_8.z, ps_dev_param_8.w);
	}
}    dev_param_8;

static class ssfx_blood_decals : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_blood_decals);
	}
}    ssfx_blood_decals;

static class ssfx_hud_drops_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_hud_drops_1);
	}
}    ssfx_hud_drops_1;

static class ssfx_hud_drops_2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_hud_drops_2);
	}
}    ssfx_hud_drops_2;

static class ssfx_lightsetup_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_lightsetup_1);
	}
}    ssfx_lightsetup_1;

static class ssfx_is_underground : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_is_underground, 0, 0, 0);
	}
}    ssfx_is_underground;

static class ssfx_florafixes_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_florafixes_1);
	}
}    ssfx_florafixes_1;

static class ssfx_florafixes_2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_florafixes_2);
	}
}    ssfx_florafixes_2;

static class ssfx_wind_grass : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wind_grass);
	}
}    ssfx_wind_grass;

static class ssfx_wind_trees : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wind_trees);
	}
}    ssfx_wind_trees;

static class ssfx_wind_anim : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->Environment().wind_anim);
	}
}    ssfx_wind_anim;

static class ssfx_wind_anim_prev : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, Device.wind_anim_prev);
	}
}    ssfx_wind_anim_prev;

static class ssfx_shadow_bias : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_shadow_bias.x, ps_ssfx_shadow_bias.y, 0, 0);
	}
}    ssfx_shadow_bias;

static class ssfx_terrain_offset : public R_constant_setup
{
	virtual void setup(R_constant * C)
	{
		RCache.set_c(C, ps_ssfx_terrain_offset);
	}
}    ssfx_terrain_offset;

static class ssfx_volumetric : public R_constant_setup
{
	virtual void setup(R_constant * C)
	{
		RCache.set_c(C, ps_ssfx_volumetric);
	}
}    ssfx_volumetric;

static class ssfx_water : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_water);
	}
}    ssfx_water;

static class ssfx_water_setup1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_water_setup1);
	}
}    ssfx_water_setup1;

static class ssfx_water_setup2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_water_setup2);
	}
}    ssfx_water_setup2;

static class ssfx_il : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_il);
	}
}    ssfx_il;

static class ssfx_il_setup1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_il_setup1);
	}
}    ssfx_il_setup1;

// OWA: Perceptual Lighting (controlled by r3_gi command)
extern Fvector4 ps_r3_gi_pl_params;
extern Fvector4 ps_r3_gi_pl_params2;

static class pl_params : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		// OWA: Just pass PL params - enable/disable is handled by
		// compile-time #ifdef SSFX_INDIRECT_LIGHT in shaders (set via r3_gi)
		RCache.set_c(C, ps_r3_gi_pl_params);
	}
}    pl_params;

static class pl_params2 : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, ps_r3_gi_pl_params2);
	}
}    pl_params2;

static class ssfx_hud_hemi : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, ps_ssfx_hud_hemi, 0, 0, 0);
	}
}    ssfx_hud_hemi;

static class ssfx_issvp : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, Device.m_SecondViewport.IsSVPFrame(), 0, 0, 0);
	}
}    ssfx_issvp;

static class ssfx_terrain_pom : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, ps_ssfx_terrain_pom);
	}
}    ssfx_terrain_pom;

static class r3_terrain_quality : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, (float)ps_r3_terrain_quality, 0, 0, 0);
	}
}    r3_terrain_quality;

static class ssfx_pom : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, ps_ssfx_pom);
	}
}    ssfx_pom;

static class ssfx_jitter : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		float JitterX = 0;
		float JitterY = 0;

#if defined(USE_DX11)
		// OWA: o.ssfx_taa now includes r3_ssfx_taa check (compile-time)
		if (ps_ssfx_taa.x > 0 && RImplementation.o.ssfx_taa)
		{
			static Fvector2 TAA_Offset[4] = 
			{
				{  0.0f, -1.0f },
				{ -1.0f,  0.0f },
				{  1.0f,  0.0f },
				{  0.0f,  1.0f }
			};

			JitterX = TAA_Offset[ Device.dwFrame % 4 ].x / Device.dwWidth;
			JitterY = TAA_Offset[ Device.dwFrame % 4 ].y / Device.dwHeight;
		}
#endif

		RCache.set_c(C, JitterX * ps_ssfx_taa.y, JitterY * ps_ssfx_taa.y, ps_ssfx_taa.x, ps_ssfx_taa.w);

	}
}    ssfx_jitter;

static class ssfx_fTimeDelta : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, Device.fTimeDelta, 0, 0, 0);
	}
}    ssfx_fTimeDelta;

static class ssfx_fog : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_fog);
	}
}    ssfx_fog;

static class ssfx_floravariation : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_floravariation);
	}
}    ssfx_floravariation;

/* --- HDR10 parameters --- */
extern float ps_r4_hdr10_whitepoint_nits;
extern float ps_r4_hdr10_ui_nits;
extern float ps_r4_hdr10_pda_intensity;
extern int   ps_r4_hdr10_pda;
extern int   ps_r4_hdr10_on;

extern int   ps_r4_hdr10_colorspace;
// ps_r4_hdr10_tonemap_mode removed - HDR now always uses hybrid luminance/maxRGB tonemapping
extern float ps_r4_hdr10_chroma_correction;

// Color grading parameters (SDR + HDR) - renamed from ps_r4_hdr10_* for clarity
extern float ps_r4_cg_exposure;
extern float ps_r4_cg_contrast;
extern float ps_r4_cg_contrast_middle_gray;
extern float ps_r4_cg_saturation;
extern float ps_r4_cg_brightness;
extern float ps_r4_cg_gamma;

extern float ps_r4_hdr10_ui_saturation;

// OWA: HDR10 bloom and lens flare removed - unified multi-scale bloom handles both SDR and HDR

extern int   ps_r4_hdr10_sun_on;
extern float ps_r4_hdr10_sun_intensity;
extern float ps_r4_hdr10_moon_intensity;
extern float ps_r4_hdr10_sun_dawn_begin;
extern float ps_r4_hdr10_sun_dawn_end;
extern float ps_r4_hdr10_sun_dusk_begin;
extern float ps_r4_hdr10_sun_dusk_end;

#define DECL_BINDER4F(name, x, y, z, w) \
	static class cl_##name : public R_constant_setup \
	{ \
		virtual void setup(R_constant* C) \
		{ \
			RCache.set_c( \
				C, \
				(float)(x), (float)(y), (float)(z), (float)(w) \
			); \
		} \
	} name

#if RENDER == R_R4
#define HDR10_ON (RImplementation.o.dx11_hdr10)
#else
#define HDR10_ON (0)
#endif

DECL_BINDER4F( binder_hdr10_parameters1,
	ps_r4_hdr10_whitepoint_nits,
	ps_r4_hdr10_ui_nits / ps_r4_hdr10_whitepoint_nits,
	HDR10_ON,
	ps_r4_hdr10_pda
);

DECL_BINDER4F( binder_hdr10_parameters2,
	ps_r4_hdr10_colorspace,
	ps_r4_hdr10_pda_intensity,
	ps_r4_hdr10_chroma_correction,  // Chroma correction scaling for EETF
	0.0f  // Was tonemap_mode - now unused, HDR always uses hybrid tonemapping
);

// Color grading binder (SDR + HDR) - renamed from binder_hdr10_parameters3 for clarity
DECL_BINDER4F( binder_cg_parameters1,
	ps_r4_cg_exposure,
	ps_r4_cg_contrast + 1.0f,
	ps_r4_cg_saturation + 1.0f,
	ps_r4_cg_contrast_middle_gray
);

// OWA: HDR10 bloom and lens flare removed - unified multi-scale bloom handles both SDR and HDR
// binder_hdr10_parameters4-10 now use placeholder values for removed bloom/flare parameters
DECL_BINDER4F( binder_hdr10_parameters4,
	0.0f,  // Was bloom_on (removed)
	0.0f,  // Was bloom_blur_scale (removed)
	0.0f,  // Was bloom_intensity (removed)
	ps_r4_hdr10_sun_intensity
);

DECL_BINDER4F( binder_hdr10_parameters5,
	ps_r4_hdr10_sun_dawn_begin,
	ps_r4_hdr10_sun_dawn_end,
	ps_r4_hdr10_sun_dusk_begin,
	ps_r4_hdr10_sun_dusk_end
);

// Color grading binder (SDR + HDR) - renamed from binder_hdr10_parameters6 for clarity
DECL_BINDER4F( binder_cg_parameters2,
	ps_r4_cg_brightness,
	1.0f / ps_r4_cg_gamma,
	0.0f,  // Was flare_threshold (removed)
	0.0f   // Was flare_power (removed)
);

// OWA: Moon phase and procedural sun/moon data (replaces removed flare ghost/halo params)
extern float ps_r4_procedural_sun_moon;
static class cl_hdr10_parameters7 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		float moon_phase = g_pGamePersistent->moon_shader_data.phase;
		float moon_day_frac = g_pGamePersistent->moon_shader_data.game_day_frac;
		RCache.set_c(C, moon_phase, moon_day_frac, ps_r4_procedural_sun_moon, 0.0f);
	}
} binder_hdr10_parameters7;

DECL_BINDER4F( binder_hdr10_parameters8,
	0.0f,  // Was flare_halo_ca (removed)
	0.0f,  // Was flare_ghost_ca (removed)
	0.0f,  // Was flare_blur_scale (removed)
	ps_r4_hdr10_ui_saturation + 1.0f
);

// OWA: Procedural sun flare params + moon intensity (replaces removed lens flare params)
extern float ps_r4_sun_flare_intensity;
extern float ps_r4_sun_flare_rays;
DECL_BINDER4F( binder_hdr10_parameters9,
	ps_r4_sun_flare_intensity,   // Star-burst flare strength [0, 2]
	ps_r4_sun_flare_rays,        // Number of radial flare rays [4, 12]
	ps_r4_hdr10_moon_intensity,  // Moon HDR glow multiplier (unchanged)
	0.0f                         // Reserved
);

DECL_BINDER4F( binder_hdr10_parameters10,
	0.0f,  // Was flare_lens_color.x (removed)
	0.0f,  // Was flare_lens_color.y (removed)
	0.0f,  // Was flare_lens_color.z (removed)
	ps_r4_hdr10_sun_on
);



// OWA: HDR expansion tuning parameters (knee is now automatic per BT.2408)
extern float ps_r4_hdr10_light_expansion;
extern float ps_r4_hdr10_particle_expansion;
DECL_BINDER4F( binder_hdr10_parameters11,
	0.0f,  // Unused (was eetf_knee, now automatic per BT.2408)
	ps_r4_hdr10_light_expansion,
	ps_r4_hdr10_particle_expansion,
	0.0f   // Unused (was eetf_knee_softness, now automatic)
);
/* --- HDR10 Parameters --- */

extern Fvector4 ps_vignette_control;
static class vignette_control : public R_constant_setup
{
	virtual void setup(R_constant *C)
	{
		RCache.set_c(C, ps_vignette_control.x, ps_vignette_control.y, ps_vignette_control.z, ps_vignette_control.w);
	}
} vignette_control;

// OWA retro shader constant setters

static class cl_owa_tex_contrast : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, ps_r__tf_contrast, 0, 0, 0);
	}
} binder_owa_tex_contrast;

static class cl_owa_auto_fog : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		// OWA: r2_auto_fog > 0 acts as developer override for fog_auto_blend
		// This lets you test auto fog color on the stratified path without editing weather files
		// r2_auto_fog 0 (default) = use weather value, r2_auto_fog 0.5/1.0 = override
		float fog_auto_blend;
		if (ps_r2_auto_fog > 0.f)
			fog_auto_blend = ps_r2_auto_fog;
		else
			fog_auto_blend = g_pGamePersistent->Environment().CurrentEnv->m_fFogAutoBlend;

		RCache.set_c(C, fog_auto_blend, 0, 0, 0);
	}
} binder_owa_auto_fog;

// OWA Multi-Scale Bloom parameters: x=threshold, y=intensity, z=radius, w=reserved
static class cl_owa_bloom_params : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, ps_r2_bloom_threshold, ps_r2_bloom_intensity, ps_r2_bloom_radius, 0);
	}
} binder_owa_bloom_params;

// OWA: Static lighting brightness multiplier (for R1-style lightmap rendering)
// Default 2.0 matches OLR legacy value; tune for HDR pipeline integration
extern float ps_r4_static_brightness;
static class cl_static_brightness : public R_constant_setup
{
	virtual void setup(R_constant* C) override
	{
		RCache.set_c(C, ps_r4_static_brightness, 0, 0, 0);
	}
} binder_static_brightness;

// Standart constant-binding
void CBlender_Compile::SetMapping()
{
	// matrices
	r_Constant("m_W", &binder_w);
	r_Constant("m_invW", &binder_invw);
	r_Constant("m_V", &binder_v);
	r_Constant("m_P", &binder_p);
	r_Constant("m_WV", &binder_wv);
	r_Constant("m_VP", &binder_vp);
	r_Constant("m_WVP", &binder_wvp);
	r_Constant("m_inv_V", &binder_inv_v);

	r_Constant("m_v_prev", &binder_v_prev);
	r_Constant("m_p_prev", &binder_p_prev);
	r_Constant("m_wv_prev", &binder_wv_prev);
	r_Constant("m_vp_prev", &binder_vp_prev);
	r_Constant("m_wvp_prev", &binder_wvp_prev);

	r_Constant("m_xform_v", &tree_binder_m_xform_v);
	r_Constant("m_xform", &tree_binder_m_xform);
	r_Constant("consts", &tree_binder_consts);
	r_Constant("wave", &tree_binder_wave);
	r_Constant("wind", &tree_binder_wind);
	r_Constant("c_scale", &tree_binder_c_scale);
	r_Constant("c_bias", &tree_binder_c_bias);
	r_Constant("c_sun", &tree_binder_c_sun);

	//hemi cube
	r_Constant("L_material", &binder_material);
	r_Constant("hemi_cube_pos_faces", &binder_hemi_cube_pos_faces);
	r_Constant("hemi_cube_neg_faces", &binder_hemi_cube_neg_faces);

	//	Igor	temp solution for the texgen functionality in the shader
	r_Constant("m_texgen", &binder_texgen);
	r_Constant("mVPTexgen", &binder_VPtexgen);

#ifndef _EDITOR
	// fog-params
	r_Constant("fog_plane", &binder_fog_plane);
	r_Constant("fog_params", &binder_fog_params);
	r_Constant("fog_color", &binder_fog_color);
	r_Constant("wind_params", &binder_wind_params);
#endif
	// time
	r_Constant("timers", &binder_times);
	r_Constant("timers_game", &binder_game_times);

	// eye-params
	r_Constant("eye_position", &binder_eye_P);
	r_Constant("eye_position_lerp", &binder_eye_PL); // crookr
	r_Constant("eye_direction", &binder_eye_D);
	r_Constant("eye_direction_lerp", &binder_eye_DL); // crookr
	r_Constant("eye_normal", &binder_eye_N);

#ifndef _EDITOR
	// global-lighting (env params)
	r_Constant("L_sun_color", &binder_sun0_color);
	r_Constant("L_sun_dir_w", &binder_sun0_dir_w);
	r_Constant("L_sun_dir_e", &binder_sun0_dir_e);
	//	r_Constant				("L_lmap_color",	&binder_lm_color);
	r_Constant("L_lumscale", &binder_lumscale);  // OWA: x=sun, y=hemi, z=amb
	r_Constant("L_hemi_color", &binder_hemi_color);
	r_Constant("L_ambient", &binder_amb_color);
#endif

	r_Constant("screen_res", &binder_screen_res);
	r_Constant("ogse_c_screen", &binder_screen_params);
	r_Constant("near_far_plane", &binder_near_far_plane);

	// OWA retro shader constants - globally available to all shaders
	r_Constant("bloom_params", &binder_owa_bloom_params);  // OWA Multi-Scale Bloom: x=threshold, y=intensity, z=radius
	r_Constant("tex_contrast", &binder_owa_tex_contrast);
	r_Constant("auto_fog", &binder_owa_auto_fog);
	r_Constant("L_static_brightness", &binder_static_brightness);  // OWA: Static lighting brightness (R1-style)
	// misc
	r_Constant("m_hud_params", &binder_hud_params);	//--#SM+#--
	r_Constant("m_hud_fov_params", &binder_hud_fov_params);
	r_Constant("m_script_params", &binder_script_params); //--#SM+#--
	r_Constant("m_blender_mode", &binder_blend_mode);	//--#SM+#--
	
	// Rain
	r_Constant("rain_params", &binder_rain_params);
	//Actor data
	r_Constant("actor_data", &binder_actor_data);
	// OWA: pp_img_corrections and pp_img_cg removed - img_corrections() never called in R4

	// detail
	//if (bDetail	&& detail_scaler)
	//	Igor: bDetail can be overridden by no_detail_texture option.
	//	But shader can be deatiled implicitly, so try to set this parameter
	//	anyway.
	if (detail_scaler)
		r_Constant("dt_params", detail_scaler);

	// PDA
	r_Constant("pda_params", &binder_pda_params);

	// Screen Space Shaders	
	r_Constant("ssfx_floravariation", &ssfx_floravariation);
	r_Constant("ssfx_fog", &ssfx_fog);
	r_Constant("ssfx_timedelta", &ssfx_fTimeDelta);
	r_Constant("ssfx_jitter", &ssfx_jitter);
	r_Constant("ssfx_pom", &ssfx_pom);

	r_Constant("ssfx_terrain_pom", &ssfx_terrain_pom);
	r_Constant("r3_terrain_quality", &r3_terrain_quality);

	r_Constant("ssfx_issvp", &ssfx_issvp);
	r_Constant("ssfx_hud_hemi", &ssfx_hud_hemi);
	r_Constant("ssfx_il_setup", &ssfx_il);
	r_Constant("ssfx_il_setup2", &ssfx_il_setup1);
	r_Constant("pl_params", &pl_params);
	r_Constant("pl_params2", &pl_params2);
	r_Constant("ssfx_water", &ssfx_water);
	r_Constant("ssfx_water_setup1", &ssfx_water_setup1);
	r_Constant("ssfx_water_setup2", &ssfx_water_setup2);

	r_Constant("ssfx_volumetric", &ssfx_volumetric);
	r_Constant("ssfx_terrain_offset", &ssfx_terrain_offset);
	r_Constant("ssfx_shadow_bias", &ssfx_shadow_bias);
	r_Constant("ssfx_wind_anim", &ssfx_wind_anim);
	r_Constant("ssfx_wind_anim_prev", &ssfx_wind_anim_prev);
	r_Constant("sky_color", &binder_sky_color);
	r_Constant("sky_rotations", &binder_sky_rotations);  // OWA: Per-state sky rotations for fog sampling
	r_Constant("ssfx_blood_decals", &ssfx_blood_decals);
	r_Constant("ssfx_hud_drops_1", &ssfx_hud_drops_1);
	r_Constant("ssfx_hud_drops_2", &ssfx_hud_drops_2);
	r_Constant("ssfx_lightsetup_1", &ssfx_lightsetup_1);
	r_Constant("ssfx_is_underground", &ssfx_is_underground);
	r_Constant("ssfx_florafixes_1", &ssfx_florafixes_1);
	r_Constant("ssfx_florafixes_2", &ssfx_florafixes_2);
	r_Constant("ssfx_wsetup_grass", &ssfx_wind_grass);
	r_Constant("ssfx_wsetup_trees", &ssfx_wind_trees);

	// Shader stuff
	r_Constant("shader_param_1", &dev_param_1);
	r_Constant("shader_param_2", &dev_param_2);
	r_Constant("shader_param_3", &dev_param_3);
	r_Constant("shader_param_4", &dev_param_4);
	r_Constant("shader_param_5", &dev_param_5);
	r_Constant("shader_param_6", &dev_param_6);
	r_Constant("shader_param_7", &dev_param_7);
	r_Constant("shader_param_8", &dev_param_8);
	
	// Mark Switch
	r_Constant("markswitch_current", &markswitch_current);
	r_Constant("markswitch_count", &markswitch_count);
	r_Constant("markswitch_color", &markswitch_color);

	// Shader 3D Scopes
	r_Constant("s3ds_param_1", &s3ds_param_1);
	r_Constant("s3ds_param_2", &s3ds_param_2);
	r_Constant("s3ds_param_3", &s3ds_param_3);
	r_Constant("s3ds_param_4", &s3ds_param_4);

	// crookr
	r_Constant("fakescope_params1", &binder_fakescope_params);
	r_Constant("fakescope_params2", &binder_fakescope_ca);
	r_Constant("fakescope_params3", &binder_fakescope_params3);

	// other common
	for (u32 it = 0; it < DEV->v_constant_setup.size(); it++)
	{
		std::pair<shared_str, R_constant_setup*> cs = DEV->v_constant_setup[it];
		r_Constant(*cs.first, cs.second);
	}


	r_Constant("L_glowing", &binder_silencer_glowing);		//--DSR-- SilencerOverheat
	//--DSR-- HeatVision_start
	r_Constant("L_hotness", &binder_heatvision_hotness);
	r_Constant("heatvision_params1", &binder_heatvision_params1);
	r_Constant("heatvision_params2", &binder_heatvision_params2);
	r_Constant("heatvision_params3", &binder_heatvision_args1);
	r_Constant("heatvision_params4", &binder_heatvision_args2);
	//--DSR-- HeatVision_end

	// HDR10 parameters (HDR-only settings)
    r_Constant("hdr10_parameters1",  &binder_hdr10_parameters1);
    r_Constant("hdr10_parameters2",  &binder_hdr10_parameters2);
	r_Constant("hdr10_parameters4",  &binder_hdr10_parameters4);
	r_Constant("hdr10_parameters5",  &binder_hdr10_parameters5);
	r_Constant("hdr10_parameters7",  &binder_hdr10_parameters7);
	r_Constant("hdr10_parameters8",  &binder_hdr10_parameters8);
	r_Constant("hdr10_parameters9",  &binder_hdr10_parameters9);
	r_Constant("hdr10_parameters10", &binder_hdr10_parameters10);
	r_Constant("hdr10_parameters11", &binder_hdr10_parameters11);

	// Color grading parameters (SDR + HDR)
    r_Constant("cg_parameters1",     &binder_cg_parameters1);
	r_Constant("cg_parameters2",     &binder_cg_parameters2);

	r_Constant("vignette_control", &vignette_control);
}
