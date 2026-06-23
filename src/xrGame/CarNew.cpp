#include "StdAfx.h"
#ifdef CAR_NEW
#include "Car.h"
#include "Level.h"
#include "../Include/xrRender/Kinematics.h"

#include "script_game_object.h"
#include "CameraFirstEye.h"
#include "cameralook.h"
#include "xr_level_controller.h"

bool CCar::is_ai_obstacle() const
{
	/* npcs will try to walk around car when it is on the way. */
	return true;
}

bool CCar::IsCameraZoom()
{
	return m_zoom_status;
}

void CCar::SetUseAction(LPCSTR txt)
{
	m_sUseAction._set(txt);
}

/*------------------------------------------------------------------------------------------------------------------------
    Fly
------------------------------------------------------------------------------------------------------------------------*/
CCar::SCarFlyBone::SCarFlyBone()
{
	bid = BI_NONE;
	E = nullptr;
	J = nullptr;
	clockwise = false;
	axis = 0;
	spinning = false;
}

void CCar::Fly_Load(LPCSTR section)
{
	m_control_neutral = READ_IF_EXISTS(pSettings, r_float, section, "control_neutral", 0.0F);
	m_control_ele_max = READ_IF_EXISTS(pSettings, r_float, section, "control_ele_max", 0.0F);
	m_control_pit_max = READ_IF_EXISTS(pSettings, r_float, section, "control_pit_max", 0.0F);
	m_control_rol_max = READ_IF_EXISTS(pSettings, r_float, section, "control_rol_max", 0.0F);
	m_control_yaw_max = deg2rad(READ_IF_EXISTS(pSettings, r_float, section, "control_yaw_max", 0.0F));
	m_control_ele_inc = READ_IF_EXISTS(pSettings, r_float, section, "control_ele_inc", 0.0F);
	m_control_pit_inc = READ_IF_EXISTS(pSettings, r_float, section, "control_pit_inc", 0.0F);
	m_control_rol_inc = READ_IF_EXISTS(pSettings, r_float, section, "control_rol_inc", 0.0F);
	m_control_yaw_inc = deg2rad(READ_IF_EXISTS(pSettings, r_float, section, "control_yaw_inc", 0.0F));

	m_fly_weight_min = READ_IF_EXISTS(pSettings, r_float, section, "fly_weight_min", 1.0F);
	m_fly_weight_min = (m_fly_weight_min > 0.0F) ? m_fly_weight_min : 1.0F;
}

BOOL CCar::Fly_net_Spawn(CSE_Abstract *DC)
{
	IKinematics *K = Visual()->dcast_PKinematics();
	CInifile *ini = K->LL_UserData();
	const LPCSTR fly_sec = "fly_definition";

	/* body_bone is for applying torque. */
	m_body_bid = ini->line_exist(fly_sec, "body_bone") ? K->LL_BoneID(ini->r_string(fly_sec, "body_bone")) : BI_NONE;

	{
		m_rotor_bones.clear();
		int n = ini->line_count("rotors");
		for (int k = 0; k < n; k++)
		{
			LPCSTR bone_name, str;
			ini->r_line("rotors", k, &bone_name, &str);
			u16 bone_id = K->LL_BoneID(bone_name);
			if (bone_id != BI_NONE)
			{
				m_rotor_bones.push_back(SCarFlyBone());
				SCarFlyBone &I = m_rotor_bones.back();
				I.E = m_pPhysicsShell->get_Element(bone_id);
				I.J = m_pPhysicsShell->get_Joint(bone_id);

				string64 tmp, key, val;
				for (int c = 0; c < _GetItemCount(str); ++c)
				{
					memset(tmp, 0, sizeof(tmp));
					_GetItem(str, c, tmp);
					if (strlen(tmp) && _GetItemCount(tmp, ':') == 2)
					{
						memset(key, 0, sizeof(key));
						memset(val, 0, sizeof(val));
						_GetItem(tmp, 0, key, ':');
						_GetItem(tmp, 1, val, ':');
						if (strcmp(key, "clockwise") == 0)
						{
							I.clockwise = strcmp(val, "true") == 0;
						}
					}
				}
			}
		}
		R_ASSERT3(m_rotor_bones.size(), "fly_definition no rotors", cNameSect_str());
	}
	{
		m_drive_bones.clear();
		LPCSTR str = READ_IF_EXISTS(ini, r_string, fly_sec, "drive_bones", NULL);
		int n = _GetItemCount(str);
		string64 bone_name;
		for (int k = 0; k < n; ++k)
		{
			memset(bone_name, 0, sizeof(bone_name));
			_GetItem(str, k, bone_name);
			u16 bone_id = K->LL_BoneID(bone_name);
			if (bone_id != BI_NONE)
			{
				m_drive_bones.push_back(SCarFlyBone());
				SCarFlyBone &I = m_drive_bones.back();
				I.bid = bone_id;
				I.E = m_pPhysicsShell->get_Element(bone_id);
			}
		}
		R_ASSERT3(m_drive_bones.size(), "fly_definition no drive_bones", cNameSect_str());
	}

	m_rotor_force_max = READ_IF_EXISTS(ini, r_float, fly_sec, "rotor_force_max", 0.0F);
	m_rotor_speed_max = READ_IF_EXISTS(ini, r_float, fly_sec, "rotor_speed_max", 0.0F);

	return TRUE;
}

bool CCar::Fly_attach_Actor(CGameObject *actor)
{
	ControlReset();
	return true;
}

void CCar::Fly_detach_Actor()
{
	ControlReset();
}

void CCar::Fly_VisualUpdate(float fov)
{
	if (m_pPhysicsShell)
	{
		m_pPhysicsShell->InterpolateGlobalTransform(&XFORM());
		Visual()->dcast_PKinematics()->CalculateBones();
	}

	m_car_sound->Update();
}

void CCar::Fly_PhDataUpdate(float step)
{
	/* Only update physic. Don't calculate bones. */
	if (m_pPhysicsShell == nullptr)
		return;

	Fly_RotorUpdate();
	if (b_engine_on && m_rotor_bones.size() && m_drive_bones.size())
	{
		CPhysicsElement *bone_body = m_pPhysicsShell->get_Element(m_body_bid);
		if (bone_body == nullptr)
		{
			bone_body = m_pPhysicsShell->get_ElementByStoreOrder(0);
		}

		float mass = m_pPhysicsShell->getMass();

		/* Make it float in mid air. Must be m_rotor_bones, not m_drive_bones. */
		{
			float force = mass * EffectiveGravity() / m_rotor_bones.size();
			Fvector vec = Fvector().set(0.0F, 1.0F, 0.0F).mul(force);
			for (auto &I : m_rotor_bones)
			{
				I.E->applyForce(vec.x, vec.y, vec.z);
			}
		}

		if (m_control_ele == eControlEle_NA && m_control_pit == eControlPit_NA && m_control_rol == eControlRol_NA)
		{
			Fvector velocity;
			m_pPhysicsShell->get_LinearVel(velocity);
			if (velocity.magnitude() > EPS_L)
			{
				float force = mass * FlyWeightScale() * __min(velocity.magnitude(), m_control_neutral);
				Fvector vec = Fvector().invert(velocity).normalize_safe().mul(force);
				m_pPhysicsShell->applyForce(vec.x, vec.y, vec.z);
			}
		}

		/* Control. */
		switch (m_control_ele)
		{
		case eControlEle_UP:
		case eControlEle_DW:
		{
			float force = mass * FlyWeightScale() * m_control_ele_max / m_drive_bones.size();
			Fvector vec = Fvector().set(0.0F, 1.0F, 0.0F).mul((m_control_ele == eControlEle_UP) ? force : -force);
			for (auto &I : m_drive_bones)
			{
				I.E->applyRelForce(vec.x, vec.y, vec.z);
			}
			break;
		}
		}

		switch (m_control_pit)
		{
		case eControlPit_FS:
		case eControlPit_BS:
		{
			float force = mass * FlyWeightScale() * m_control_pit_max / m_drive_bones.size();
			Fvector vec = Fvector().set(0.0F, 0.0F, 1.0F).mul((m_control_pit == eControlPit_FS) ? force : -force);
			for (auto &I : m_drive_bones)
			{
				I.E->applyRelForce(vec.x, vec.y, vec.z);
			}
			break;
		}
		}

		switch (m_control_rol)
		{
		case eControlRol_RS:
		case eControlRol_LS:
		{
			float force = mass * FlyWeightScale() * m_control_rol_max / m_drive_bones.size();
			Fvector vec = Fvector().set(1.0F, 0.0F, 0.0F).mul((m_control_rol == eControlRol_RS) ? force : -force);
			for (auto &I : m_drive_bones)
			{
				I.E->applyRelForce(vec.x, vec.y, vec.z);
			}
			break;
		}
		}

		switch (m_control_yaw)
		{
		case eControlYaw_RS:
		case eControlYaw_LS:
		{
			float force = mass * FlyWeightScale() * m_control_yaw_max;
			Fvector vec = Fvector().set(0.0F, 1.0F, 0.0F).mul((m_control_yaw == eControlYaw_RS) ? force : -force);
			bone_body->applyRelTorque(vec.x, vec.y, vec.z);
			break;
		}
		}
	}
}

void CCar::Fly_RotorUpdate()
{
	if (b_engine_on)
	{
		for (auto &I : m_rotor_bones)
		{
			if (I.spinning != true)
			{
				I.spinning = true;
				I.J->SetForceAndVelocity(m_rotor_force_max, m_rotor_speed_max * ((I.clockwise) ? 1 : -1), 1);
			}
		}
	}
	else
	{
		for (auto &I : m_rotor_bones)
		{
			if (I.spinning == true)
			{
				I.spinning = false;
				I.J->SetForceAndVelocity(0, 0, 1);
			}
		}
	}
}

float CCar::FlyWeightScale()
{
    return m_fly_weight_min / (m_fly_weight_min + m_fly_weight_add);
}

void CCar::SetFlyWeightAdd(float val)
{
	m_fly_weight_add = (val > 0.0F) ? val : 0.0F;
}

void CCar::ControlReset()
{
	ControlPressEleUp(false);
	ControlPressEleDw(false);
	ControlPressYawRs(false);
	ControlPressYawLs(false);
	ControlPressPitFs(false);
	ControlPressPitBs(false);
	ControlPressRolRs(false);
	ControlPressRolLs(false);
	m_control_ele = eControlEle_NA;
	m_control_yaw = eControlYaw_NA;
	m_control_pit = eControlPit_NA;
	m_control_rol = eControlPit_NA;
}

void CCar::ControlPressEleUp(bool status)
{
	m_control_press_ele_up = status;
	if (status)
	{
		m_control_ele = (m_control_press_ele_dw) ? eControlEle_NA : eControlEle_UP;
	}
	else
	{
		m_control_ele = (m_control_press_ele_dw) ? eControlEle_DW : eControlEle_NA;
	}
}

void CCar::ControlPressEleDw(bool status)
{
	m_control_press_ele_dw = status;
	if (status)
	{
		m_control_ele = (m_control_press_ele_up) ? eControlEle_NA : eControlEle_DW;
	}
	else
	{
		m_control_ele = (m_control_press_ele_up) ? eControlEle_UP : eControlEle_NA;
	}
}

void CCar::ControlPressYawRs(bool status)
{
	m_control_press_yaw_rs = status;
	if (status)
	{
		m_control_yaw = (m_control_press_yaw_ls) ? eControlYaw_NA : eControlYaw_RS;
	}
	else
	{
		m_control_yaw = (m_control_press_yaw_ls) ? eControlYaw_LS : eControlYaw_NA;
	}
}

void CCar::ControlPressYawLs(bool status)
{
	m_control_press_yaw_ls = status;
	if (status)
	{
		m_control_yaw = (m_control_press_yaw_rs) ? eControlYaw_NA : eControlYaw_LS;
	}
	else
	{
		m_control_yaw = (m_control_press_yaw_rs) ? eControlYaw_RS : eControlYaw_NA;
	}
}

void CCar::ControlPressPitFs(bool status)
{
	m_control_press_pit_fs = status;
	if (status)
	{
		m_control_pit = (m_control_press_pit_bs) ? eControlPit_NA : eControlPit_FS;
	}
	else
	{
		m_control_pit = (m_control_press_pit_bs) ? eControlPit_BS : eControlPit_NA;
	}
}

void CCar::ControlPressPitBs(bool status)
{
	m_control_press_pit_bs = status;
	if (status)
	{
		m_control_pit = (m_control_press_pit_fs) ? eControlPit_NA : eControlPit_BS;
	}
	else
	{
		m_control_pit = (m_control_press_pit_fs) ? eControlPit_FS : eControlPit_NA;
	}
}

void CCar::ControlPressRolRs(bool status)
{
	m_control_press_rol_rs = status;
	if (status)
	{
		m_control_rol = (m_control_press_rol_ls) ? eControlRol_NA : eControlRol_RS;
	}
	else
	{
		m_control_rol = (m_control_press_rol_ls) ? eControlRol_LS : eControlRol_NA;
	}
}

void CCar::ControlPressRolLs(bool status)
{
	m_control_press_rol_ls = status;
	if (status)
	{
		m_control_rol = (m_control_press_rol_rs) ? eControlRol_NA : eControlRol_LS;
	}
	else
	{
		m_control_rol = (m_control_press_rol_rs) ? eControlRol_RS : eControlRol_NA;
	}
}

/*----------------------------------------------------------------------------------------------------
	IR
----------------------------------------------------------------------------------------------------*/
void CCar::Fly_OnMouseMove(int dx, int dy)
{
	CCameraBase *cam = Camera();
	float scale = (cam->f_fov / g_fov) * psMouseSens * psMouseSensScale / 50.0F;
	if (dx)
	{
		float d = float(dx) * scale;
		cam->Move((d < 0) ? kLEFT : kRIGHT, _abs(d));
	}
	if (dy)
	{
		float d = ((psMouseInvert.test(1)) ? -1 : 1) * float(dy) * scale * 3.0F / 4.0F;
		cam->Move((d > 0) ? kUP : kDOWN, _abs(d));
	}
}

void CCar::Fly_OnKeyboardPress(int dik)
{
	switch (dik)
	{
	/* Movement. */
	case kACCEL:
	case kSPRINT_TOGGLE:
		ControlPressEleUp(true);
		break;
	case kCROUCH:
		ControlPressEleDw(true);
		break;
	case kL_LOOKOUT:
		ControlPressYawLs(true);
		break;
	case kR_LOOKOUT:
		ControlPressYawRs(true);
		break;
	case kFWD:
		ControlPressPitFs(true);
		break;
	case kBACK:
		ControlPressPitBs(true);
		break;
	case kL_STRAFE:
		ControlPressRolLs(true);
		break;
	case kR_STRAFE:
		ControlPressRolRs(true);
		break;
	/* Action. */
	case kWPN_ZOOM:
		if (!psActorFlags.test(AF_AIM_TOGGLE))
		{
			m_zoom_status = true;
		}
		else
		{
			m_zoom_status = (m_zoom_status) ? false : true;
		}
		break;
	};
}

void CCar::Fly_OnKeyboardRelease(int dik)
{
	switch (dik)
	{
	/* Movement. */
	case kACCEL:
	case kSPRINT_TOGGLE:
		ControlPressEleUp(false);
		break;
	case kCROUCH:
		ControlPressEleDw(false);
		break;
	case kL_LOOKOUT:
		ControlPressYawLs(false);
		break;
	case kR_LOOKOUT:
		ControlPressYawRs(false);
		break;
	case kFWD:
		ControlPressPitFs(false);
		break;
	case kBACK:
		ControlPressPitBs(false);
		break;
	case kL_STRAFE:
		ControlPressRolLs(false);
		break;
	case kR_STRAFE:
		ControlPressRolRs(false);
		break;
	/* Action. */
	case kWPN_ZOOM:
		if (!psActorFlags.test(AF_AIM_TOGGLE))
		{
			m_zoom_status = false;
		}
		break;
	case kDETECTOR:
		SwitchEngine();
		break;
	/* Change camera. */
	case kCAM_1:
		OnCameraChange(ectFirst);
		break;
	case kCAM_2:
		OnCameraChange(ectChase);
		break;
	case kCAM_3:
		OnCameraChange(ectFree);
		break;
	};
}

void CCar::Fly_OnKeyboardHold(int dik)
{
}

/*------------------------------------------------------------------------------------------------------------------------
    SVisualCamera
------------------------------------------------------------------------------------------------------------------------*/
CCar::SVisualCamera::SVisualCamera(CCar* obj)
{
    m_car = obj;
    m_enable = false;
    m_active = false;

    m_snap = false;
    m_rotate_x_bone = BI_NONE;
    m_rotate_y_bone = BI_NONE;
    m_rotate_x_speed = 0.0F;
    m_rotate_y_speed = 0.0F;
    m_cur_x_rot = 0.0F;
    m_cur_y_rot = 0.0F;
    m_tgt_x_rot = 0.0F;
    m_tgt_y_rot = 0.0F;
}

CCar::SVisualCamera::~SVisualCamera()
{

}

void CCar::SVisualCamera::net_Spawn(CSE_Abstract* DC)
{
    IKinematics* K = Car()->Visual()->dcast_PKinematics();
    CInifile* ini = K->LL_UserData();
    const LPCSTR def = "visual_camera_definition";
    if (!READ_IF_EXISTS(ini, r_bool, def, "enable", FALSE))
        return;

    m_enable = true;
    m_rotate_x_bone = K->LL_BoneID(ini->r_string(def, "rotate_x_bone"));
    VERIFY(m_rotate_x_bone != BI_NONE);
    m_rotate_y_bone = K->LL_BoneID(ini->r_string(def, "rotate_y_bone"));
    VERIFY(m_rotate_y_bone != BI_NONE);
    m_rotate_x_speed = deg2rad(READ_IF_EXISTS(ini, r_float, def, "rotate_x_speed", 20.0F));
    m_rotate_y_speed = deg2rad(READ_IF_EXISTS(ini, r_float, def, "rotate_y_speed", 20.0F));
    m_snap = !!READ_IF_EXISTS(ini, r_bool, def, "snap", FALSE);

    CBoneData& bdX = K->LL_GetData(m_rotate_x_bone);
    VERIFY(bdX.IK_data.type == jtJoint);
    m_lim_x_rot.set(bdX.IK_data.limits[0].limit.x, bdX.IK_data.limits[0].limit.y);
    CBoneData& bdY = K->LL_GetData(m_rotate_y_bone);
    VERIFY(bdY.IK_data.type == jtJoint);
    m_lim_y_rot.set(bdY.IK_data.limits[1].limit.x, bdY.IK_data.limits[1].limit.y);
}

bool CCar::SVisualCamera::attach_Actor(CGameObject* obj)
{
    if (IsEnable() == false)
        return true;
    Activate(true);
    SetBoneCallbacks(true);
    return true;
}

void CCar::SVisualCamera::detach_Actor()
{
    if (IsEnable() == false)
        return;
    Activate(false);
    SetBoneCallbacks(false);
}

void CCar::SVisualCamera::VisualUpdate(float fov)
{
    if (IsEnable() == false)
        return;
    if (IsActive() == false)
        return;
    UpdateDir();
    UpdateCam();
}

void CCar::SVisualCamera::UpdateDir()
{
    {
        m_tgt_x_rot = angle_normalize_signed(-m_desire_ang.x);
        clamp(m_tgt_x_rot, -m_lim_x_rot.y, -m_lim_x_rot.x);
    }
    {
        m_tgt_y_rot = angle_normalize_signed(-m_desire_ang.y);
        clamp(m_tgt_y_rot, -m_lim_y_rot.y, -m_lim_y_rot.x);
    }

    if (Snap())
    {
        m_cur_x_rot = m_tgt_x_rot;
        m_cur_y_rot = m_tgt_y_rot;
    }
    else
    {
        m_cur_x_rot = angle_inertion_var(m_cur_x_rot, m_tgt_x_rot, m_rotate_x_speed, m_rotate_x_speed, PI, Device.fTimeDelta);
        m_cur_y_rot = angle_inertion_var(m_cur_y_rot, m_tgt_y_rot, m_rotate_y_speed, m_rotate_y_speed, PI, Device.fTimeDelta);
    }
}

void CCar::SVisualCamera::UpdateCam()
{
    CCameraBase* cam = Car()->Camera();
    if (cam->tag == ectFirst)
    {
        cam->pitch = m_cur_x_rot;
        cam->yaw = m_cur_y_rot;
    }
}

void _BCL CCar::SVisualCamera::BoneCallbackX(CBoneInstance* B)
{
    CCar::SVisualCamera* P = static_cast<CCar::SVisualCamera*>(B->callback_param());
    B->mTransform.mulB_43(Fmatrix().rotateX(P->m_cur_x_rot));
}

void _BCL CCar::SVisualCamera::BoneCallbackY(CBoneInstance* B)
{
    CCar::SVisualCamera* P = static_cast<CCar::SVisualCamera*>(B->callback_param());
    B->mTransform.mulB_43(Fmatrix().rotateY(P->m_cur_y_rot));
}

void CCar::SVisualCamera::SetBoneCallbacks(bool val)
{
    CBoneInstance& biX = Car()->Visual()->dcast_PKinematics()->LL_GetBoneInstance(m_rotate_x_bone);
    CBoneInstance& biY = Car()->Visual()->dcast_PKinematics()->LL_GetBoneInstance(m_rotate_y_bone);
    if (val)
    {
        biX.set_callback(bctCustom, BoneCallbackX, this);
        biY.set_callback(bctCustom, BoneCallbackY, this);
    }
    else
    {
        biX.set_callback(bctPhysics, Car()->PPhysicsShell()->GetBonesCallback(), Car()->PPhysicsShell()->get_Element(m_rotate_x_bone));
        biY.set_callback(bctPhysics, Car()->PPhysicsShell()->GetBonesCallback(), Car()->PPhysicsShell()->get_Element(m_rotate_y_bone));
    }
}

void CCar::SVisualCamera::Activate(bool val)
{
    if (IsEnable() == false)
        return;
    if (m_active != val)
    {
        m_active = val;
        SetBoneCallbacks(m_active);
    }
}

void CCar::SVisualCamera::SetDesireAngle(Fvector vec)
{
    m_desire_ang.set(vec.x, vec.y, 0);
}

void CCar::SVisualCamera::OnMouseMove(int dx, int dy)
{
    float scale = (Car()->Camera()->f_fov / g_fov) * psMouseSens * psMouseSensScale / 50.0F;
    if (dx)
    {
        float d = float(dx) * scale;
        m_desire_ang.y -= d;
        clamp(m_desire_ang.y, m_lim_y_rot.x, m_lim_y_rot.y);
    }
    if (dx)
    {
        float d = float(dy) * scale * (psMouseInvert.test(1) ? -1 : 1) * psMouseSensVerticalK * 0.75F;
        m_desire_ang.x -= d;
        clamp(m_desire_ang.x, m_lim_x_rot.x, m_lim_x_rot.y);
    }
    if (Snap())
    {
        UpdateCam();
    }
}
#endif