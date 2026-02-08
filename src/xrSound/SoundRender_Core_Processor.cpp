#include "stdafx.h"
#pragma hdrstop

#include "cl_intersect.h"
#include "SoundRender_Core.h"
#include "SoundRender_Emitter.h"
#include "SoundRender_TargetA.h"
#include "SoundRender_Source.h"
#include "SoundRender_CoreA.h"

// Steam Audio
#include "SteamAudio/SteamAudioSimulator.h"
#include "SteamAudio/SteamAudioReverb.h"
#include "SteamAudio/SteamAudioScene.h"
#include "SteamAudio/SteamAudioSource.h"

extern float psSA_ReverbUpdateRate;
extern int psSA_Convolution;

CSoundRender_Emitter* CSoundRender_Core::i_play(ref_sound* S, BOOL _loop, float delay)
{
	VERIFY(S->_p->feedback==0);
	CSoundRender_Emitter* E = xr_new<CSoundRender_Emitter>();
	S->_p->feedback = E;
	E->start(S, _loop, delay);
	s_emitters.push_back(E);
	return E;
}

void CSoundRender_Core::update(const Fvector& P, const Fvector& D, const Fvector& N)
{
	u32 it;

	if (0 == bReady) return;
	bLocked = TRUE;
	float new_tm = Timer.GetElapsed_sec();
	fTimer_Delta = new_tm - fTimer_Value;
	//.	float dt					= float(Timer_Delta)/1000.f;
	float dt_sec = fTimer_Delta;
	fTimer_Value = new_tm;

	s_emitters_u ++;

	// Steam Audio: Request simulation FIRST, wait for results, THEN process sources
	// This fixes the timing bug where outputs were fetched before simulation completed
	if (SoundRenderA && SoundRenderA->IsSteamAudioEnabled())
	{
		CSteamAudioSimulator* simulator = SoundRenderA->GetSteamSimulator();
		if (simulator)
		{
			// Request direct simulation (occlusion/transmission) - runs every frame
			simulator->RequestDirectSimulation();

			// Consume results from the PREVIOUS frame's simulation (if ready).
			// No wait: sources have safe "no effect" defaults, so the first frame
			// without results is imperceptible. The old Sleep(1) spin-wait stalled
			// the main thread for up to 75ms due to Windows timer resolution (~15ms).
			if (simulator->IsDirectResultReady())
			{
				simulator->ConsumeDirectResult();
			}

			// Request reflections simulation less frequently (configurable interval)
			// Reverb doesn't need to update every frame - it's expensive
			// Member variable resets naturally when fTimer_Value resets on level load
			if (fTimer_Value - m_lastReflectionsRequest > psSA_ReverbUpdateRate)
			{
				simulator->RequestReflectionsSimulation();
				m_lastReflectionsRequest = fTimer_Value;
			}
			// Note: We don't wait for reflections - they can lag by a frame
			// This is acceptable because reverb is perceptually tolerant of latency
		}

		// Flush any pending simulator commits from source adds/removes last frame
		// This batches commits for better performance vs per-source commits
		CSteamAudioScene* scene = SoundRenderA->GetSteamScene();
		if (scene)
		{
			scene->FlushCommit();
		}

		// Update reverb probe with latest simulation outputs
		CSteamAudioReverb* reverb = SoundRenderA->GetSteamReverb();
		if (reverb && reverb->IsInitialized() && psSoundFlags.test(ss_SA_Reverb))
		{
			reverb->UpdateProbe(dt_sec);
		}


	}

	// Firstly update emitters, which are now being rendered
	//Msg	("! update: r-emitters");
	for (it = 0; it < s_targets.size(); it++)
	{
		CSoundRender_Target* T = s_targets[it];
		CSoundRender_Emitter* E = T->get_emitter();
		if (E)
		{
			E->update(dt_sec);
			E->marker = s_emitters_u;
			E = T->get_emitter(); // update can stop itself
			if (E) T->priority = E->priority();
			else T->priority = -1;
		}
		else
		{
			T->priority = -1;
		}
	}

	// Update emmitters
	//Msg	("! update: emitters");
	for (it = 0; it < s_emitters.size(); it++)
	{
		CSoundRender_Emitter* pEmitter = s_emitters[it];
		if (pEmitter->marker != s_emitters_u)
		{
			pEmitter->update(dt_sec);
			pEmitter->marker = s_emitters_u;
		}
		if (!pEmitter->isPlaying())
		{
			// Stopped
			xr_delete(pEmitter);
			s_emitters.erase(s_emitters.begin() + it);
			it--;
		}
	}

	// Get currently rendering emitters
	//Msg	("! update: targets");
	s_targets_defer.clear();
	s_targets_pu ++;
	// u32 PU				= s_targets_pu%s_targets.size();
	for (it = 0; it < s_targets.size(); it++)
	{
		CSoundRender_Target* T = s_targets[it];
		if (T->get_emitter())
		{
			// Has emmitter, maybe just not started rendering
			if (T->get_Rendering())
			{
				/*if	(PU == it)*/
				T->fill_parameters();
				T->update();
			}
			else
				s_targets_defer.push_back(T);
		}
	}

	// Commit parameters from pending targets
	if (!s_targets_defer.empty())
	{
		//Msg	("! update: start render - commit");
		s_targets_defer.erase(std::unique(s_targets_defer.begin(), s_targets_defer.end()), s_targets_defer.end());
		for (it = 0; it < s_targets_defer.size(); it++)
			s_targets_defer[it]->fill_parameters();
	}

	// update EFX
	if (m_is_supported)
	{
		bool saReverbActive = SoundRenderA && SoundRenderA->IsSteamAudioEnabled()
			&& psSoundFlags.test(ss_SA_Reverb);

		if (saReverbActive)
		{
			// Steam Audio reverb probe drives ALL environment parameters.
			// Bypass the baked environment lerp entirely — SA's internal
			// exponential smoothing handles transitions.
			CSteamAudioReverb* reverb = SoundRenderA->GetSteamReverb();
			if (reverb && reverb->IsInitialized() && reverb->HasValidData())
			{
				reverb->GetEnvironment(e_current);
			}
			// else: keep e_current as-is (identity/last known good) until probe warms up
		}
		else
		{
			// Baked environment path: interpolate from e_current toward SDK-authored target
			if (bListenerMoved)
			{
				bListenerMoved = FALSE;
				e_target_ptr = get_environment(P);
				if (!e_target_ptr)
					e_target_ptr = &e_identity;
			}

			constexpr float percent = 0.95f;
			float alpha = 1.0f - std::exp(std::log(1.0f - percent) * dt_sec / snd_efx_environment_change_time);
			clamp(alpha, 0.f, 1.f);
			e_current.lerp(e_current, *e_target_ptr, alpha);
		}

		set_listener(e_current);
		commit();
	}

	// update listener
	update_listener(P, D, N, dt_sec);

	// Note: Steam Audio simulation request moved to START of update() for proper synchronization

	// Start rendering of pending targets
	if (!s_targets_defer.empty())
	{
		CSoundRender_CoreA* Core = (CSoundRender_CoreA*)this;
		//Msg	("! update: start render");
		for (it = 0; it < s_targets_defer.size(); it++)
		{
			CSoundRender_TargetA* Ptr = (CSoundRender_TargetA*)s_targets_defer[it];
			if (m_is_supported)
				Ptr->SetSlot(Core->slot);
			Ptr->render();
		}
	}

	// Convolution reverb: convolve accumulated source mix with IR and stream to OpenAL
	if (SoundRenderA && SoundRenderA->IsSteamAudioEnabled() && psSA_Convolution)
	{
		CSteamAudioReverb* reverb = SoundRenderA->GetSteamReverb();
		if (reverb && reverb->IsConvolutionActive())
		{
			reverb->UpdateConvolution();
		}
	}

	// Events
	update_events();

	bLocked = FALSE;
}

static u32 g_saved_event_count = 0;

void CSoundRender_Core::update_events()
{
	g_saved_event_count = s_events.size();
	for (u32 it = 0; it < s_events.size(); it++)
	{
		event& E = s_events[it];
		Handler(E.first, E.second);
	}
	s_events.clear_not_free();
}

void CSoundRender_Core::statistic(CSound_stats* dest, CSound_stats_ext* ext)
{
	if (dest)
	{
		dest->_rendered = 0;
		for (u32 it = 0; it < s_targets.size(); it++)
		{
			CSoundRender_Target* T = s_targets[it];
			if (T->get_emitter() && T->get_Rendering()) dest->_rendered++;
		}
		dest->_simulated = s_emitters.size();
		dest->_cache_hits = cache._stat_hit;
		dest->_cache_misses = cache._stat_miss;
		dest->_events = g_saved_event_count;
		cache.stats_clear();
	}
	if (ext)
	{
		for (u32 it = 0; it < s_emitters.size(); it++)
		{
			CSoundRender_Emitter* _E = s_emitters[it];
			CSound_stats_ext::SItem _I;
			_I._3D = !_E->b2D;
			_I._rendered = !!_E->target;
			_I.params = _E->p_source;
			_I.volume = _E->smooth_volume;
			if (_E->owner_data)
			{
				_I.name = _E->source()->fname;
				_I.game_object = _E->owner_data->g_object;
				_I.game_type = _E->owner_data->g_type;
				_I.type = _E->owner_data->s_type;
			}
			else
			{
				_I.game_object = 0;
				_I.game_type = 0;
				_I.type = st_Effect;
			}
			ext->append(_I);
		}
	}
}


float CSoundRender_Core::get_occlusion_to(const Fvector& hear_pt, const Fvector& snd_pt, float dispersion)
{
	float occ_value = 1.f;

	if (0 != geom_SOM)
	{
		// Calculate RAY params
		Fvector pos, dir;
		pos.random_dir();
		pos.mul(dispersion);
		pos.add(snd_pt);
		dir.sub(pos, hear_pt);
		float range = dir.magnitude();
		dir.div(range);

#ifdef _EDITOR
		ETOOLS::ray_options		(CDB::OPT_CULL);
		ETOOLS::ray_query		(geom_SOM,hear_pt,dir,range);
		u32 r_cnt				= ETOOLS::r_count();
		CDB::RESULT*	_B 		= ETOOLS::r_begin();
#else
		geom_DB.ray_options(CDB::OPT_CULL);
		geom_DB.ray_query(geom_SOM, hear_pt, dir, range);
		u32 r_cnt = geom_DB.r_count();
		CDB::RESULT* _B = geom_DB.r_begin();
#endif
		if (0 != r_cnt)
		{
			for (u32 k = 0; k < r_cnt; k++)
			{
				CDB::RESULT* R = _B + k;
				occ_value *= *(float*)&R->dummy;
			}
		}
	}
	return occ_value;
}

float CSoundRender_Core::get_occlusion(Fvector& P, float R, Fvector* occ)
{
	float occ_value = 1.f;

	// Calculate RAY params
	Fvector base = listener_position();
	Fvector pos, dir;
	float range;
	pos.random_dir();
	pos.mul(R);
	pos.add(P);
	dir.sub(pos, base);
	range = dir.magnitude();
	dir.div(range);

	if (0 != geom_MODEL)
	{
		bool bNeedFullTest = true;
		// 1. Check cached polygon
		float _u, _v, _range;
		if (CDB::TestRayTri(base, dir, occ, _u, _v, _range, true))
			if (_range > 0 && _range < range)
			{
				occ_value = psSoundOcclusionScale;
				bNeedFullTest = false;
			}
		// 2. Polygon doesn't picked up - real database query
		if (bNeedFullTest)
		{
#ifdef _EDITOR
			ETOOLS::ray_options		(CDB::OPT_ONLYNEAREST);
			ETOOLS::ray_query		(geom_MODEL,base,dir,range);
			if (0!=ETOOLS::r_count()){ 
				// cache polygon
				const CDB::RESULT*	R = ETOOLS::r_begin			();
#else
			geom_DB.ray_options(CDB::OPT_ONLYNEAREST);
			geom_DB.ray_query(geom_MODEL, base, dir, range);
			if (0 != geom_DB.r_count())
			{
				// cache polygon
				const CDB::RESULT* R = geom_DB.r_begin();
#endif
				const CDB::TRI& T = geom_MODEL->get_tris()[R->id];
				const Fvector* V = geom_MODEL->get_verts();
				occ[0].set(V[T.verts[0]]);
				occ[1].set(V[T.verts[1]]);
				occ[2].set(V[T.verts[2]]);
				occ_value = psSoundOcclusionScale;
			}
		}
	}
	if (0 != geom_SOM)
	{
#ifdef _EDITOR
		ETOOLS::ray_options		(CDB::OPT_CULL);
		ETOOLS::ray_query		(geom_SOM,base,dir,range);
		u32 r_cnt				= ETOOLS::r_count();
        CDB::RESULT*	_B 		= ETOOLS::r_begin();
#else
		geom_DB.ray_options(CDB::OPT_CULL);
		geom_DB.ray_query(geom_SOM, base, dir, range);
		u32 r_cnt = geom_DB.r_count();
		CDB::RESULT* _B = geom_DB.r_begin();
#endif
		if (0 != r_cnt)
		{
			for (u32 k = 0; k < r_cnt; k++)
			{
				CDB::RESULT* R = _B + k;
				occ_value *= *(float*)&R->dummy;
			}
		}
	}
	return occ_value;
}
