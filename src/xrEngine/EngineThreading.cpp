#include "stdafx.h"
#include "EngineThreading.h"
#include "CustomHUD.h"
#include "IGame_Persistent.h"
#include "IGame_Level.h"
#include "Rain.h"

void XRay::Engine::PreRenderThread()
{
	PROF_THREAD("Secondary Task 1");

	{
		PROF_EVENT("seqParallelRender");
		for (auto& it : Device.seqParallelRender)
			it();
	}

	if (g_pGamePersistent && g_pGamePersistent->pEnvironment && g_pGamePersistent->pEnvironment->eff_Rain)
	{
		PROF_EVENT("CEffect_Rain::UpdateItems");
		g_pGamePersistent->pEnvironment->eff_Rain->UpdateItems();
	}

	if (Device.ParticleWorkerCallback)
	{
		PROF_EVENT("Process Particles");
		Device.ParticleWorkerCallback();
	}
}

void XRay::Engine::GameThread()
{
	PROF_THREAD("Secondary Task 2")
		
	// we has granted permission to execute
	if (g_hud)
	{
		PROF_EVENT("g_hud OnFrameMT");
		g_hud->OnFrameMT();
	}

	if (g_pGameLevel && g_pGameLevel->bReady)
	{
		PROF_EVENT("SoundEvent_Dispatch");
		g_pGameLevel->SoundEvent_Dispatch();
	}

	if (!Device.Paused())
	{
		PROF_EVENT("Sheduler");
		::Engine.Sheduler.Update();
	}

	{
		PROF_EVENT("seqParallel");
		for (u32 pit = 0; pit < Device.seqParallel.size(); pit++)
			Device.seqParallel[pit]();
		Device.seqParallel.clear();
	}

	{
		PROF_EVENT("seqFrameMT");
		Device.seqFrameMT.Process(rp_Frame);
	}
}
