#include "stdafx.h"
#include "SteamAudioReverb.h"
#include "SteamAudio.h"
#include "SteamAudioScene.h"
#include "SteamAudioSource.h"
#include "../SoundRender_CoreA.h"
#include "../SoundRender_Environment.h"
#include "../SoundRender.h"

CSteamAudioReverb::CSteamAudioReverb()
{
}

CSteamAudioReverb::~CSteamAudioReverb()
{
    Destroy();
}

bool CSteamAudioReverb::Initialize(CSteamAudioScene* scene)
{
    if (!scene || !scene->IsReady())
    {
        Msg("! STEAM_AUDIO: Cannot initialize reverb - scene not ready");
        return false;
    }

    IPLSimulator simulator = scene->GetSimulator();
    m_simulator = simulator;

    // --- Create listener probe source ---
    // Persistent IPLSource at the listener position, runs reflections in PARAMETRIC mode.
    // The simulator traces rays and extracts 3-band RT60 values from the simulated field.
    IPLSourceSettings sourceSettings = {};
    sourceSettings.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;

    IPLerror error = iplSourceCreate(simulator, &sourceSettings, &m_listenerSource);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create listener probe source (error: %d)", error);
        return false;
    }

    iplSourceAdd(m_listenerSource, simulator);
    scene->MarkPendingCommit();

    // Initialize listener probe inputs
    m_listenerInputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
    m_listenerInputs.reverbScale[0] = 1.0f;
    m_listenerInputs.reverbScale[1] = 1.0f;
    m_listenerInputs.reverbScale[2] = 1.0f;
    m_listenerInputs.hybridReverbTransitionTime = 1.0f;
    m_listenerInputs.hybridReverbOverlapPercent = 0.25f;

    m_listenerInputs.source.origin = {0.0f, 0.0f, 0.0f};
    m_listenerInputs.source.ahead = {0.0f, 0.0f, -1.0f};
    m_listenerInputs.source.up = {0.0f, 1.0f, 0.0f};
    m_listenerInputs.source.right = {1.0f, 0.0f, 0.0f};

    iplSourceSetInputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &m_listenerInputs);

    m_hasValidData = false;
    m_listenerOutputs.reflections.reverbTimes[0] = 0.0f;
    m_listenerOutputs.reflections.reverbTimes[1] = 0.0f;
    m_listenerOutputs.reflections.reverbTimes[2] = 0.0f;

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Listener reverb probe initialized (PARAMETRIC mode, full EFX mapping)");

    return true;
}

void CSteamAudioReverb::Destroy()
{
    if (m_listenerSource)
    {
        if (m_simulator)
        {
            iplSourceRemove(m_listenerSource, m_simulator);
            CSteamAudioScene* scene = nullptr;
            if (SoundRenderA && SoundRenderA->IsSteamAudioEnabled())
            {
                scene = SoundRenderA->GetSteamScene();
                if (scene)
                    scene->MarkPendingCommit();
            }
        }
        iplSourceRelease(&m_listenerSource);
        m_listenerSource = nullptr;
    }
    m_simulator = nullptr;
    m_hasValidData = false;
}

void CSteamAudioReverb::UpdateListenerProbe(const Fvector& pos, const Fvector& dir, const Fvector& up)
{
    if (!m_listenerSource)
        return;

    m_listenerInputs.source.origin.x = pos.x;
    m_listenerInputs.source.origin.y = pos.y;
    m_listenerInputs.source.origin.z = -pos.z;

    m_listenerInputs.source.ahead.x = dir.x;
    m_listenerInputs.source.ahead.y = dir.y;
    m_listenerInputs.source.ahead.z = -dir.z;

    m_listenerInputs.source.up.x = up.x;
    m_listenerInputs.source.up.y = up.y;
    m_listenerInputs.source.up.z = -up.z;

    Fvector right;
    right.crossproduct(up, dir);
    m_listenerInputs.source.right.x = right.x;
    m_listenerInputs.source.right.y = right.y;
    m_listenerInputs.source.right.z = -right.z;

    iplSourceSetInputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &m_listenerInputs);
}

// Helper: exponential smoothing toward target
static float smooth(float current, float target, float rate, float dt)
{
    float alpha = 1.0f - expf(-rate * dt);
    return current + (target - current) * alpha;
}

void CSteamAudioReverb::UpdateProbe(float dt)
{
    if (!m_listenerSource)
        return;

    iplSourceGetOutputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &m_listenerOutputs);

    // In PARAMETRIC mode, there's no IR — check if reverbTimes are nonzero
    bool valid = (m_listenerOutputs.reflections.reverbTimes[0] > 0.001f ||
                  m_listenerOutputs.reflections.reverbTimes[1] > 0.001f ||
                  m_listenerOutputs.reflections.reverbTimes[2] > 0.001f);

    if (valid && !m_hasValidData)
    {
        m_hasValidData = true;
        Msg("STEAM_AUDIO: Listener probe first valid result — reverbTimes: [%.3f, %.3f, %.3f]",
            m_listenerOutputs.reflections.reverbTimes[0],
            m_listenerOutputs.reflections.reverbTimes[1],
            m_listenerOutputs.reflections.reverbTimes[2]);
    }

    if (!valid)
        return;

    // Compute raw parameters from current reverbTimes
    DeriveParameters();

    // Smooth all parameters toward raw values.
    // Rate of 3/s → ~63% convergence in 333ms, ~95% in 1s. Fast enough to respond
    // to room changes but slow enough to prevent jarring pops during transitions.
    constexpr float RATE = 3.0f;

    m_smoothedParams.DecayTime        = smooth(m_smoothedParams.DecayTime,        m_rawParams.DecayTime,        RATE, dt);
    m_smoothedParams.DecayHFRatio     = smooth(m_smoothedParams.DecayHFRatio,     m_rawParams.DecayHFRatio,     RATE, dt);
    m_smoothedParams.DecayLFRatio     = smooth(m_smoothedParams.DecayLFRatio,     m_rawParams.DecayLFRatio,     RATE, dt);
    m_smoothedParams.Room             = smooth(m_smoothedParams.Room,             m_rawParams.Room,             RATE, dt);
    m_smoothedParams.RoomHF           = smooth(m_smoothedParams.RoomHF,           m_rawParams.RoomHF,           RATE, dt);
    m_smoothedParams.RoomLF           = smooth(m_smoothedParams.RoomLF,           m_rawParams.RoomLF,           RATE, dt);
    m_smoothedParams.Density          = smooth(m_smoothedParams.Density,          m_rawParams.Density,          RATE, dt);
    m_smoothedParams.Diffusion        = smooth(m_smoothedParams.Diffusion,        m_rawParams.Diffusion,        RATE, dt);
    m_smoothedParams.Reflections      = smooth(m_smoothedParams.Reflections,      m_rawParams.Reflections,      RATE, dt);
    m_smoothedParams.ReflectionsDelay = smooth(m_smoothedParams.ReflectionsDelay, m_rawParams.ReflectionsDelay, RATE, dt);
    m_smoothedParams.Reverb           = smooth(m_smoothedParams.Reverb,           m_rawParams.Reverb,           RATE, dt);
    m_smoothedParams.ReverbDelay      = smooth(m_smoothedParams.ReverbDelay,      m_rawParams.ReverbDelay,      RATE, dt);
    m_smoothedParams.EchoTime         = smooth(m_smoothedParams.EchoTime,         m_rawParams.EchoTime,         RATE, dt);
    m_smoothedParams.EchoDepth        = smooth(m_smoothedParams.EchoDepth,        m_rawParams.EchoDepth,        RATE, dt);
    m_smoothedParams.AirAbsorptionHF  = smooth(m_smoothedParams.AirAbsorptionHF,  m_rawParams.AirAbsorptionHF,  RATE, dt);

    m_smoothedParams.ModulationTime   = smooth(m_smoothedParams.ModulationTime,   m_rawParams.ModulationTime,   RATE, dt);
    m_smoothedParams.ModulationDepth  = smooth(m_smoothedParams.ModulationDepth,  m_rawParams.ModulationDepth,  RATE, dt);

    // These don't need smoothing — static
    m_smoothedParams.RoomRolloffFactor = m_rawParams.RoomRolloffFactor;
    m_smoothedParams.DecayHFLimit      = m_rawParams.DecayHFLimit;
    m_smoothedParams.HFReference       = m_rawParams.HFReference;
    m_smoothedParams.LFReference       = m_rawParams.LFReference;

    if (g_SA_DebugLogging)
    {
        static int s_probeLogCounter = 0;
        s_probeLogCounter++;
        if (s_probeLogCounter % 60 == 0)
        {
            Msg("STEAM_AUDIO: RT60=[%.3f, %.3f, %.3f] → Decay=%.2f HF=%.2f LF=%.2f Room=%.3f Refl=%.3f/%.4fs Rev=%.2f/%.4fs Dens=%.2f Diff=%.2f",
                m_listenerOutputs.reflections.reverbTimes[0],
                m_listenerOutputs.reflections.reverbTimes[1],
                m_listenerOutputs.reflections.reverbTimes[2],
                m_smoothedParams.DecayTime,
                m_smoothedParams.DecayHFRatio,
                m_smoothedParams.DecayLFRatio,
                m_smoothedParams.Room,
                m_smoothedParams.Reflections,
                m_smoothedParams.ReflectionsDelay,
                m_smoothedParams.Reverb,
                m_smoothedParams.ReverbDelay,
                m_smoothedParams.Density,
                m_smoothedParams.Diffusion);
        }
    }
}

void CSteamAudioReverb::DeriveParameters()
{
    // reverbTimes[3] = [low, mid, high] frequency band RT60 decay times
    float rtLow  = std::max(m_listenerOutputs.reflections.reverbTimes[0], 0.01f);
    float rtMid  = std::max(m_listenerOutputs.reflections.reverbTimes[1], 0.01f);
    float rtHigh = std::max(m_listenerOutputs.reflections.reverbTimes[2], 0.01f);

    float hfRatio = rtHigh / rtMid;
    float lfRatio = rtLow / rtMid;
    float sqrtMid = sqrtf(rtMid);
    float cbrtMid = powf(rtMid, 0.333f);

    // === Direct mappings ===

    // DecayTime: mid-band RT60 (primary reference)
    // EFX range: [0.1, 20.0]
    m_rawParams.DecayTime = std::clamp(rtMid, 0.1f, 20.0f);

    // DecayHFRatio: high/mid ratio
    // EFX range: [0.1, 2.0]
    m_rawParams.DecayHFRatio = std::clamp(hfRatio, 0.1f, 2.0f);

    // DecayLFRatio: low/mid ratio
    // EFX range: [0.1, 2.0]
    m_rawParams.DecayLFRatio = std::clamp(lfRatio, 0.1f, 2.0f);

    // === Gain parameters ===

    // Room (overall reverb gain): kept relatively flat so DecayTime carries room-size
    // info while reverb stays audible even in small rooms (short RT60).
    // Gentle sqrt scaling adds subtle loudness increase in large spaces.
    // EFX range: [0.0, 1.0]
    m_rawParams.Room = std::clamp(0.35f + 0.08f * sqrtMid, 0.0f, 1.0f);

    // RoomHF: scales with HF ratio — if HF decays faster, less HF energy in reverb
    // EFX range: [0.0, 1.0]
    m_rawParams.RoomHF = std::clamp(0.5f + 0.4f * m_rawParams.DecayHFRatio, 0.1f, 1.0f);

    // RoomLF: scales with LF ratio
    // EFX range: [0.0, 1.0]
    m_rawParams.RoomLF = std::clamp(0.5f + 0.4f * m_rawParams.DecayLFRatio, 0.1f, 1.0f);

    // === Spatial character ===

    // Density: how "thick" the reverb tail is
    // Compute from band similarity — more uniform decay across bands = denser
    // Use coefficient of variation: stddev/mean. Low CV = high density.
    // EFX range: [0.0, 1.0]
    {
        float mean = (rtLow + rtMid + rtHigh) / 3.0f;
        float variance = ((rtLow - mean) * (rtLow - mean) +
                          (rtMid - mean) * (rtMid - mean) +
                          (rtHigh - mean) * (rtHigh - mean)) / 3.0f;
        float cv = sqrtf(variance) / std::max(mean, 0.01f);
        m_rawParams.Density = std::clamp(1.0f - 2.0f * cv, 0.2f, 1.0f);
    }

    // Diffusion: scattered vs distinct reflections
    // Longer RT60 → more bounces → more diffuse
    // EFX range: [0.0, 1.0]
    m_rawParams.Diffusion = std::clamp(0.4f + 0.3f * sqrtMid, 0.0f, 1.0f);

    // === Early reflections ===

    // ReflectionsDelay: time to first reflection, scales with room size
    // Sabine: RT60 ∝ V/A, room dimension ∝ V^(1/3) ∝ RT60^(1/3)
    // First reflection ≈ 2 * nearestWall / c ∝ RT60^(1/3)
    // EFX range: [0.0, 0.3]
    m_rawParams.ReflectionsDelay = std::clamp(0.005f + 0.015f * cbrtMid, 0.0f, 0.3f);

    // Reflections (early gain): smaller rooms → stronger early reflections relative to late
    // Inverse sqrt relationship — close walls produce dense, prominent early reflections
    // EFX range: [0.0, 3.16]
    m_rawParams.Reflections = std::clamp(0.25f / std::max(sqrtMid, 0.1f), 0.0f, 3.16f);

    // === Late reverb ===

    // ReverbDelay: gap between early and late reverb
    // EFX range: [0.0, 0.1]
    m_rawParams.ReverbDelay = std::clamp(0.01f + 0.01f * cbrtMid, 0.0f, 0.1f);

    // Reverb (late reverb gain): longer decay = more late reverb energy
    // EFX range: [0.0, 10.0]
    m_rawParams.Reverb = std::clamp(0.5f + 0.6f * sqrtMid, 0.0f, 10.0f);

    // === Echo ===

    // EchoTime: flutter echo period. Tight spaces → shorter echo time
    // EFX range: [0.075, 0.25]
    m_rawParams.EchoTime = std::clamp(0.25f - 0.05f * std::min(rtMid, 3.0f), 0.075f, 0.25f);

    // EchoDepth: flutter echo strength. More pronounced in tight reflective spaces.
    // Scale by DecayHFRatio — reflective materials (concrete/metal, HFRatio≈1.0) produce
    // flutter between parallel walls, while absorptive materials (carpet, HFRatio<0.7) damp it.
    // EFX range: [0.0, 1.0]
    m_rawParams.EchoDepth = std::clamp((0.35f - 0.1f * rtMid) * m_rawParams.DecayHFRatio, 0.0f, 1.0f);

    // === Air absorption ===

    // AirAbsorptionHF: derived from HF ratio — faster HF decay means more absorption
    // EFX range: [0.892, 1.0]
    m_rawParams.AirAbsorptionHF = std::clamp(0.96f + 0.03f * m_rawParams.DecayHFRatio, 0.892f, 1.0f);

    // === Modulation ===

    // ModulationTime: period of reverb tail modulation. Smaller rooms → faster.
    // EFX range: [0.004, 4.0]
    m_rawParams.ModulationTime = std::clamp(0.15f + 0.08f * sqrtMid, 0.04f, 0.25f);

    // ModulationDepth: subtle tail wavering in small reflective spaces.
    // Reflective materials (high HFRatio) produce more noticeable modulation from
    // standing-wave interference between parallel surfaces.
    // EFX range: [0.0, 1.0]
    m_rawParams.ModulationDepth = std::clamp((0.10f - 0.03f * rtMid) * m_rawParams.DecayHFRatio, 0.0f, 0.15f);

    // === Static parameters ===

    // SA handles distance attenuation on direct path, so reverb shouldn't also roll off
    m_rawParams.RoomRolloffFactor = 0.0f;
    m_rawParams.DecayHFLimit = 1;  // AL_TRUE

    // Reference frequencies: SA's 3 bands are roughly Low(<800Hz), Mid(800-5kHz), High(>5kHz)
    // EAX defaults (5000Hz HF, 250Hz LF) are reasonable matches
    m_rawParams.HFReference = 5000.0f;
    m_rawParams.LFReference = 250.0f;
}

void CSteamAudioReverb::GetEnvironment(CSoundRender_Environment& env) const
{
    // Mark as extended so all EAX parameters are sent to OpenAL
    env.version = sndenv_ver_extended;

    env.DecayTime          = m_smoothedParams.DecayTime;
    env.DecayHFRatio       = m_smoothedParams.DecayHFRatio;
    env.DecayLFRatio       = m_smoothedParams.DecayLFRatio;
    env.DecayHFLimit       = m_smoothedParams.DecayHFLimit;

    env.Room               = m_smoothedParams.Room;
    env.RoomHF             = m_smoothedParams.RoomHF;
    env.RoomLF             = m_smoothedParams.RoomLF;
    env.RoomRolloffFactor  = m_smoothedParams.RoomRolloffFactor;

    env.Density            = m_smoothedParams.Density;
    env.EnvironmentDiffusion = m_smoothedParams.Diffusion;

    env.Reflections        = m_smoothedParams.Reflections;
    env.ReflectionsDelay   = m_smoothedParams.ReflectionsDelay;
    env.ReflectionsPan[0]  = 0.0f;
    env.ReflectionsPan[1]  = 0.0f;
    env.ReflectionsPan[2]  = 0.0f;

    env.Reverb             = m_smoothedParams.Reverb;
    env.ReverbDelay        = m_smoothedParams.ReverbDelay;
    env.ReverbPan[0]       = 0.0f;
    env.ReverbPan[1]       = 0.0f;
    env.ReverbPan[2]       = 0.0f;

    env.EchoTime           = m_smoothedParams.EchoTime;
    env.EchoDepth          = m_smoothedParams.EchoDepth;

    env.AirAbsorptionHF    = m_smoothedParams.AirAbsorptionHF;
    env.ModulationTime     = m_smoothedParams.ModulationTime;
    env.ModulationDepth    = m_smoothedParams.ModulationDepth;
    env.HFReference        = m_smoothedParams.HFReference;
    env.LFReference        = m_smoothedParams.LFReference;
}
