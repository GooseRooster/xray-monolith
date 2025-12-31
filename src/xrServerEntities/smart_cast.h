#pragma once

#include <fast_dynamic_cast/fast_dynamic_cast.hpp>

// Helper to detect if a type has a dedicated cast method
// Specializations will be provided for known dcast methods
template<typename _To, typename _From>
struct has_dcast : std::false_type {};

#define DECLARE_SPECIALIZATION(TO, FROM, METHOD) \
template<> struct has_dcast<TO*, FROM*> : std::true_type { \
    static TO* cast(FROM* ptr) { return ptr ? ptr->METHOD() : nullptr; } \
};

#include "../include/xrRender/RenderVisual.h"
#include "../include/xrRender/Kinematics.h"
#include "../include/xrRender/KinematicsAnimated.h"
#include "../include/xrRender/ParticleCustom.h"

DECLARE_SPECIALIZATION(IKinematics, IRenderVisual, dcast_PKinematics)
DECLARE_SPECIALIZATION(IKinematicsAnimated, IRenderVisual, dcast_PKinematicsAnimated)
DECLARE_SPECIALIZATION(IKinematics, IKinematicsAnimated, dcast_PKinematics)
DECLARE_SPECIALIZATION(IKinematicsAnimated, IKinematics, dcast_PKinematicsAnimated)
DECLARE_SPECIALIZATION(IRenderVisual, IKinematics, dcast_RenderVisual)
DECLARE_SPECIALIZATION(IRenderVisual, IKinematicsAnimated, dcast_RenderVisual)
DECLARE_SPECIALIZATION(IParticleCustom, IRenderVisual, dcast_ParticleCustom)

// Project-specific specializations
#ifdef XRGAME_EXPORTS
#include "../xrGame/GameObject.h"
#include "../xrGame/InventoryOwner.h"
#include "../xrGame/inventory_item.h"
#include "../xrGame/Weapon.h"
#include "xrServer_Objects_ALife.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "../xrGame/AI_PhraseDialogManager.h"
#include "../xrGame/PhysicObject.h"
#include "../xrGame/level_changer.h"
#include "../xrGame/Spectator.h"
#include "../xrGame/UIPlayerItem.h"
#include "../xrGame/UITeamHeader.h"
#include "../xrGame/UITeamPanels.h"
#include "../xrGame/UITeamState.h"
#include "../xrGame/ai/stalker/ai_stalker.h"
#include "../xrGame/ai/trader/ai_trader.h"
#include "../xrGame/script_zone.h"
#include "../xrGame/ui/ArtefactDetectorUI.h"
#include "../xrGame/ui/ChangeWeatherDialog.hpp"
#include "../xrGame/ui/UIAchievements.h"
#include "../xrGame/ui/UIActorMenu.h"
#include "../xrGame/ui/UIActorStateInfo.h"
#include "../xrGame/ui/UIBoosterInfo.h"
#include "../xrGame/ui/UIBtnHint.h"
#include "../xrGame/ui/UIAnimatedStatic.h"
#include "../xrGame/ui/UIButton.h"
#include "../xrGame/ui/UIBuyWeaponTab.h"
#include "../xrGame/ui/UIBuyWndBase.h"
#include "../xrGame/ui/UICDkey.h"
#include "../xrGame/ui/UICellCustomItems.h"
#include "../xrGame/ui/UIChangeMap.h"
#include "../xrGame/ui/UICharacterInfo.h"
#include "../xrGame/ui/UIChatWnd.h"
#include "../xrGame/ui/UICheckButton.h"
#include "../xrGame/ui/UIComboBox.h"
#include "../xrGame/ui/UICustomSpin.h"
#include "../xrGame/ui/UIDebugFonts.h"
#include "../xrGame/ui/UIDemoPlayControl.h"
#include "../xrGame/ui/UIDoubleProgressBar.h"
#include "../xrGame/ui/UIDragDropListEx.h"
#include "../xrGame/ui/UIDragDropReferenceList.h"
#include "../xrGame/ui/UIEditBoxEx.h"
#include "../xrGame/ui/UIEditKeyBind.h"
#include "../xrGame/ui/UIFixedScrollBar.h"
#include "../xrGame/ui/UIGameLog.h"
#include "../xrGame/ui/UIHudStatesWnd.h"
#include "../xrGame/ui/UIInvUpgrade.h"
#include "../xrGame/ui/UIInvUpgradeInfo.h"
#include "../xrGame/ui/UILanimController.h"
#include "../xrGame/ui/UIListBox.h"
#include "../xrGame/ui/UIListBoxItem.h"
#include "../xrGame/ui/UIListBoxItemMsgChain.h"
#include "../xrGame/ui/UIListItemServer.h"
#include "../xrGame/ui/UILogsWnd.h"
#include "../xrGame/ui/UIMMShniaga.h"
#include "../xrGame/ui/UIMPAdminMenu.h"
#include "../xrGame/ui/UIMPChangeMapAdm.h"
#include "../xrGame/ui/UIMPPlayersAdm.h"
#include "../xrGame/ui/UIMPServerAdm.h"
#include "../xrGame/ui/UIMainIngameWnd.h"
#include "../xrGame/ui/UIMap.h"
#include "../xrGame/ui/UIMapDesc.h"
#include "../xrGame/ui/UIMapInfo.h"
#include "../xrGame/ui/UIMapLegend.h"
#include "../xrGame/ui/UIMapList.h"
#include "../xrGame/ui/UIMapWnd.h"
#include "../xrGame/ui/UIMessageBox.h"
#include "../xrGame/ui/UIMessageBoxEx.h"
#include "../xrGame/ui/UIMessagesWindow.h"
#include "../xrGame/ui/UIMoneyIndicator.h"
#include "../xrGame/ui/UIMotionIcon.h"
#include "../xrGame/ui/UIMpTradeWnd.h"
#include "../xrGame/ui/UINewsItemWnd.h"
#include "../xrGame/ui/UIOutfitInfo.h"
#include "../xrGame/ui/UIPdaKillMessage.h"
#include "../xrGame/ui/UIPdaMsgListItem.h"
#include "../xrGame/ui/UIPdaWnd.h"
#include "../xrGame/ui/UIProgressBar.h"
#include "../xrGame/ui/UIProgressShape.h"
#include "../xrGame/ui/UIRadioButton.h"
#include "../xrGame/ui/UIRankIndicator.h"
#include "../xrGame/ui/UIRankingWnd.h"
#include "../xrGame/ui/UIRankingsCoC.h"
#include "../xrGame/ui/UIScriptWnd.h"
#include "../xrGame/ui/UIScrollBar.h"
#include "../xrGame/ui/UIScrollBox.h"
#include "../xrGame/ui/UIScrollView.h"
#include "../xrGame/ui/UISecondTaskWnd.h"
#include "../xrGame/ui/UIServerInfo.h"
#include "../xrGame/ui/UISkinSelector.h"
#include "../xrGame/ui/UISpawnWnd.h"
#include "../xrGame/ui/UISpeechMenu.h"
#include "../xrGame/ui/UIStatic.h"
#include "../xrGame/ui/UIInvUpgradeProperty.h"
#include "../xrGame/ui/UIInventoryUpgradeWnd.h"
#include "../xrGame/ui/UIItemInfo.h"
#include "../xrGame/ui/UIKeyBinding.h"
#include "../xrGame/ui/UIKickPlayer.h"
#include "../xrGame/ui/UIStats.h"
#include "../xrGame/ui/UIStatsIcon.h"
#include "../xrGame/ui/UIStatsPlayerInfo.h"
#include "../xrGame/ui/UIStatsPlayerList.h"
#include "../xrGame/ui/UITabButtonMP.h"
#include "../xrGame/ui/UITalkDialogWnd.h"
#include "../xrGame/ui/UITalkWnd.h"
#include "../xrGame/ui/UITaskWnd.h"
#include "../xrGame/ui/UITrackBar.h"
#include "../xrGame/ui/UIVote.h"
#include "../xrGame/ui/UIVoteStatusWnd.h"
#include "../xrGame/ui/UIVotingCategory.h"
#include "../xrGame/ui/UIWpnParams.h"
#include "../xrGame/ui/ui_af_params.h"

class CObject;
class CGameObject;
class CEntity;
class CEntityAlive;
class CInventoryItem;
class CInventoryOwner;
class CHudItem;
class CFoodItem;
class CWeapon;
class CWeaponMagazined;
class CWeaponAmmo;
class CSE_ALifeTraderAbstract;
class CAI_PhraseDialogManager;

DECLARE_SPECIALIZATION(IRenderable, ISpatial, dcast_Renderable)
DECLARE_SPECIALIZATION(IRender_Light, ISpatial, dcast_Light)
DECLARE_SPECIALIZATION(CObject, ISpatial, dcast_CObject)
DECLARE_SPECIALIZATION(Feel::Sound, ISpatial, dcast_FeelSound)
DECLARE_SPECIALIZATION(CEntity, CGameObject, cast_entity)
DECLARE_SPECIALIZATION(CEntityAlive, CGameObject, cast_entity_alive)
DECLARE_SPECIALIZATION(CInventoryItem, CGameObject, cast_inventory_item)
DECLARE_SPECIALIZATION(CInventoryOwner, CGameObject, cast_inventory_owner)
DECLARE_SPECIALIZATION(CActor, CGameObject, cast_actor)
DECLARE_SPECIALIZATION(CGameObject, CInventoryOwner, cast_game_object)
DECLARE_SPECIALIZATION(CWeapon, CInventoryItem, cast_weapon)
DECLARE_SPECIALIZATION(CWeapon, CGameObject, cast_weapon)
DECLARE_SPECIALIZATION(CFoodItem, CInventoryItem, cast_food_item)
DECLARE_SPECIALIZATION(CMissile, CInventoryItem, cast_missile)
DECLARE_SPECIALIZATION(CFlashlight, CInventoryItem, cast_flashlight)
DECLARE_SPECIALIZATION(CCustomZone, CGameObject, cast_custom_zone)
DECLARE_SPECIALIZATION(CWeaponMagazined, CWeapon, cast_weapon_magazined)
DECLARE_SPECIALIZATION(CWeaponBinoculars, CWeapon, cast_weapon_binoculars)
DECLARE_SPECIALIZATION(CWeaponKnife, CWeapon, cast_weapon_knife)
DECLARE_SPECIALIZATION(CWeaponMagazinedWGrenade, CWeaponMagazined, cast_weapon_magazined_w_grenade)
DECLARE_SPECIALIZATION(CWeaponBM16, CWeaponMagazined, cast_weapon_bm16)
DECLARE_SPECIALIZATION(CWeaponRPG7, CWeaponMagazined, cast_weapon_rpg7)
DECLARE_SPECIALIZATION(CWeaponRG6, CWeaponMagazined, cast_weapon_rg6)
DECLARE_SPECIALIZATION(CHudItem, CInventoryItem, cast_hud_item)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CGameObject, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(IInputReceiver, CGameObject, cast_input_receiver)
DECLARE_SPECIALIZATION(CWeaponAmmo, CInventoryItem, cast_weapon_ammo)
DECLARE_SPECIALIZATION(CParticlesPlayer, CGameObject, cast_particles_player)
DECLARE_SPECIALIZATION(CArtefact, CGameObject, cast_artefact)
DECLARE_SPECIALIZATION(CCustomMonster, CGameObject, cast_custom_monster)
DECLARE_SPECIALIZATION(CAI_Stalker, CGameObject, cast_stalker)
DECLARE_SPECIALIZATION(CScriptEntity, CGameObject, cast_script_entity)
DECLARE_SPECIALIZATION(CSpaceRestrictor, CGameObject, cast_restrictor)
DECLARE_SPECIALIZATION(CExplosive, CGameObject, cast_explosive)
DECLARE_SPECIALIZATION(CGameObject, CAttachmentOwner, cast_game_object)
DECLARE_SPECIALIZATION(CGameObject, CInventoryItem, cast_game_object)
DECLARE_SPECIALIZATION(CAttachableItem, CGameObject, cast_attachable_item)
DECLARE_SPECIALIZATION(CHolderCustom, CGameObject, cast_holder_custom)
DECLARE_SPECIALIZATION(CAttachmentOwner, CGameObject, cast_attachment_owner)
DECLARE_SPECIALIZATION(CEatableItem, CInventoryItem, cast_eatable_item)
DECLARE_SPECIALIZATION(CBaseMonster, CGameObject, cast_base_monster)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_ALifeInventoryItem, cast_abstract)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_ALifeTraderAbstract, cast_abstract)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_ALifeGroupAbstract, cast_abstract)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_ALifeSchedulable, cast_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeGroupAbstract, CSE_Abstract, cast_group_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeSchedulable, CSE_Abstract, cast_schedulable)
DECLARE_SPECIALIZATION(CSE_ALifeInventoryItem, CSE_Abstract, cast_inventory_item)
DECLARE_SPECIALIZATION(CSE_ALifeTraderAbstract, CSE_Abstract, cast_trader_abstract)
DECLARE_SPECIALIZATION(CSE_Visual, CSE_Abstract, visual)
DECLARE_SPECIALIZATION(CSE_Motion, CSE_Abstract, motion)
DECLARE_SPECIALIZATION(ISE_Shape, CSE_Abstract, shape)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_PHSkeleton, cast_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeObject, CSE_Abstract, cast_alife_object)
DECLARE_SPECIALIZATION(CSE_ALifeDynamicObject, CSE_Abstract, cast_alife_dynamic_object)
DECLARE_SPECIALIZATION(CSE_ALifeItemAmmo, CSE_Abstract, cast_item_ammo)
DECLARE_SPECIALIZATION(CSE_ALifeItemWeapon, CSE_Abstract, cast_item_weapon)
DECLARE_SPECIALIZATION(CSE_ALifeItemDetector, CSE_Abstract, cast_item_detector)
DECLARE_SPECIALIZATION(CSE_ALifeMonsterAbstract, CSE_Abstract, cast_monster_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeHumanAbstract, CSE_Abstract, cast_human_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeAnomalousZone, CSE_Abstract, cast_anomalous_zone)
DECLARE_SPECIALIZATION(CSE_ALifeTrader, CSE_Abstract, cast_trader)
DECLARE_SPECIALIZATION(CSE_ALifeCreatureAbstract, CSE_Abstract, cast_creature_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeSmartZone, CSE_Abstract, cast_smart_zone)
DECLARE_SPECIALIZATION(CSE_ALifeOnlineOfflineGroup, CSE_Abstract, cast_online_offline_group)
DECLARE_SPECIALIZATION(CSE_ALifeItemPDA, CSE_Abstract, cast_item_pda)
DECLARE_SPECIALIZATION(CPhraseDialogManager, CAI_PhraseDialogManager, cast_phrase_dialog_manager)
DECLARE_SPECIALIZATION(CAttachmentOwner, CActor, cast_attachment_owner)
DECLARE_SPECIALIZATION(CInventoryOwner, CActor, cast_inventory_owner)
DECLARE_SPECIALIZATION(CGameObject, CActor, cast_game_object)
DECLARE_SPECIALIZATION(IInputReceiver, CActor, cast_input_receiver)
DECLARE_SPECIALIZATION(CEntityAlive, CActor, cast_entity_alive)
DECLARE_SPECIALIZATION(CEntity, CActor, cast_entity)
DECLARE_SPECIALIZATION(CPhraseDialogManager, CActor, cast_phrase_dialog_manager)
DECLARE_SPECIALIZATION(CEntity, CCar, cast_entity)
DECLARE_SPECIALIZATION(CGameObject, CCar, cast_game_object)
DECLARE_SPECIALIZATION(CExplosive, CCar, cast_explosive)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CCar, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CParticlesPlayer, CCar, cast_particles_player)
DECLARE_SPECIALIZATION(CScriptEntity, CCar, cast_script_entity)
DECLARE_SPECIALIZATION(IDamageSource, CCar, cast_IDamageSource)
DECLARE_SPECIALIZATION(CHolderCustom, CCar, cast_holder_custom)
DECLARE_SPECIALIZATION(CSpaceRestrictor, CCustomZone, cast_restrictor)
DECLARE_SPECIALIZATION(CGameObject, CCustomZone, cast_game_object)
DECLARE_SPECIALIZATION(CGameObject, CEntity, cast_game_object)
DECLARE_SPECIALIZATION(CUsableScriptObject, CGameObject, cast_usable_script_object)
DECLARE_SPECIALIZATION(CGameObject, CInventoryBox, cast_game_object)
DECLARE_SPECIALIZATION(CAttachmentOwner, CInventoryOwner, cast_attachment_owner)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CPhysicObject, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CParticlesPlayer, CPhysicsShellHolder, cast_particles_player)
DECLARE_SPECIALIZATION(CGameObject, CPhysicsShellHolder, cast_game_object)
DECLARE_SPECIALIZATION(CGameObject, CSpectator, cast_game_object)
DECLARE_SPECIALIZATION(IInputReceiver, CSpectator, cast_input_receiver)
DECLARE_SPECIALIZATION(CTorch, CInventoryItemObject, cast_torch)
DECLARE_SPECIALIZATION(CUIWindow, UIPlayerItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UITeamHeader, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UITeamPanels, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UITeamState, ui_cast_window)
DECLARE_SPECIALIZATION(CHolderCustom, CWeaponStatMgun, cast_holder_custom)
DECLARE_SPECIALIZATION(CGameObject, CWeaponStatMgun, cast_game_object)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CWeaponStatMgun, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CAttachmentOwner, CAI_Stalker, cast_attachment_owner)
DECLARE_SPECIALIZATION(CInventoryOwner, CAI_Stalker, cast_inventory_owner)
DECLARE_SPECIALIZATION(CEntityAlive, CAI_Stalker, cast_entity_alive)
DECLARE_SPECIALIZATION(CEntity, CAI_Stalker, cast_entity)
DECLARE_SPECIALIZATION(CGameObject, CAI_Stalker, cast_game_object)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CAI_Stalker, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CParticlesPlayer, CAI_Stalker, cast_particles_player)
DECLARE_SPECIALIZATION(CCustomMonster, CAI_Stalker, cast_custom_monster)
DECLARE_SPECIALIZATION(CScriptEntity, CAI_Stalker, cast_script_entity)
DECLARE_SPECIALIZATION(CPhraseDialogManager, CAI_Stalker, cast_phrase_dialog_manager)
DECLARE_SPECIALIZATION(CAI_PhraseDialogManager, CAI_Stalker, cast_ai_phrase_dialog_manager)
DECLARE_SPECIALIZATION(CAttachmentOwner, CAI_Trader, cast_attachment_owner)
DECLARE_SPECIALIZATION(CInventoryOwner, CAI_Trader, cast_inventory_owner)
DECLARE_SPECIALIZATION(CEntityAlive, CAI_Trader, cast_entity_alive)
DECLARE_SPECIALIZATION(CEntity, CAI_Trader, cast_entity)
DECLARE_SPECIALIZATION(CGameObject, CAI_Trader, cast_game_object)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CAI_Trader, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CParticlesPlayer, CAI_Trader, cast_particles_player)
DECLARE_SPECIALIZATION(CScriptEntity, CAI_Trader, cast_script_entity)
DECLARE_SPECIALIZATION(CPhraseDialogManager, CAI_Trader, cast_phrase_dialog_manager)
DECLARE_SPECIALIZATION(CAI_PhraseDialogManager, CAI_Trader, cast_ai_phrase_dialog_manager)
DECLARE_SPECIALIZATION(CInventoryItem, CAttachableItem, cast_inventory_item)
DECLARE_SPECIALIZATION(CTorch, CAttachableItem, cast_torch)
DECLARE_SPECIALIZATION(CInventoryItem, CEatableItem, cast_inventory_item)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CEatableItemObject, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CInventoryItem, CEatableItemObject, cast_inventory_item)
DECLARE_SPECIALIZATION(CAttachableItem, CEatableItemObject, cast_attachable_item)
DECLARE_SPECIALIZATION(CFoodItem, CEatableItemObject, cast_food_item)
DECLARE_SPECIALIZATION(CFlashlight, CEatableItemObject, cast_flashlight)
DECLARE_SPECIALIZATION(CGameObject, CEatableItemObject, cast_game_object)
DECLARE_SPECIALIZATION(CEatableItem, CEatableItemObject, cast_eatable_item)
DECLARE_SPECIALIZATION(CActor, CEntityAlive, cast_actor)
DECLARE_SPECIALIZATION(CAI_Stalker, CEntityAlive, cast_stalker)
DECLARE_SPECIALIZATION(CEntity, CEntityAlive, cast_entity)
DECLARE_SPECIALIZATION(CInventoryOwner, CEntityAlive, cast_inventory_owner)
DECLARE_SPECIALIZATION(CGameObject, CEntityAlive, cast_game_object)
DECLARE_SPECIALIZATION(CGameObject, CHelicopter, cast_game_object)
DECLARE_SPECIALIZATION(CExplosive, CHelicopter, cast_explosive)
DECLARE_SPECIALIZATION(CCar, CHolderCustom, cast_car)
DECLARE_SPECIALIZATION(CGameObject, CHolderCustom, cast_game_object)
DECLARE_SPECIALIZATION(CInventoryItem, CHudItemObject, cast_inventory_item)
DECLARE_SPECIALIZATION(CCustomDetector, CHudItemObject, cast_custom_detector)
DECLARE_SPECIALIZATION(CWeaponBinoculars, CHudItemObject, cast_weapon_binoculars)
DECLARE_SPECIALIZATION(CWeaponKnife, CHudItemObject, cast_weapon_knife)
DECLARE_SPECIALIZATION(CWeaponMagazined, CHudItemObject, cast_weapon_magazined)
DECLARE_SPECIALIZATION(CWeaponMagazinedWGrenade, CHudItemObject, cast_weapon_magazined_w_grenade)
DECLARE_SPECIALIZATION(CWeaponBM16, CHudItemObject, cast_weapon_bm16)
DECLARE_SPECIALIZATION(CWeapon, CHudItemObject, cast_weapon)
DECLARE_SPECIALIZATION(CWeaponRPG7, CHudItemObject, cast_weapon_rpg7)
DECLARE_SPECIALIZATION(CWeaponRG6, CHudItemObject, cast_weapon_rg6)
DECLARE_SPECIALIZATION(CHudItem, CHudItemObject, cast_hud_item)
DECLARE_SPECIALIZATION(CGrenade, CHudItemObject, cast_grenade)
DECLARE_SPECIALIZATION(CMissile, CHudItemObject, cast_missile)
DECLARE_SPECIALIZATION(CBolt, CHudItemObject, cast_bolt)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CHudItemObject, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CPhysicItem, CHudItemObject, cast_physics_item)
DECLARE_SPECIALIZATION(CAttachableItem, CInventoryItem, cast_attachable_item)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CInventoryItemObject, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CInventoryItem, CInventoryItemObject, cast_inventory_item)
DECLARE_SPECIALIZATION(CAttachableItem, CInventoryItemObject, cast_attachable_item)
DECLARE_SPECIALIZATION(CFlashlight, CInventoryItemObject, cast_flashlight)
DECLARE_SPECIALIZATION(CHudItem, CInventoryItemObject, cast_hud_item)
DECLARE_SPECIALIZATION(CGameObject, CInventoryItemObject, cast_game_object)
DECLARE_SPECIALIZATION(CCustomDetector, CInventoryItemObject, cast_custom_detector)
DECLARE_SPECIALIZATION(CWeaponBinoculars, CInventoryItemObject, cast_weapon_binoculars)
DECLARE_SPECIALIZATION(CWeaponKnife, CInventoryItemObject, cast_weapon_knife)
DECLARE_SPECIALIZATION(CWeaponMagazined, CInventoryItemObject, cast_weapon_magazined)
DECLARE_SPECIALIZATION(CWeaponMagazinedWGrenade, CInventoryItemObject, cast_weapon_magazined_w_grenade)
DECLARE_SPECIALIZATION(CWeaponBM16, CInventoryItemObject, cast_weapon_bm16)
DECLARE_SPECIALIZATION(CWeapon, CInventoryItemObject, cast_weapon)
DECLARE_SPECIALIZATION(CWeaponRPG7, CInventoryItemObject, cast_weapon_rpg7)
DECLARE_SPECIALIZATION(CWeaponRG6, CInventoryItemObject, cast_weapon_rg6)
DECLARE_SPECIALIZATION(CBolt, CInventoryItemObject, cast_bolt)
DECLARE_SPECIALIZATION(CPda, CInventoryItemObject, cast_pda)
DECLARE_SPECIALIZATION(CGrenade, CInventoryItemObject, cast_grenade)
DECLARE_SPECIALIZATION(CMissile, CInventoryItemObject, cast_missile)
DECLARE_SPECIALIZATION(CSilencer, CInventoryItemObject, cast_addon_silencer)
DECLARE_SPECIALIZATION(CScope, CInventoryItemObject, cast_addon_scope)
DECLARE_SPECIALIZATION(CGrenadeLauncher, CInventoryItemObject, cast_addon_grenade_launcher)
DECLARE_SPECIALIZATION(CPhysicItem, CInventoryItemObject, cast_physics_item)
DECLARE_SPECIALIZATION(CGameObject, CLevelChanger, cast_game_object)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CPhysicItem, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CSpaceRestrictor, CScriptZone, cast_restrictor)
DECLARE_SPECIALIZATION(CGameObject, CScriptZone, cast_game_object)
DECLARE_SPECIALIZATION(CScriptEntity, CProjector, cast_script_entity)
DECLARE_SPECIALIZATION(CGameObject, CProjector, cast_game_object)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CProjector, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CCustomZone, CSpaceRestrictor, cast_custom_zone)
DECLARE_SPECIALIZATION(CGameObject, CSpaceRestrictor, cast_game_object)
DECLARE_SPECIALIZATION(CScriptZone, CSpaceRestrictor, cast_script_zone)
DECLARE_SPECIALIZATION(CTeamBaseZone, CGameObject, cast_team_base_zone)
DECLARE_SPECIALIZATION(CUIWindow, CUIDetectorWave, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIArtefactDetectorElite, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, ChangeWeatherDialog, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, ChangeGameTypeDialog, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUI3tButton, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIAchievements, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIActorMenu, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, ui_actor_state_wnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, ui_actor_state_item, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIAnimatedStatic, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIAnimatedStatic, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUISleepStatic, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUISleepStatic, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIBoosterInfo, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UIBoosterInfoItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIButtonHint, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIButton, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIButton, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIBuyWeaponTab, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, IBuyWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUICDkey, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMPPlayerName, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIInventoryCellItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIInventoryCellItem, ui_cast_static)
DECLARE_SPECIALIZATION(CUICellItem, CUIInventoryCellItem, ui_cast_cell_item)
DECLARE_SPECIALIZATION(CUIWindow, CUIAmmoCellItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIAmmoCellItem, ui_cast_static)
DECLARE_SPECIALIZATION(CUICellItem, CUIAmmoCellItem, ui_cast_cell_item)
DECLARE_SPECIALIZATION(CUIWindow, CUIWeaponCellItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIWeaponCellItem, ui_cast_static)
DECLARE_SPECIALIZATION(CUICellItem, CUIWeaponCellItem, ui_cast_cell_item)
DECLARE_SPECIALIZATION(CUIWindow, CUICellItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUICellItem, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIChangeMap, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUICharacterInfo, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIChatWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUICheckButton, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIComboBox, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUICustomEdit, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUICustomEdit, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUICustomSpin, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIDebugFonts, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIDemoPlayControl, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIDialogWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIDoubleProgressBar, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIDragDropListEx, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUICellContainer, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIDragDropReferenceList, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIEditBox, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIEditBox, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIEditBoxEx, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIEditBoxEx, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIEditKeyBind, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIEditKeyBind, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIFixedScrollBar, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIFrameLineWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIFrameWindow, ui_cast_window)
DECLARE_SPECIALIZATION(ITextureOwner, CUIFrameWindow, ui_cast_texture_owner)
DECLARE_SPECIALIZATION(CUIWindow, CUIGameLog, ui_cast_window)
DECLARE_SPECIALIZATION(CUIScrollView, CUIGameLog, ui_cast_scroll_view)
DECLARE_SPECIALIZATION(CUIWindow, UIHint, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UIHintWindow, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIHudStatesWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUI_IB_FrameLineWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUI_IB_FrameWindow, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UIUpgrade, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIUpgradePoint, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIUpgradePoint, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, UIInvUpgradeInfo, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIInventoryUpgradeWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIItemInfo, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIKeyBinding, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIKickPlayer, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UIInvUpgPropertiesWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UIProperty, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIColorAnimConrollerContainer, ui_cast_window)
DECLARE_SPECIALIZATION(CUILightAnimColorConroller, CUIColorAnimConrollerContainer, ui_cast_light_anim_color_controller)
DECLARE_SPECIALIZATION(CUIWindow, CUIListBox, ui_cast_window)
DECLARE_SPECIALIZATION(CUIScrollView, CUIListBox, ui_cast_scroll_view)
DECLARE_SPECIALIZATION(CUIWindow, CUIListBoxItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIListBoxItem, CUIListBoxItem, ui_cast_list_box_item)
DECLARE_SPECIALIZATION(CUISelectable, CUIListBoxItem, ui_cast_selectable)
DECLARE_SPECIALIZATION(CUIWindow, CUIListBoxItemMsgChain, ui_cast_window)
DECLARE_SPECIALIZATION(CUIListBoxItem, CUIListBoxItemMsgChain, ui_cast_list_box_item)
DECLARE_SPECIALIZATION(CUIWindow, CUIListItemServer, ui_cast_window)
DECLARE_SPECIALIZATION(CUIListBoxItem, CUIListItemServer, ui_cast_list_box_item)
DECLARE_SPECIALIZATION(CUIListItemServer, CUIListItemServer, ui_cast_list_item_server)
DECLARE_SPECIALIZATION(CUIWindow, CUILogsWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMMShniaga, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMpAdminMenu, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMpChangeMapAdm, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMpPlayersAdm, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMpServerAdm, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMainIngameWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUICustomMap, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUICustomMap, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIMapDesc, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMapInfo, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UIMapLegend, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UIMapLegendItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMapList, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMapWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMessageBox, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIMessageBox, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIMessageBoxEx, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMessagesWindow, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMoneyIndicator, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMotionIcon, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIMpTradeWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUINewsItemWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIOutfitImmunity, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIOutfitInfo, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIPdaKillMessage, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIPdaMsgListItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIPdaWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIProgressBar, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIProgressShape, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIProgressShape, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIRadioButton, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIRankIndicator, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIRankingWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIRankingsCoC, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIDialogWndEx, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIScrollBar, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIScrollBox, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIScrollView, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UITaskListWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UITaskListWndItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIServerInfo, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUISkinSelectorWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUISpawnWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUISpeechMenu, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIStatic, ui_cast_window)
DECLARE_SPECIALIZATION(ITextureOwner, CUIStatic, ui_cast_texture_owner)
DECLARE_SPECIALIZATION(CUILightAnimColorConroller, CUIStatic, ui_cast_light_anim_color_controller)
DECLARE_SPECIALIZATION(CUIWindow, CUITextWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUILightAnimColorConroller, CUITextWnd, ui_cast_light_anim_color_controller)
DECLARE_SPECIALIZATION(CUIWindow, CUIStats, ui_cast_window)
DECLARE_SPECIALIZATION(CUIScrollView, CUIStats, ui_cast_scroll_view)
DECLARE_SPECIALIZATION(CUIWindow, CUIStatsIcon, ui_cast_window)
DECLARE_SPECIALIZATION(CUIStatic, CUIStatsIcon, ui_cast_static)
DECLARE_SPECIALIZATION(CUIWindow, CUIStatsPlayerInfo, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIStatsPlayerList, ui_cast_window)
DECLARE_SPECIALIZATION(CUIScrollView, CUIStatsPlayerList, ui_cast_scroll_view)
DECLARE_SPECIALIZATION(CUIWindow, CUITabButton, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUITabButtonMP, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUITabControl, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUITalkDialogWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIQuestionItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIAnswerItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUITalkWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUITaskWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUITaskItem, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUITrackBar, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIVote, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UIVoteStatusWnd, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIVotingCategory, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIWpnParams, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIConditionParams, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUI_IB_Static, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, CUIArtefactParams, ui_cast_window)
DECLARE_SPECIALIZATION(CUIWindow, UIArtefactParamItem, ui_cast_window)

template<>
struct has_dcast<CGameObject*, CObject*> : std::true_type {
    static CGameObject* cast(CObject* ptr) {
		return static_cast<CGameObject*>(ptr);
    }
};
#endif

#ifdef XRPHYSICS_EXPORTS
#include "../xrCDB/ISpatial.h"

DECLARE_SPECIALIZATION(IRenderable, ISpatial, dcast_Renderable)
DECLARE_SPECIALIZATION(IRender_Light, ISpatial, dcast_Light)
DECLARE_SPECIALIZATION(CObject, ISpatial, dcast_CObject)
#endif

#if defined(XRRENDER_R1_EXPORTS) || defined(XRRENDER_R2_EXPORTS) || defined(XRRENDER_R3_EXPORTS) || defined(XRRENDER_R4_EXPORTS)
#include "../xrCDB/ISpatial.h"

DECLARE_SPECIALIZATION(IRenderable, ISpatial, dcast_Renderable)
DECLARE_SPECIALIZATION(IRender_Light, ISpatial, dcast_Light)
DECLARE_SPECIALIZATION(CObject, ISpatial, dcast_CObject)
#endif

#undef DECLARE_SPECIALIZATION

template < typename _Ty >
using clean_type_t = typename std::remove_cv_t<std::remove_reference_t<std::remove_pointer_t< _Ty >>>;

template < typename _To, typename _From >
__forceinline _To smart_cast(_From* ptr)
{
    // Try dedicated cast function if available
    if constexpr (has_dcast<_To, _From*>::value) {
        return has_dcast<_To, _From*>::cast(ptr);
    }

	return fast_dynamic_cast<_To>(ptr);
};

// const T*
template < typename _To, typename _From >
__forceinline const _To smart_cast(const _From* ptr)
{
    _From* nonconst_ptr = const_cast<_From*>(ptr);
    _To casted_ptr = smart_cast<_To>(nonconst_ptr);
    return const_cast<const _To>(casted_ptr);
};

// T&
template < typename _To, typename _From, typename = std::enable_if_t<!std::is_same<clean_type_t<_To>, clean_type_t<_From&>>::value>>
__forceinline _To smart_cast(_From& ref)
{
    using _ToPtr = std::add_pointer_t<std::remove_reference_t<_To>>;
    auto casted_ptr = smart_cast<_ToPtr>(&ref);
    if (!casted_ptr)
        throw std::bad_cast{};
    return *casted_ptr;
};

// const T&
template < typename _To, typename _From, typename = std::enable_if_t<!std::is_same<clean_type_t<_To>, clean_type_t<_From&>>::value>>
__forceinline _To smart_cast(const _From& ref)
{
    using _ToPtr = std::add_pointer_t<std::remove_reference_t<_To>>;
    auto casted_ptr = smart_cast<_ToPtr>(const_cast<_From*>(&ref));
    if (!casted_ptr)
        throw std::bad_cast{};
    return *casted_ptr;
};

// T -> T
template < typename _To >
__forceinline _To smart_cast(_To ptr) { return ptr; }
