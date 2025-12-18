#include "pch_script.h"
#include "GametaskManager.h"

using namespace luabind;

extern CGameTaskManager* get_task_manager();

#pragma optimize("s",on)
void CGameTaskManager::script_register(lua_State* L)
{
	module(L)
	[
		// register class
		class_<CGameTaskManager>("game_task_manager")
		.def("give_task", &CGameTaskManager::GiveGameTaskToActor),

		// register globals
		def("get_game_task_manager", get_task_manager)
	];
}
