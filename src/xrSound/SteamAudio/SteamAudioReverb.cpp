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
    // Persistent IPLSource at the listener position, runs reflections simulation.
    // In PARAMETRIC mode: extracts 3-band RT60 values from the simulated field.
    // In CONVOLUTION mode: produces an impulse response for convolution.
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

    // Initialize convolution reverb if enabled
    if (psSA_Convolution)
    {
        if (!InitializeConvolution(scene))
        {
            Msg("! STEAM_AUDIO: Convolution init failed, falling back to parametric");
        }
    }

    if (g_SA_DebugLogging)
    {
        if (m_convolutionInitialized)
            Msg("STEAM_AUDIO: Listener reverb probe initialized (CONVOLUTION mode)");
        else
            Msg("STEAM_AUDIO: Listener reverb probe initialized (PARAMETRIC mode, full EFX mapping)");
    }

    return true;
}

void CSteamAudioReverb::Destroy()
{
    DestroyConvolution();

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

    // Check validity: reverbTimes are always populated (both parametric and convolution modes)
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

    // Store current RT60 in ring buffer for median filtering
    for (int b = 0; b < 3; b++)
        m_rt60History[b][m_historyIndex] = m_listenerOutputs.reflections.reverbTimes[b];
    m_historyIndex = (m_historyIndex + 1) % 3;

    // Median-of-3 rejects isolated spikes (parallel-wall Monte Carlo artifacts)
    // while preserving genuine room transitions (2+ consecutive matching values pass immediately)
    auto median3 = [](float a, float b, float c) {
        if (a > b) std::swap(a, b);
        if (b > c) std::swap(b, c);
        if (a > b) std::swap(a, b);
        return b;
    };
    for (int b = 0; b < 3; b++)
        m_filteredRT60[b] = median3(m_rt60History[b][0], m_rt60History[b][1], m_rt60History[b][2]);

    // Compute raw parameters from filtered reverbTimes (used for EFX in parametric mode,
    // and for gain control in convolution mode)
    DeriveParameters();

    // Smooth all parameters toward raw values.
    // Rate of 2/s → ~63% convergence in 500ms, ~95% in 1.5s. Dampens Monte Carlo
    // noise from the ray tracer while still responding to real room transitions.
    constexpr float RATE = 2.0f;

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
    // Read from median-filtered values to reject Monte Carlo spikes.
    //
    // Clamp to [0.01, MAX_RT60]. With the median filter as primary spike defense,
    // this clamp only catches sustained artifacts. Nothing in S.T.A.L.K.E.R.'s
    // game world exceeds ~2s (largest rooms are industrial halls).
    constexpr float MAX_RT60 = 2.0f;
    float rtLow  = std::clamp(m_filteredRT60[0], 0.01f, MAX_RT60);
    float rtMid  = std::clamp(m_filteredRT60[1], 0.01f, MAX_RT60);
    float rtHigh = std::clamp(m_filteredRT60[2], 0.01f, MAX_RT60);

    float hfRatio = rtHigh / rtMid;
    float lfRatio = rtLow / rtMid;
    float sqrtMid = sqrtf(rtMid);

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

    // Room (overall reverb gain): EFX presets use ~0.3162 across most presets.
    // DecayTime carries room-size info; Room shouldn't scale much.
    // However, for very short RT60 (tiny rooms), total reverb energy = Room × Reverb × DecayTime
    // drops too low (0.097 at RT60=0.25 vs EFX Room's 0.499). Mild boost compensates.
    // Ramps from 0.395 at RT60=0.25 down to constant 0.32 at RT60≥0.5.
    // EFX range: [0.0, 1.0]
    m_rawParams.Room = 0.32f + 0.15f * std::max(0.0f, 1.0f - rtMid * 2.0f);

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
    // Computed before Reverb because it's a useful metric for reverb character.
    // EFX range: [0.0, 1.0]
    {
        float mean = (rtLow + rtMid + rtHigh) / 3.0f;
        float variance = ((rtLow - mean) * (rtLow - mean) +
                          (rtMid - mean) * (rtMid - mean) +
                          (rtHigh - mean) * (rtHigh - mean)) / 3.0f;
        float cv = sqrtf(variance) / std::max(mean, 0.01f);
        m_rawParams.Density = std::clamp(1.0f - 2.0f * cv, 0.2f, 1.0f);
    }

    // Diffusion: INVERTED relationship vs old formula.
    // EFX reference: Indoor (Room/Stoneroom/Hallway) = 1.0, Outdoor (Forest/Plain/Valley) = 0.0-0.45.
    // Enclosed spaces → sound bounces off many nearby walls → smooth, blended tail (high diffusion).
    // Open spaces → fewer surfaces, more distinct individual echoes → low diffusion.
    // EFX range: [0.0, 1.0]
    {
        float diffNorm = std::clamp((rtMid - 0.15f) / 1.5f, 0.0f, 1.0f);
        m_rawParams.Diffusion = std::clamp(0.95f - 0.55f * diffNorm, 0.15f, 1.0f);
    }

    // === Early reflections ===

    // ReflectionsDelay: time to first reflection, scales with room size.
    // EFX: Indoor = 2-12ms (nearby walls), Outdoor = 69-263ms (distant surfaces).
    // Linear scaling maps RT60 directly to delay — physically correct since
    // both are proportional to room dimensions / speed of sound.
    // RT60=0.25 → 12ms (EFX Stoneroom=12ms), RT60=1.25 → 52ms (EFX Backyard=69ms).
    // EFX range: [0.0, 0.3]
    m_rawParams.ReflectionsDelay = std::clamp(0.002f + 0.04f * rtMid, 0.0f, 0.3f);

    // Reflections (early gain): gentle positive scaling.
    // Old formula (0.25/√rtMid) gave indoor 2.3x more than outdoor — too extreme.
    // EFX presets show no clear indoor/outdoor pattern (0.05-0.45).
    // ReflectionsDelay is what distinguishes indoor vs outdoor, not gain.
    // EFX range: [0.0, 3.16]
    m_rawParams.Reflections = std::clamp(0.15f + 0.15f * sqrtMid, 0.05f, 0.6f);

    // === Late reverb ===

    // ReverbDelay: gap between early and late reverb. Linear for same reasons as ReflectionsDelay.
    // RT60=0.25 → 8ms (EFX Room=11ms), RT60=1.25 → 33ms, RT60=2.0 → 52ms.
    // EFX range: [0.0, 0.1]
    m_rawParams.ReverbDelay = std::clamp(0.002f + 0.025f * rtMid, 0.0f, 0.1f);

    // Reverb (late reverb gain): INVERTED vs old formula.
    // EFX reference: Indoor late reverb = 1.0-1.66, Outdoor = 0.11-0.77.
    // Enclosed spaces accumulate energy (many short reflection paths → dense late field).
    // Open spaces lose energy to infinity (few long paths → sparse late field).
    // Sigmoid transition centered at 0.7s RT60 (approximate indoor/outdoor boundary for OWA tuning).
    // EFX range: [0.0, 10.0]
    {
        float openness = 1.0f / (1.0f + expf(-4.0f * (rtMid - 0.7f)));
        m_rawParams.Reverb = std::clamp(1.3f - 0.6f * openness, 0.3f, 10.0f);
    }

    // === Echo ===

    // EchoTime: flutter echo period. Tight spaces → shorter echo time
    // EFX range: [0.075, 0.25]
    m_rawParams.EchoTime = std::clamp(0.25f - 0.05f * std::min(rtMid, 2.0f), 0.075f, 0.25f);

    // EchoDepth: flutter echo strength — requires parallel walls (enclosed spaces).
    // Short RT60 = enclosed (parallel walls exist) → echo active.
    // Long RT60 = open (no parallel surfaces) → echo suppressed.
    // Scale by DecayHFRatio — reflective materials (concrete/metal) produce flutter.
    // EFX range: [0.0, 1.0]
    float echoEnclosure = 1.0f - 1.0f / (1.0f + expf(-6.0f * (rtMid - 0.5f)));
    m_rawParams.EchoDepth = std::clamp(
        0.25f * echoEnclosure * m_rawParams.DecayHFRatio,
        0.0f, 1.0f);

    // === Air absorption ===

    // AirAbsorptionHF: derived from HF ratio — faster HF decay means more absorption
    // EFX range: [0.892, 1.0]
    m_rawParams.AirAbsorptionHF = std::clamp(0.96f + 0.03f * m_rawParams.DecayHFRatio, 0.892f, 1.0f);

    // === Modulation ===

    // ModulationTime: period of reverb tail modulation. Smaller rooms → faster.
    // EFX range: [0.004, 4.0]
    m_rawParams.ModulationTime = std::clamp(0.15f + 0.08f * sqrtMid, 0.04f, 0.25f);

    // ModulationDepth: standing-wave interference — also needs parallel surfaces.
    // Same enclosure gating as EchoDepth.
    // EFX range: [0.0, 1.0]
    m_rawParams.ModulationDepth = std::clamp(
        0.08f * echoEnclosure * m_rawParams.DecayHFRatio,
        0.0f, 0.15f);

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

// ============================================================================
// Convolution reverb implementation
// ============================================================================

bool CSteamAudioReverb::InitializeConvolution(CSteamAudioScene* scene)
{
    if (!scene || !scene->IsReady())
        return false;

    IPLContext context = CSteamAudio::Instance().GetContext();
    // SA API takes non-const IPLAudioSettings* — make a mutable copy
    IPLAudioSettings audioSettings = CSteamAudio::Instance().GetAudioSettings();
    m_frameSize = audioSettings.frameSize;
    m_samplingRate = audioSettings.samplingRate;

    // --- Create reflection effect (convolution engine) ---
    // Takes mono input, produces ambisonics output by convolving with the IR
    IPLReflectionEffectSettings reflSettings = {};
    reflSettings.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
    reflSettings.irSize = (IPLint32)(2.0f * m_samplingRate);  // 2s IR at 44.1kHz = 88200 samples
    reflSettings.numChannels = AMBI_CHANNELS;                  // 1st-order ambisonics

    IPLerror error = iplReflectionEffectCreate(context, &audioSettings, &reflSettings, &m_reflectionEffect);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create reflection effect (error: %d)", error);
        return false;
    }

    // --- Create HRTF (required by ambisonics decode API even in PANNING mode) ---
    IPLHRTFSettings hrtfSettings = {};
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1.0f;

    error = iplHRTFCreate(context, &audioSettings, &hrtfSettings, &m_hrtf);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create HRTF for ambisonics decode (error: %d)", error);
        iplReflectionEffectRelease(&m_reflectionEffect);
        m_reflectionEffect = nullptr;
        return false;
    }

    // --- Create ambisonics decode effect (4ch ambisonics → 2ch stereo) ---
    IPLAmbisonicsDecodeEffectSettings decodeSettings = {};
    decodeSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
    decodeSettings.hrtf = m_hrtf;
    decodeSettings.maxOrder = 1;

    error = iplAmbisonicsDecodeEffectCreate(context, &audioSettings, &decodeSettings, &m_ambisonicsDecoder);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create ambisonics decoder (error: %d)", error);
        iplHRTFRelease(&m_hrtf);
        m_hrtf = nullptr;
        iplReflectionEffectRelease(&m_reflectionEffect);
        m_reflectionEffect = nullptr;
        return false;
    }

    // --- Allocate processing buffers ---
    m_monoInputData.assign(m_frameSize, 0.0f);
    for (int ch = 0; ch < AMBI_CHANNELS; ch++)
        m_ambiChannelData[ch].assign(m_frameSize, 0.0f);
    for (int ch = 0; ch < STEREO_CHANNELS; ch++)
        m_stereoChannelData[ch].assign(m_frameSize, 0.0f);
    m_stereoS16.assign(m_frameSize * STEREO_CHANNELS, 0);

    // --- Initialize temp drain buffer for per-source ring draining ---
    m_tempDrainFrame.assign(m_frameSize, 0.0f);

    // --- Create OpenAL streaming source and buffers ---
    alGenSources(1, &m_reverbSource);
    alGenBuffers(NUM_AL_BUFFERS, m_reverbBuffers);

    ALenum alErr = alGetError();
    if (alErr != AL_NO_ERROR)
    {
        Msg("! STEAM_AUDIO: Failed to create OpenAL reverb source (error: 0x%04x)", alErr);
        iplAmbisonicsDecodeEffectRelease(&m_ambisonicsDecoder);
        m_ambisonicsDecoder = nullptr;
        iplHRTFRelease(&m_hrtf);
        m_hrtf = nullptr;
        iplReflectionEffectRelease(&m_reflectionEffect);
        m_reflectionEffect = nullptr;
        return false;
    }

    // Configure as non-positional, listener-relative source (acts as a reverb bus)
    alSourcei(m_reverbSource, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(m_reverbSource, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcef(m_reverbSource, AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(m_reverbSource, AL_LOOPING, AL_FALSE);
    alSourcef(m_reverbSource, AL_GAIN, 1.0f);
    // NOT routed to EFX slot — no reverb-of-reverb

    // Pre-fill buffers with silence and queue them
    xr_vector<s16> silence(m_frameSize * STEREO_CHANNELS, 0);
    for (int i = 0; i < NUM_AL_BUFFERS; i++)
    {
        alBufferData(m_reverbBuffers[i], AL_FORMAT_STEREO16,
                     silence.data(), (ALsizei)(silence.size() * sizeof(s16)),
                     m_samplingRate);
    }
    alSourceQueueBuffers(m_reverbSource, NUM_AL_BUFFERS, m_reverbBuffers);
    alSourcePlay(m_reverbSource);

    m_convolutionInitialized = true;
    Msg("STEAM_AUDIO: Convolution reverb initialized (frameSize=%d, rate=%d, %d AL buffers)",
        m_frameSize, m_samplingRate, NUM_AL_BUFFERS);

    return true;
}

void CSteamAudioReverb::DestroyConvolution()
{
    if (!m_convolutionInitialized)
        return;

    // Stop and delete OpenAL source
    if (m_reverbSource)
    {
        alSourceStop(m_reverbSource);
        alSourcei(m_reverbSource, AL_BUFFER, 0);  // Detach all buffers
        alDeleteSources(1, &m_reverbSource);
        m_reverbSource = 0;
    }

    // Delete OpenAL buffers
    alDeleteBuffers(NUM_AL_BUFFERS, m_reverbBuffers);
    for (int i = 0; i < NUM_AL_BUFFERS; i++)
        m_reverbBuffers[i] = 0;

    // Release IPL objects
    if (m_ambisonicsDecoder)
    {
        iplAmbisonicsDecodeEffectRelease(&m_ambisonicsDecoder);
        m_ambisonicsDecoder = nullptr;
    }
    if (m_hrtf)
    {
        iplHRTFRelease(&m_hrtf);
        m_hrtf = nullptr;
    }
    if (m_reflectionEffect)
    {
        iplReflectionEffectRelease(&m_reflectionEffect);
        m_reflectionEffect = nullptr;
    }

    // Free processing buffers
    m_monoInputData.clear();
    m_tempDrainFrame.clear();
    for (int ch = 0; ch < AMBI_CHANNELS; ch++)
        m_ambiChannelData[ch].clear();
    for (int ch = 0; ch < STEREO_CHANNELS; ch++)
        m_stereoChannelData[ch].clear();
    m_stereoS16.clear();

    m_convolutionInitialized = false;
    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Convolution reverb destroyed");
}

void CSteamAudioReverb::UpdateConvolution()
{
    if (!m_convolutionInitialized || !m_reflectionEffect || !m_ambisonicsDecoder)
        return;

    ALint processed = 0;
    alGetSourcei(m_reverbSource, AL_BUFFERS_PROCESSED, &processed);

    if (processed <= 0)
        return;

    const auto& sources = CSteamAudioSource::GetActiveSources();

    // Snapshot the reflection params ONCE per UpdateConvolution call.
    // Reuse for all iterations so the IR doesn't change mid-burst
    // if the simulation thread completes between iterations.
    IPLReflectionEffectParams reflParams = m_listenerOutputs.reflections;
    reflParams.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
    reflParams.numChannels = AMBI_CHANNELS;
    // Don't override irSize — let it use whatever the simulation produced.
    // The simulator sets this field based on actual ray-traced IR content.
    // Hardcoding to maxDuration * sampleRate tells the convolver the IR is
    // longer than it actually is, which can cause premature tail cutoff
    // when the effect reads past valid IR data into zeros.

    while (processed > 0)
    {
        // --- Mix one frame from each active source's ring ---
        memset(m_monoInputData.data(), 0, m_frameSize * sizeof(float));
        bool hasSourceAudio = false;
        for (auto* src : sources)
        {
            if (src->PopFrame(m_tempDrainFrame.data()))
            {
                hasSourceAudio = true;
                for (int i = 0; i < m_frameSize; i++)
                    m_monoInputData[i] += m_tempDrainFrame[i];
            }
        }

        // --- Convolution: mono → 4ch ambisonics ---
        float* monoPtr = m_monoInputData.data();
        IPLAudioBuffer monoInBuf = {};
        monoInBuf.numChannels = 1;
        monoInBuf.numSamples = m_frameSize;
        monoInBuf.data = &monoPtr;

        float* ambiPtrs[AMBI_CHANNELS];
        for (int ch = 0; ch < AMBI_CHANNELS; ch++)
            ambiPtrs[ch] = m_ambiChannelData[ch].data();

        IPLAudioBuffer ambiOutBuf = {};
        ambiOutBuf.numChannels = AMBI_CHANNELS;
        ambiOutBuf.numSamples = m_frameSize;
        ambiOutBuf.data = ambiPtrs;

        IPLAudioEffectState effectState = iplReflectionEffectApply(
            m_reflectionEffect, &reflParams, &monoInBuf, &ambiOutBuf, nullptr);

        // If no sources are playing AND the tail is complete, output silence
        // to avoid burning CPU on decode/convert for zero-energy frames.
        if (!hasSourceAudio && effectState == IPL_AUDIOEFFECTSTATE_TAILCOMPLETE)
        {
            // Still need to feed OpenAL so it doesn't underrun.
            // Queue silence for remaining processed buffers.
            memset(m_stereoS16.data(), 0, m_stereoS16.size() * sizeof(s16));
            while (processed > 0)
            {
                ALuint bufId = 0;
                alSourceUnqueueBuffers(m_reverbSource, 1, &bufId);
                alBufferData(bufId, AL_FORMAT_STEREO16,
                             m_stereoS16.data(),
                             (ALsizei)(m_stereoS16.size() * sizeof(s16)),
                             m_samplingRate);
                alSourceQueueBuffers(m_reverbSource, 1, &bufId);
                processed--;
            }
            break;
        }

        // --- Decode ambisonics → stereo ---
        float* stereoPtrs[STEREO_CHANNELS];
        for (int ch = 0; ch < STEREO_CHANNELS; ch++)
            stereoPtrs[ch] = m_stereoChannelData[ch].data();

        IPLAudioBuffer stereoOutBuf = {};
        stereoOutBuf.numChannels = STEREO_CHANNELS;
        stereoOutBuf.numSamples = m_frameSize;
        stereoOutBuf.data = stereoPtrs;

        IPLAmbisonicsDecodeEffectParams decodeParams = {};
        decodeParams.order = 1;
        decodeParams.hrtf = m_hrtf;
        decodeParams.orientation = m_listenerInputs.source;
        decodeParams.binaural = IPL_FALSE;

        iplAmbisonicsDecodeEffectApply(m_ambisonicsDecoder, &decodeParams, &ambiOutBuf, &stereoOutBuf);

        // --- Gain + s16 conversion ---
        float targetGain = std::clamp(psSA_ConvolutionGain, 0.0f, 2.0f);
        float frameDt = (float)m_frameSize / (float)m_samplingRate;
        m_smoothedConvGain = smooth(m_smoothedConvGain, targetGain, 2.0f, frameDt);

        for (int i = 0; i < m_frameSize; i++)
        {
            float L = m_stereoChannelData[0][i] * m_smoothedConvGain * 32767.0f;
            float R = m_stereoChannelData[1][i] * m_smoothedConvGain * 32767.0f;
            L = std::clamp(L, -32768.0f, 32767.0f);
            R = std::clamp(R, -32768.0f, 32767.0f);
            m_stereoS16[i * 2]     = (s16)L;
            m_stereoS16[i * 2 + 1] = (s16)R;
        }

        // --- Queue buffer ---
        ALuint bufId = 0;
        alSourceUnqueueBuffers(m_reverbSource, 1, &bufId);
        alBufferData(bufId, AL_FORMAT_STEREO16,
                     m_stereoS16.data(),
                     (ALsizei)(m_stereoS16.size() * sizeof(s16)),
                     m_samplingRate);
        alSourceQueueBuffers(m_reverbSource, 1, &bufId);
        processed--;
    }

    // --- Underrun recovery ---
    ALint state = 0;
    alGetSourcei(m_reverbSource, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING)
    {
        alSourcePlay(m_reverbSource);
        if (g_SA_DebugLogging)
            Msg("STEAM_AUDIO: Convolution source restarted (underrun)");
    }
}