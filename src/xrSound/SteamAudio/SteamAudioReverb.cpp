#include "stdafx.h"
#include "SteamAudioReverb.h"
#include "SteamAudio.h"
#include "SteamAudioScene.h"
#include "SteamAudioSource.h"
#include "../SoundRender_CoreA.h"
#include "../SoundRender.h"

extern u32 psSndQuality;

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
    // HYBRID mode: produces IR for convolution + RT60/EQ for parametric late tail.
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
    m_listenerInputs.reverbScale[0] = psSA_ReverbScaleLow;
    m_listenerInputs.reverbScale[1] = psSA_ReverbScaleMid;
    m_listenerInputs.reverbScale[2] = psSA_ReverbScaleHigh;

    // hybridReverbTransitionTime: controls convolution vs parametric split
    // Medium (1): 0.3s — just early slaps convolved, cheaper
    // High (2): 0.5s — richer convolution of early field
    m_listenerInputs.hybridReverbTransitionTime = (psSndQuality >= 2) ? 0.5f : 0.3f;
    m_listenerInputs.hybridReverbOverlapPercent = 0.25f;  // Smooth crossfade

    m_listenerInputs.source.origin = {0.0f, 0.0f, 0.0f};
    m_listenerInputs.source.ahead = {0.0f, 0.0f, -1.0f};
    m_listenerInputs.source.up = {0.0f, 1.0f, 0.0f};
    m_listenerInputs.source.right = {1.0f, 0.0f, 0.0f};

    iplSourceSetInputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &m_listenerInputs);

    m_hasValidData = false;
    m_listenerOutputs.reflections.reverbTimes[0] = 0.0f;
    m_listenerOutputs.reflections.reverbTimes[1] = 0.0f;
    m_listenerOutputs.reflections.reverbTimes[2] = 0.0f;

    // Always initialize convolution (HYBRID mode)
    if (!InitializeConvolution(scene))
    {
        Msg("! STEAM_AUDIO: HYBRID convolution init failed");
    }

    if (m_convolutionInitialized)
        Msg("STEAM_AUDIO: Listener reverb probe initialized (HYBRID mode)");
    else
        Msg("STEAM_AUDIO: Listener reverb probe initialized (simulation only, convolution failed)");

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

    // Read per-band reverb scale from cvars — these scale SA's internal RT60 values,
    // which shape HYBRID's parametric late tail.
    m_listenerInputs.reverbScale[0] = psSA_ReverbScaleLow;
    m_listenerInputs.reverbScale[1] = psSA_ReverbScaleMid;
    m_listenerInputs.reverbScale[2] = psSA_ReverbScaleHigh;

    // Allow quality switching without reload
    m_listenerInputs.hybridReverbTransitionTime = (psSndQuality >= 2) ? 0.5f : 0.3f;

    iplSourceSetInputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &m_listenerInputs);
}

void CSteamAudioReverb::UpdateProbe(float dt)
{
    if (!m_listenerSource)
        return;

    iplSourceGetOutputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &m_listenerOutputs);

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
}

// ============================================================================
// Convolution reverb implementation
// ============================================================================

// Helper: exponential smoothing toward target
static float smooth(float current, float target, float rate, float dt)
{
    float alpha = 1.0f - expf(-rate * dt);
    return current + (target - current) * alpha;
}

bool CSteamAudioReverb::InitializeConvolution(CSteamAudioScene* scene)
{
    if (!scene || !scene->IsReady())
        return false;

    IPLContext context = CSteamAudio::Instance().GetContext();
    // SA API takes non-const IPLAudioSettings* — make a mutable copy
    IPLAudioSettings audioSettings = CSteamAudio::Instance().GetAudioSettings();
    m_frameSize = audioSettings.frameSize;
    m_samplingRate = audioSettings.samplingRate;

    // --- Create reflection effect (HYBRID: convolution early + parametric late) ---
    IPLReflectionEffectSettings reflSettings = {};
    reflSettings.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
    reflSettings.irSize = (IPLint32)(2.0f * m_samplingRate);  // Full IR for RT60/EQ analysis
    reflSettings.numChannels = AMBI_CHANNELS;                  // 2nd-order ambisonics

    IPLerror error = iplReflectionEffectCreate(context, &audioSettings, &reflSettings, &m_reflectionEffect);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create HYBRID reflection effect (error: %d)", error);
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

    // --- Create ambisonics decode effect (9ch ambisonics → 2ch stereo) ---
    IPLAmbisonicsDecodeEffectSettings decodeSettings = {};
    decodeSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
    decodeSettings.hrtf = m_hrtf;
    decodeSettings.maxOrder = 2;

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
    Msg("STEAM_AUDIO: HYBRID reverb initialized (frameSize=%d, rate=%d, %d AL buffers)",
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

    // Discard any orphaned ring buffers (level unload — no point draining them)
    CSteamAudioSource::ClearOrphanedRings();

    // Reset LP filter state
    m_lpStateL = 0.0f;
    m_lpStateR = 0.0f;

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
        Msg("STEAM_AUDIO: HYBRID reverb destroyed");
}

void CSteamAudioReverb::ReinitializeOpenAL()
{
    if (!m_convolutionInitialized)
        return;

    // Destroy stale OpenAL handles from the old context
    if (m_reverbSource)
    {
        alSourceStop(m_reverbSource);
        alSourcei(m_reverbSource, AL_BUFFER, 0);
        alDeleteSources(1, &m_reverbSource);
        m_reverbSource = 0;
    }
    alDeleteBuffers(NUM_AL_BUFFERS, m_reverbBuffers);
    for (int i = 0; i < NUM_AL_BUFFERS; i++)
        m_reverbBuffers[i] = 0;

    // Create fresh OpenAL resources on the new context
    alGenSources(1, &m_reverbSource);
    alGenBuffers(NUM_AL_BUFFERS, m_reverbBuffers);

    ALenum alErr = alGetError();
    if (alErr != AL_NO_ERROR)
    {
        Msg("! STEAM_AUDIO: Failed to recreate OpenAL reverb source after device switch (error: 0x%04x)", alErr);
        m_convolutionInitialized = false;
        return;
    }

    // Configure as non-positional, listener-relative source (same as InitializeConvolution)
    alSourcei(m_reverbSource, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(m_reverbSource, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcef(m_reverbSource, AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(m_reverbSource, AL_LOOPING, AL_FALSE);
    alSourcef(m_reverbSource, AL_GAIN, 1.0f);

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

    // Reset LP filter state
    m_lpStateL = 0.0f;
    m_lpStateR = 0.0f;

    Msg("STEAM_AUDIO: HYBRID reverb OpenAL resources reinitialized after device switch");
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
    reflParams.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
    reflParams.numChannels = AMBI_CHANNELS;

    // Only set irSize when we have a valid IR handle.
    // Before the first simulation completes, ir is null (zero-init).
    // Passing irSize > 0 with null ir could corrupt the effect's internal state.
    bool hasValidIR = (reflParams.ir != nullptr);
    if (hasValidIR)
        reflParams.irSize = (IPLint32)(2.0f * m_samplingRate);  // Must match sharedInputs.duration

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

        // Drain orphaned ring buffers from recently destroyed sources.
        if (CSteamAudioSource::DrainOrphanedFrames(m_monoInputData.data(), m_frameSize, m_tempDrainFrame.data()))
            hasSourceAudio = true;

        // --- HYBRID convolution: mono → 9ch ambisonics ---
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

        // Only call iplReflectionEffectApply when we have a valid IR handle.
        // Before the first simulation completes, ir is null — calling the effect
        // with irSize > 0 and null ir could corrupt internal state permanently.
        IPLAudioEffectState effectState;
        if (hasValidIR)
        {
            effectState = iplReflectionEffectApply(
                m_reflectionEffect, &reflParams, &monoInBuf, &ambiOutBuf, nullptr);
        }
        else
        {
            // No IR yet — output silence, keep buffer queue flowing
            for (int ch = 0; ch < AMBI_CHANNELS; ch++)
                memset(m_ambiChannelData[ch].data(), 0, m_frameSize * sizeof(float));
            effectState = IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
        }

        // If no sources are playing AND the tail is complete, output silence
        if (!hasSourceAudio && effectState == IPL_AUDIOEFFECTSTATE_TAILCOMPLETE)
        {
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
        decodeParams.order = 2;
        decodeParams.hrtf = m_hrtf;
        decodeParams.orientation = m_listenerInputs.source;
        decodeParams.binaural = IPL_TRUE;

        iplAmbisonicsDecodeEffectApply(m_ambisonicsDecoder, &decodeParams, &ambiOutBuf, &stereoOutBuf);

        // --- Gentle 1-pole low-pass to darken reverb tail ---
        float lpAlpha = std::clamp(psSA_ConvolutionLPF, 0.1f, 1.0f);
        if (lpAlpha < 1.0f)
        {
            for (int i = 0; i < m_frameSize; i++)
            {
                m_lpStateL += lpAlpha * (m_stereoChannelData[0][i] - m_lpStateL);
                m_lpStateR += lpAlpha * (m_stereoChannelData[1][i] - m_lpStateR);
                m_stereoChannelData[0][i] = m_lpStateL;
                m_stereoChannelData[1][i] = m_lpStateR;
            }
        }

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
            Msg("STEAM_AUDIO: HYBRID convolution source restarted (underrun)");
    }
}
