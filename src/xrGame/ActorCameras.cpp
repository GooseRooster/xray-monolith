#include "StdAfx.h"
#include "Actor.h"
#include "../xrEngine/CameraBase.h"
#ifdef DEBUG
#include "PHDebug.h"
#endif
#include "Hit.h"
#include "PHDestroyable.h"
#include "Car.h"

#include "Weapon.h"
#include "Inventory.h"

//#include "SleepEffector.h"
#include "ActorEffector.h"
#include "Level.h"
#include "../xrEngine/cl_intersect.h"

//#include "elevatorstate.h"
#include "CharacterPhysicsSupport.h"
#include "EffectorShot.h"

#include "PHMovementControl.h"
#include "../xrPhysics/IElevatorState.h"
#include "../xrPhysics/ActorCameraCollision.h"
#include "IKLimbsController.h"
#include "GamePersistent.h"
#include "player_hud.h"
#include "Missile.h"

#include "EffectorBobbing.h"
class CFPCamEffector;

ENGINE_API extern float psHUD_FOV;
ENGINE_API extern float psHUD_FOV_def;
BOOL g_freelook_while_reloading = 1;

void CActor::cam_Set(EActorCameras style)
{
	CCameraBase* old_cam = cam_Active();
	cam_active = style;
	old_cam->OnDeactivate();
	cam_Active()->OnActivate(old_cam);
}

float CActor::f_Ladder_cam_limit = 1.f;
float f_Freelook_cam_limit = PI_DIV_2; // 1.45f;
float f_Freelook_cam_limit_p = .75f;

void CActor::cam_SetLadder()
{
	CCameraBase* C = cameras[eacFirstEye];
	g_LadderOrient();
	float yaw = (-XFORM().k.getH());
	float& cam_yaw = C->yaw;
	float delta_yaw = angle_difference_signed(yaw, cam_yaw);

	if (-f_Ladder_cam_limit < delta_yaw && f_Ladder_cam_limit > delta_yaw)
	{
		yaw = cam_yaw + delta_yaw;
		float lo = (yaw - f_Ladder_cam_limit);
		float hi = (yaw + f_Ladder_cam_limit);
		C->lim_yaw.set(lo, hi);
		C->bClampYaw = true;
	}
}

void CActor::camUpdateLadder(float dt)
{
	if (!character_physics_support()->movement()->ElevatorState())
		return;

	CCameraBase* C = cameras[eacFirstEye];

	if (C->bClampYaw) return;
	float yaw = (-XFORM().k.getH());

	float& cam_yaw = C->yaw;
	float delta = angle_difference_signed(yaw, cam_yaw);

	if (-0.05f < delta && 0.05f > delta)
	{
		yaw = cam_yaw + delta;
		float lo = (yaw - f_Ladder_cam_limit);
		float hi = (yaw + f_Ladder_cam_limit);
		C->lim_yaw.set(lo, hi);
		C->bClampYaw = true;
	}
	else
	{
		cam_yaw += delta * _min(dt * 10.f, 1.f);
	}

	IElevatorState* es = character_physics_support()->movement()->ElevatorState();
	if (es && es->State() == clbClimbingDown)
	{
		float& cam_pitch = C->pitch;
		const float ldown_pitch = C->lim_pitch.y;
		float delta = angle_difference_signed(ldown_pitch, cam_pitch);
		if (delta > 0.f)
			cam_pitch += delta * _min(dt * 10.f, 1.f);
	}
}

void CActor::cam_UnsetLadder()
{
	CCameraBase* C = cameras[eacFirstEye];
	C->lim_yaw.set(0, 0);
	C->bClampYaw = false;
}

void CActor::cam_SetFreelook()
{
	cam_freelook = eflEnabling;
}

void CActor::camUpdateFreelook(float dt)
{
	CCameraBase* C = cameras[eacFirstEye];

	switch (cam_freelook)
	{
	case eflEnabled:
	case eflDisabled:
	{
		return;
	}
	break;
	case eflEnabling:
	{
		if (!C->bClampYaw)
		{
			float& cam_yaw = C->yaw;
			old_torso_yaw = -r_torso.yaw;
			float lo = (cam_yaw - f_Freelook_cam_limit);
			float hi = (cam_yaw + f_Freelook_cam_limit);
			C->lim_yaw.set(lo, hi);
			C->bClampYaw = true;
		}

		if (C->lim_pitch.similar({ -1.5, 1.5 }))
		{
			float& cam_pitch = C->pitch;

			// Fix camera jump if freelook key is pressed right after loading a save.
			if (abs(cam_pitch) > 1.5f)
			{
				while (cam_pitch < C->lim_pitch[0])
					cam_pitch += PI_MUL_2;
				while (cam_pitch > C->lim_pitch[1])
					cam_pitch -= PI_MUL_2;
			}

			if (cam_pitch < -f_Freelook_cam_limit_p)
			{
				float diff_p = angle_difference(cam_pitch, -f_Freelook_cam_limit_p + .05f);
				if (diff_p < .025)
					cam_pitch = -f_Freelook_cam_limit_p + .005f;
				else
					cam_pitch += diff_p * _min(dt * 10.f, .5f);
				clamp(cam_pitch, C->lim_pitch.x, -f_Freelook_cam_limit_p);
			}
			else if (cam_pitch > f_Freelook_cam_limit_p)
			{
				float diff_p = angle_difference(cam_pitch, f_Freelook_cam_limit_p - .05f);
				if (diff_p < .025)
					cam_pitch = f_Freelook_cam_limit_p - .005f;
				else
					cam_pitch -= diff_p * _min(dt * 10.f, .5f);
				clamp(cam_pitch, f_Freelook_cam_limit_p, C->lim_pitch.y);
			}
			else
			{
				C->lim_pitch.set(-f_Freelook_cam_limit_p, f_Freelook_cam_limit_p);
				cam_freelook = eflEnabled;
			}
		}
	}
	break;
	case eflDisabling:
	{

		if (C->bClampYaw)
		{
			float& cam_yaw = C->yaw;
			float delta = angle_difference_signed(old_torso_yaw, cam_yaw);

			if (abs(delta) < 0.05f)
			{
				C->lim_yaw.set(0, 0);
				C->bClampYaw = false;
			}
			else
			{
				cam_yaw += delta * _min(dt * 10.f, 1.f);
			}
		}

		if (!C->lim_pitch.similar({ -1.5, 1.5 }))
		{
			float& cam_pitch = C->pitch;
			float delta = angle_difference_signed(0.f, cam_pitch);

			if (abs(delta) < 0.05f)
			{
				C->lim_pitch = { -1.5, 1.5 };
			}
			else
			{
				cam_pitch += delta * _min(dt * 10.f, 1.f);
			}
		}

		if (!C->bClampYaw && C->lim_pitch.similar({ -1.5, 1.5 }))
		{
			cam_freelook = eflDisabled;
		}
	}
	break;
	}
}

void CActor::cam_UnsetFreelook()
{
	cam_freelook = eflDisabling;
}

bool CActor::CanUseFreelook()
{
	if (cam_active != eacFirstEye)
		return false;

	if (g_player_hud->script_anim_part == 2)
		return false;

	Estate state = character_physics_support()->movement()->ElevatorState()->State();
	if ((state != clbNoLadder && state != clbNone && state != clbNoState) || m_holder)
		return false;

	if (inventory().ActiveItem())
	{
		CWeapon* wep = inventory().ActiveItem()->cast_weapon();
		CMissile* msl = inventory().ActiveItem()->cast_missile();
		if (msl && msl->GetState() > CHudItem::eLastBaseState)
			return false;
		else if (wep)
		{
			if (wep->IsZoomed())
				return false;

			u32 state = wep->GetState();

			switch (state)
			{
				case CWeapon::eFire:
				case CWeapon::eFire2:
				case CWeapon::eSwitch:
				case CWeapon::eSwitchMode: return false;
				case CWeapon::eReload: return g_freelook_while_reloading;
				default: return true; // If it's none of the states mentioned above it would return true on line 278 anyways.
			}
		}
	}

	return true;
}

float cammera_into_collision_shift = 0.05f;

float CActor::CameraHeight()
{
	Fvector R;
	character_physics_support()->movement()->Box().getsize(R);
	return m_fCamHeightFactor * (R.y - cammera_into_collision_shift);
}

IC float viewport_near(float& w, float& h)
{
	w = 2.f * VIEWPORT_NEAR * tan(deg2rad(Device.fFOV) / 2.f);
	h = w * Device.fASPECT;
	float c = _sqrt(w * w + h * h);
	return _max(_max(VIEWPORT_NEAR, _max(w, h)), c);
}

ICF void calc_point(Fvector& pt, float radius, float depth, float alpha)
{
	pt.x = radius * _sin(alpha);
	pt.y = radius + radius * _cos(alpha);
	pt.z = depth;
}

ICF void calc_gl_point(Fvector& pt, const Fmatrix& xform, float radius, float angle)
{
	calc_point(pt, radius,VIEWPORT_NEAR / 2, angle);
	xform.transform_tiny(pt);
}

ICF BOOL test_point(const Fvector& pt, xrXRC& xrc, const Fmatrix33& mat, const Fvector& ext)
{
	CDB::RESULT* it = xrc.r_begin();
	CDB::RESULT* end = xrc.r_end();
	for (; it != end; it++)
	{
		CDB::RESULT& O = *it;
		if (GMLib.GetMaterialByIdx(O.material)->Flags.is(SGameMtl::flPassable))
			continue;
		if (CDB::TestBBoxTri(mat, pt, ext, O.verts,FALSE))
			return TRUE;
	}
	return FALSE;
}


IC bool test_point(const Fvector& pt, const Fmatrix33& mat, const Fvector& ext, CActor* actor)
{
	Fmatrix fmat = Fidentity;
	fmat.i.set(mat.i);
	fmat.j.set(mat.j);
	fmat.k.set(mat.k);
	fmat.c.set(pt);
	//IPhysicsShellHolder * ve = smart_cast<IPhysicsShellHolder*> ( Level().CurrentEntity() ) ;
	VERIFY(actor);
	return test_camera_box(ext, fmat, actor);
}

#ifdef	DEBUG
template<typename T>
void	dbg_draw_viewport( const T &cam_info, float _viewport_near )
{
	
	VERIFY( _viewport_near > 0.f );
	const Fvector near_plane_center = Fvector().mad( cam_info.Position(), cam_info.Direction(), _viewport_near );
	float h_w, h_h;
	viewport_size ( _viewport_near, cam_info, h_w, h_h );
	const Fvector right	= Fvector().mul( cam_info.Right(), h_w );
	const Fvector up	= Fvector().mul( cam_info.Up(), h_h );

	
	const Fvector	top_left = Fvector().sub( near_plane_center,  right ).add( up );
	const Fvector	top_right = Fvector().add( near_plane_center,  right ).add( up );
	const Fvector	bottom_left = Fvector().sub( near_plane_center,  right ).sub( up );
	const Fvector	bottom_right = Fvector().add( near_plane_center,  right ).sub( up );
	
	DBG_DrawLine( cam_info.Position(), top_left, D3DCOLOR_XRGB(255, 0, 0 ) );
	DBG_DrawLine( cam_info.Position(), top_right, D3DCOLOR_XRGB(255, 0, 0 ) );
	DBG_DrawLine( cam_info.Position(), bottom_left, D3DCOLOR_XRGB(255, 0, 0 ) );
	DBG_DrawLine( cam_info.Position(), bottom_right, D3DCOLOR_XRGB(255, 0, 0 ) );

	DBG_DrawLine( top_right, top_left, D3DCOLOR_XRGB(255, 0, 0 ) );
	DBG_DrawLine( bottom_right, top_right, D3DCOLOR_XRGB(255, 0, 0 ) );
	DBG_DrawLine( top_left, bottom_left, D3DCOLOR_XRGB(255, 0, 0 ) );
	DBG_DrawLine( bottom_left, bottom_right, D3DCOLOR_XRGB(255, 0, 0 ) );

}
#endif
IC void get_box_mat(Fmatrix33& mat, float alpha, const SRotation& r_torso)
{
	float dZ = ((PI_DIV_2 - ((PI + alpha) / 2)));
	Fmatrix xformR;
	xformR.setXYZ(-r_torso.pitch, r_torso.yaw, -dZ);
	mat.i = xformR.i;
	mat.j = xformR.j;
	mat.k = xformR.k;
}

IC void get_q_box(Fbox& xf, float c, float alpha, float radius)
{
	Fvector src_pt, tgt_pt;
	calc_point(tgt_pt, radius, 0, alpha);
	src_pt.set(0, tgt_pt.y, 0);
	xf.invalidate();
	xf.modify(src_pt);
	xf.modify(tgt_pt);
	xf.grow(c);
}

IC void get_cam_oob(Fvector& bc, Fvector& bd, Fmatrix33& mat, const Fmatrix& xform, const SRotation& r_torso,
                    float alpha, float radius, float c)
{
	get_box_mat(mat, alpha, r_torso);
	Fbox xf;
	get_q_box(xf, c, alpha, radius);
	xf.xform(Fbox().set(xf), xform);
	// query
	xf.get_CD(bc, bd);
}

IC void get_cam_oob(Fvector& bd, Fmatrix& mat, const Fmatrix& xform, const SRotation& r_torso, float alpha,
                    float radius, float c)
{
	Fmatrix33 mat3;
	Fvector bc;
	get_cam_oob(bc, bd, mat3, xform, r_torso, alpha, radius, c);
	mat.set(Fidentity);
	mat.i.set(mat3.i);
	mat.j.set(mat3.j);
	mat.k.set(mat3.k);
	mat.c.set(bc);
}

void CActor::cam_Lookout(const Fmatrix& xform, float camera_height)
{
	if (!fis_zero(r_torso_tgt_roll))
	{
		float w, h;
		float c = viewport_near(w, h);
		w /= 2.f;
		h /= 2.f;
		float alpha = r_torso_tgt_roll / 2.f;
		float radius = camera_height * 0.5f;
		// init valid angle
		float valid_angle = alpha;
		Fvector bc, bd;
		Fmatrix33 mat;
		get_cam_oob(bc, bd, mat, xform, r_torso, alpha, radius, c);

		/*
		xrXRC				xrc			;
		xrc.box_options		(0)			;
		xrc.box_query		(Level().ObjectSpace.GetStaticModel(), bc, bd)		;
		u32 tri_count		= xrc.r_count();

		*/
		//if (tri_count)		
		{
			float da = 0.f;
			BOOL bIntersect = FALSE;
			Fvector ext = {w, h,VIEWPORT_NEAR / 2};
			Fvector pt;
			calc_gl_point(pt, xform, radius, alpha);
			if (test_point(pt, mat, ext, this))
			{
				da = PI / 1000.f;
				if (!fis_zero(r_torso.roll))
					da *= r_torso.roll / _abs(r_torso.roll);
				float angle = 0.f;
				for (; _abs(angle) < _abs(alpha); angle += da)
				{
					Fvector pt;
					calc_gl_point(pt, xform, radius, angle);
					if (test_point(pt, mat, ext, this))
					{
						bIntersect = TRUE;
						break;
					}
				}
				valid_angle = bIntersect ? angle : alpha;
			}
		}
		r_torso.roll = valid_angle * 2.f;
		r_torso_tgt_roll = r_torso.roll;
	}
	else
	{
		r_torso_tgt_roll = 0.f;
		r_torso.roll = 0.f;
	}
}
#ifdef	DEBUG
BOOL ik_cam_shift = true;
float ik_cam_shift_tolerance = 0.2f;
float ik_cam_shift_speed = 0.01f;
#else
static const BOOL ik_cam_shift = true;
static const float ik_cam_shift_tolerance = 0.2f;
static const float ik_cam_shift_speed = 0.01f;
#endif

BOOL firstPersonDeath = TRUE;
extern BOOL  ps_r_fp_body;                 // OWA: first-person body
// Note: ps_r_fp_body_base_offset and ps_r_fp_body_cam_offset are applied to the
// body root in renderable_Render, not to the camera here. See Actor.cpp.
extern float ps_r_fp_body_smooth_v;        // OWA: vertical EMA time constant (seconds)
extern float ps_r_fp_body_smooth_h;        // OWA: horizontal EMA time constant (seconds)
extern float ps_r_fp_body_smooth_h_limit;  // OWA: max horizontal camera lag (metres)
float offsetH = 0;
float offsetP = 0;
float offsetB = 0;
float offsetX = 0;
float offsetY = 0;
float offsetZ = 0;
float viewportNearOffset = 0;
float firstPersonDeathHeadScale = 3.f;

void CActor::cam_Update(float dt, float fFOV)
{
	if (m_holder) return;

	// HUD FOV Update
	if (this == Level().CurrentControlEntity())
	{
		if (eacFirstEye == cam_active)
		{
			CHudItem* pItem = smart_cast<CHudItem*>(inventory().ActiveItem());
			CHudItem* pDevice = smart_cast<CHudItem*>(inventory().ItemFromSlot(DETECTOR_SLOT));

			if (pItem && pItem->IsAttachedToHUD() && pDevice && pDevice->IsAttachedToHUD())
				psHUD_FOV = fminf(pItem->GetHudFov(), pDevice->GetHudFov());
			else if (pItem && pItem->IsAttachedToHUD())
				psHUD_FOV = pItem->GetHudFov();
			else if (pDevice && pDevice->IsAttachedToHUD())
				psHUD_FOV = pDevice->GetHudFov();
			else
				psHUD_FOV = psHUD_FOV_def;
		}
		else
			psHUD_FOV = psHUD_FOV_def;
	}

	if (cam_freelook != eflDisabled && cam_active != eacFreeLook)
		camUpdateFreelook(dt);

	if ((mstate_real & mcClimb) && (cam_active != eacFreeLook))
	{
		if (cam_freelook != eflDisabled)
			cam_UnsetFreelook();
		camUpdateLadder(dt);
	}
		
	on_weapon_shot_update();
	float y_shift = 0;

	if (GamePersistent().GameType() != eGameIDSingle && ik_cam_shift && character_physics_support() &&
		character_physics_support()->ik_controller())
	{
		y_shift = character_physics_support()->ik_controller()->Shift();
		float cam_smooth_k = 1.f;
		if (_abs(y_shift - current_ik_cam_shift) > ik_cam_shift_tolerance)
		{
			cam_smooth_k = 1.f - ik_cam_shift_speed * dt / 0.01f;
		}

		if (_abs(y_shift) < ik_cam_shift_tolerance / 2.f)
			cam_smooth_k = 1.f - ik_cam_shift_speed * 1.f / 0.01f * dt;
		clamp(cam_smooth_k, 0.f, 1.f);
		current_ik_cam_shift = cam_smooth_k * current_ik_cam_shift + y_shift * (1.f - cam_smooth_k);
	}
	else
		current_ik_cam_shift = 0;

	// Alex ADD: smooth crouch fix
	float HeightInterpolationSpeed = 4.f;

	if (CurrentHeight < 0.0f)
		CurrentHeight = CameraHeight();

	if (CurrentHeight != CameraHeight())
	{
		CurrentHeight = (CurrentHeight * (1.0f - HeightInterpolationSpeed * dt)) + (CameraHeight() *
			HeightInterpolationSpeed * dt);
	}

	Fvector point = {0, CurrentHeight + current_ik_cam_shift, 0};
	Fvector dangle = {0, 0, 0};
	Fmatrix xform;
	xform.setXYZ(0, r_torso.yaw, 0);
	xform.translate_over(XFORM().c);

	// lookout
	if (this == Level().CurrentControlEntity())
		cam_Lookout(xform, point.y);


	if (!fis_zero(r_torso.roll))
	{
		float radius = point.y * 0.5f;
		float valid_angle = r_torso.roll / 2.f;
		calc_point(point, radius, 0, valid_angle);
		dangle.z = (PI_DIV_2 - ((PI + valid_angle) / 2));
	}

	float flCurrentPlayerY = xform.c.y;

	// Smooth out stair step ups.
	// stairYAdjust (≤ 0 when stepping up) is saved so the FP body path can apply
	// the same offset to the eye-bone camera Y, giving stairs the same smoothness
	// in FP body mode without recomputing anything.
	float stairYAdjust = 0.f;
	if ((character_physics_support()->movement()->Environment() == CPHMovementControl::peOnGround) && (flCurrentPlayerY
		- fPrevCamPos > 0))
	{
		fPrevCamPos += dt * 1.5f;
		if (fPrevCamPos > flCurrentPlayerY)
			fPrevCamPos = flCurrentPlayerY;
		if (flCurrentPlayerY - fPrevCamPos > 0.2f)
			fPrevCamPos = flCurrentPlayerY - 0.2f;
		stairYAdjust = fPrevCamPos - flCurrentPlayerY;
		point.y += stairYAdjust;
	}
	else
	{
		fPrevCamPos = flCurrentPlayerY;
	}

	float _viewport_near = VIEWPORT_NEAR;
	// calc point
	xform.transform_tiny(point);

	// OWA: FP body - override camera position with the animated eye bone world position.
	// The fixed-offset camera sits independently of the skeleton, so sprint/lean animations
	// move the body in front of the camera. By driving the camera from the eye bone (the same
	// bone the death camera uses), the camera stays locked to the character's actual eye level
	// through all animations. Falls back to bip01_head if eye_right is absent.
	if (ps_r_fp_body && g_Alive() && IsFocused() && cam_active == eacFirstEye)
	{
		IKinematics* k = Visual()->dcast_PKinematics();
		if (k)
		{
			k->CalculateBones(TRUE);
			u16 eye_bone = (m_eye_right != BI_NONE) ? m_eye_right : m_head;
			if (eye_bone != BI_NONE)
			{
				Fmatrix eyeWorld;
				eyeWorld.mul_43(XFORM(), k->LL_GetBoneInstance(eye_bone).mTransform);
				Fvector rawPos = eyeWorld.c;

				// OWA: Eye-bone geometry safety clamp.
				//
				// The 3rd-person physics capsule allows the rendered skeleton to animate beyond
				// its boundary. When crouching against a wall the forward-lean animation can push
				// the eye bone into the wall even though the physics body is technically valid.
				// collide_camera() is supposed to push the camera back out, but it operates
				// reactively on the camera box — it can fail for thin geometry or when the
				// clamp_change early-exit treats a stable penetration as "no change needed".
				//
				// Fix: clamp rawPos proactively before it is ever used as the camera position.
				// Cast HORIZONTALLY at eye-bone height from the actor's body axis to the raw
				// eye position. If static geometry is in the way, snap rawPos.x/z to just
				// outside the surface. rawPos.y is deliberately left unchanged — the actor
				// crouches vertically, not forward-tilts, so eye-bone Y is the correct height.
				//
				// The actor's physics guarantees XFORM().c is outside geometry, so starting
				// 1 cm behind the actor's horizontal centre is always a safe ray origin.
				// collide_camera() still runs afterwards for final viewport-near placement.
				{
					Fvector to_eye_h;
					to_eye_h.set(rawPos.x - XFORM().c.x, 0.f, rawPos.z - XFORM().c.z);
					const float h_dist = to_eye_h.magnitude();
					if (h_dist > 0.001f)
					{
						to_eye_h.mul(1.f / h_dist);  // normalize (horizontal only)

						// 1 cm behind actor horizontal centre at eye height — guaranteed
						// outside any wall the capsule is currently touching.
						Fvector origin;
						origin.set(XFORM().c.x, rawPos.y, XFORM().c.z);
						origin.mad(origin, to_eye_h, -0.01f);

						collide::rq_result RQ;
						const float ray_len = h_dist + 0.01f;  // just past the eye bone
						BOOL hit = Level().ObjectSpace.RayPick(
							origin, to_eye_h, ray_len, collide::rqtStatic, RQ, nullptr);

						if (hit)
						{
							// Wall found between actor body and eye bone.
							// Snap X/Z to just outside the surface; leave Y unchanged.
							const float ROUGH_SKIN = 0.02f;
							float safe_h = _max(0.f, RQ.range - ROUGH_SKIN);
							rawPos.x = origin.x + to_eye_h.x * safe_h;
							rawPos.z = origin.z + to_eye_h.z * safe_h;
						}
					}
				}

				// OWA: Accessibility smoothing for FP body eye-bone camera position.
				//
				// We smooth the *delta* between the eye bone and the actor's root position
				// rather than the absolute world position. This is crucial because:
				//
				//   - Large-scale movement (player walking/running): XFORM().c moves in lock-step
				//     with the body, so rawDelta (eye-to-root offset) stays nearly constant.
				//     The smoothed position follows the body instantly — no body/camera desync.
				//
				//   - Oscillatory animation movement (walk bob, strafe sway): XFORM().c is stable
				//     while the eye bone oscillates around it. rawDelta oscillates, and *that*
				//     oscillation is what we attenuate — exactly the motion-sickness trigger.
				//
				// Separate time constants for vertical (heavier smoothing safe) and horizontal
				// (lighter, bounded by a hard clamp to prevent the body running ahead of view).
				// Frame-rate independent EMA: alpha = 1 - exp(-dt / tau), tau = time constant (s).
				if (ps_r_fp_body_smooth_v > 0.f || ps_r_fp_body_smooth_h > 0.f)
				{
					// rawDelta: eye bone offset relative to actor root in world space.
					// For a standing player this is roughly (0, 1.7, 0) plus animation wiggle.
					Fvector rawDelta;
					rawDelta.sub(rawPos, xform.c);

					// Seed the smoother on first frame (or after gap) to avoid a startup snap.
					if (!m_fpBodySmoothedDeltaValid)
					{
						m_fpBodySmoothedDelta      = rawDelta;
						m_fpBodySmoothedDeltaValid = true;
					}

					// Clamp dt to [0, 100ms] to stay stable across lag spikes or pauses.
					float safeDt = _max(0.f, _min(dt, 0.1f));

					float tauV   = ps_r_fp_body_smooth_v;
					float tauH   = ps_r_fp_body_smooth_h;
					float alphaV = (tauV > 0.f) ? (1.f - expf(-safeDt / tauV)) : 1.f;
					float alphaH = (tauH > 0.f) ? (1.f - expf(-safeDt / tauH)) : 1.f;

					m_fpBodySmoothedDelta.x += alphaH * (rawDelta.x - m_fpBodySmoothedDelta.x);
					m_fpBodySmoothedDelta.y += alphaV * (rawDelta.y - m_fpBodySmoothedDelta.y);
					m_fpBodySmoothedDelta.z += alphaH * (rawDelta.z - m_fpBodySmoothedDelta.z);

					// Hard horizontal clamp: smoothed delta must stay within r_fp_body_smooth_h_limit
					// metres of the raw delta. This prevents the body from visually "running ahead"
					// of the camera when horizontal smoothing is set aggressively.
					float hLim = ps_r_fp_body_smooth_h_limit;
					clamp(m_fpBodySmoothedDelta.x, rawDelta.x - hLim, rawDelta.x + hLim);
					clamp(m_fpBodySmoothedDelta.z, rawDelta.z - hLim, rawDelta.z + hLim);

					point.add(xform.c, m_fpBodySmoothedDelta);
				}
				else
				{
					// Smoothing disabled: reset so re-enabling starts fresh without a snap.
					m_fpBodySmoothedDeltaValid = false;
					point = rawPos;
				}

				// Re-apply the stair-step smoothing that was computed before this block
				// ran. The regular smoother modified the old point.y, but that was then
				// discarded when point was replaced with the eye-bone world position above.
				// Applying stairYAdjust here restores smooth stair ascent in FP body mode.
				// fPrevCamPos is intentionally NOT reset here -- letting the accumulator
				// run naturally means no snap if FP body is toggled off mid-stair.
				point.y += stairYAdjust;
			}
		}
	}

	CCameraBase* C = cam_Active();

	C->Update(point, dangle);
	C->f_fov = fFOV;

	if (eacFirstEye != cam_active)
	{
		cameras[eacFirstEye]->Update(point, dangle);
		cameras[eacFirstEye]->f_fov = fFOV;
	}
	if (Level().CurrentEntity() == this)
	{
		collide_camera(*cameras[eacFirstEye], _viewport_near, this);
	}

	// OWA: FP body forward obstacle clearance — multi-height scan.
	//
	// The camera collision runs at eye level and is immune to clipping, but the visible
	// FP body mesh extends above and below the eye bone. In crouch-against-wall poses the
	// upper-chest/shoulder zone can extend further forward than the eye bone and clip into
	// geometry even though the camera view is clear.
	//
	// Fix: cast two forward rays along the actor's horizontal-forward axis at heights that
	// proportionally bracket the visible torso — lower chest and upper chest/shoulder.
	// Because the heights are expressed as fractions of the actual camera eye height above
	// the actor root, they adapt automatically to both standing and crouching states:
	//   standing  (eye ≈ 1.65 m above root): scans at ≈0.91 m and ≈1.32 m
	//   crouching (eye ≈ 1.00 m above root): scans at ≈0.55 m and ≈0.80 m
	// The maximum pullback from all rays drives m_fpBodyChestClearance.
	// EMA-smoothed to avoid single-frame pops on curved surfaces.
	if (ps_r_fp_body && g_Alive() && IsFocused() && cam_active == eacFirstEye
		&& !(mstate_real & mcClimb))
	{
		const float SCAN_DIST    = 0.30f;  // forward scan range
		const float BODY_FORWARD = 0.15f;  // conservative body mesh forward extent from root
		const float CLEARANCE    = 0.03f;  // minimum gap to maintain

		// Derive eye height from the collision-resolved camera position (post-collide_camera).
		// Falls back to a sane default in the unlikely event the camera hasn't been seeded yet.
		float eye_height = cam_FirstEye()->vPosition.y - XFORM().c.y;
		if (eye_height < 0.1f)
			eye_height = 0.9f;

		// Lower chest and upper chest/shoulder — two fractions of eye height.
		const float scan_fracs[2] = { 0.55f, 0.80f };

		float max_target = 0.f;
		for (int si = 0; si < 2; si++)
		{
			Fvector origin;
			origin.set(XFORM().c);
			origin.y += eye_height * scan_fracs[si];

			collide::rq_result RQ;
			BOOL hit = Level().ObjectSpace.RayPick(
				origin, xform.k, SCAN_DIST, collide::rqtStatic, RQ, nullptr);

			if (hit)
				max_target = _max(max_target, _max(0.f, (BODY_FORWARD + CLEARANCE) - RQ.range));
		}

		// EMA with ~50 ms time constant — snappy enough for fast movement,
		// smooth enough to avoid per-frame jitter on curved surfaces.
		const float TAU = 0.05f;
		float alpha = 1.f - expf(-Device.fTimeDelta / TAU);
		m_fpBodyChestClearance += alpha * (max_target - m_fpBodyChestClearance);
	}
	else
	{
		m_fpBodyChestClearance = 0.f;
	}

    static bool firstPersonDeathDied = false;
	if (cam_active == eacFirstEye) {
        bool firstPersonDeathDiedNow = firstPersonDeath && !g_Alive() && m_FPCam;
		if (firstPersonDeathDiedNow) {
            if (firstPersonDeathDied != firstPersonDeathDiedNow)
            {
                if (m_pPhysicsShell)
                {
                    auto head = m_pPhysicsShell->get_Element("bip01_head");
                    if (head)
                        head->SetScale(firstPersonDeathHeadScale);
                }
            }
			IKinematics* k = Visual()->dcast_PKinematics();

			// Get eye bone position
			CBoneInstance& eyeBone = k->LL_GetBoneInstance(m_eye_right);
			Fmatrix matrix = Fidentity;
			matrix.mul_43(XFORM(), eyeBone.mTransform);
			Fvector camPos = (matrix.c);

			// Get head bone direction, works better for first person death
			CBoneInstance& headBone = k->LL_GetBoneInstance(m_head);
			Fmatrix matrixDir = Fidentity;
			Fvector camDir;
			matrixDir.mul_43(XFORM(), headBone.mTransform);
			matrixDir.getHPB(camDir);

			// Adjust camera direction
			Fvector adjustedCamDir;
			adjustedCamDir.set(camDir).setHP(
				camDir.x + deg2rad(8.f) + deg2rad(offsetH),
				camDir.y - deg2rad(20.f) + deg2rad(offsetP)
			);
			camDir.set(
				adjustedCamDir.getH(),
				adjustedCamDir.getP(),
				camDir.z + deg2rad(90.f) + deg2rad(offsetB)
			);
			if (camDir.x < 0) {
				camDir.x = PI_MUL_2 + camDir.x;
			}

			Fvector dir, dirUp, dirRight;
			dir.setHP(camDir.x, camDir.y);
			Fvector::generate_orthonormal_basis_normalized(dir, dirUp, dirRight);

			camPos.mad(dir, -0.04 + offsetZ);
			camPos.mad(dirUp, offsetY);
			camPos.mad(dirRight, -0.01 + offsetX);

			m_FPCam->m_HPB.set(camDir);
			m_FPCam->m_Position.set(camPos);
			_viewport_near = VIEWPORT_NEAR - 0.08 + viewportNearOffset;
			//Cameras().ApplyDevice(_viewport_near);
		}
        firstPersonDeathDied = firstPersonDeathDiedNow;
	}

	//Alundaio -psp always
	//if( psActorFlags.test(AF_PSP) )
	//{
	Cameras().UpdateFromCamera(C);
	//}else
	//{
	//	Cameras().UpdateFromCamera			(cameras[eacFirstEye]);
	//}
	//-Alundaio

	fCurAVelocity = vPrevCamDir.sub(cameras[eacFirstEye]->vDirection).magnitude() / Device.fTimeDelta;
	vPrevCamDir = cameras[eacFirstEye]->vDirection;

	// Высчитываем разницу между предыдущим и текущим Yaw \ Pitch от 1-го лица //--#SM+ Begin#--
	float& cam_yaw_cur = cameras[eacFirstEye]->yaw;
	float& cam_pitch_cur = cameras[eacFirstEye]->pitch;

	static bool freelook_last_frame;
	static float cam_yaw_prev = cam_yaw_cur;
	static float cam_pitch_prev = cam_pitch_cur;

	fFPCamYawMagnitude = freelook_last_frame ? 0.f : (1.f - freelook_cam_control) * (angle_difference_signed(cam_yaw_prev, cam_yaw_cur) / Device.fTimeDelta); // L+ / R-
	fFPCamPitchMagnitude = freelook_last_frame ? 0.f : (1.f - freelook_cam_control) * (angle_difference_signed(cam_pitch_prev, cam_pitch_cur) / Device.fTimeDelta); //U+ / D-

	freelook_last_frame = cam_freelook == eflDisabling;
	cam_yaw_prev = cam_yaw_cur;
	cam_pitch_prev = cam_pitch_cur;
	//--#SM+ End#--

#ifdef DEBUG
	if( dbg_draw_camera_collision )
	{
		dbg_draw_viewport( *cameras[eacFirstEye], _viewport_near );
		dbg_draw_viewport( Cameras(), _viewport_near );
	}
#endif

	if (Level().CurrentEntity() == this)
	{
		Level().Cameras().UpdateFromCamera(C);
		if (eacFirstEye == cam_active && !Level().Cameras().GetCamEffector(cefDemo) && !Device.m_SecondViewport.IsSVPActive())
		{
			Cameras().ApplyDevice(_viewport_near);
		}
	}
}

// shot effector stuff
void CActor::update_camera(CCameraShotEffector* effector)
{
	if (!effector) return;
	//	if (Level().CurrentViewEntity() != this) return;

	CCameraBase* pACam = cam_Active();
	if (!pACam) return;

	if (pACam->bClampPitch)
	{
		while (pACam->pitch < pACam->lim_pitch[0])
			pACam->pitch += PI_MUL_2;
		while (pACam->pitch > pACam->lim_pitch[1])
			pACam->pitch -= PI_MUL_2;
	}

	effector->ChangeHP(&(pACam->pitch), &(pACam->yaw));

	if (pACam->bClampYaw) clamp(pACam->yaw, pACam->lim_yaw[0], pACam->lim_yaw[1]);
	if (pACam->bClampPitch) clamp(pACam->pitch, pACam->lim_pitch[0], pACam->lim_pitch[1]);

	if (effector && !effector->IsActive())
	{
		Cameras().RemoveCamEffector(eCEShot);
	}
}


#ifdef DEBUG
void dbg_draw_frustum (float FOV, float _FAR, float A, Fvector &P, Fvector &D, Fvector &U);
extern	Flags32	dbg_net_Draw_Flags;
extern	BOOL g_bDrawBulletHit;

void CActor::OnRender	()
{
#ifdef DEBUG
	if (inventory().ActiveItem())
		inventory().ActiveItem()->OnRender();
#endif
	if (!bDebug)				return;

	if ((dbg_net_Draw_Flags.is_any(dbg_draw_actor_phys)))
		character_physics_support()->movement()->dbg_Draw	();

	

	OnRender_Network();

	inherited::OnRender();
}
#endif
