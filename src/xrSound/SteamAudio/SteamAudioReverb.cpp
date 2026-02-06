#include "stdafx.h"
#include "SteamAudioReverb.h"
#include "SteamAudio.h"
#include "SteamAudioScene.h"
#include "SteamAudioSource.h"
#include "../SoundRender_CoreA.h"

CSteamAudioReverb::CSteamAudioReverb()
{
}

CSteamAudioReverb::~CSteamAudioReverb()
{
    Destroy();
}

bool CSteamAudioReverb::Initialize(CSteamAudioScene* scene, IPLHRTF hrtf)
{
    if (!scene || !scene->IsReady() || !hrtf)
    {
        Msg("! STEAM_AUDIO: Cannot initialize reverb - scene or HRTF not ready");
        return false;
    }

    CSteamAudio& sa = CSteamAudio::Instance();
    IPLContext context = sa.GetContext();
    const IPLAudioSettings& audioSettings = sa.GetAudioSettings();
    IPLSimulator simulator = scene->GetSimulator();

    m_hrtf = hrtf;
    m_frameSize = audioSettings.frameSize;
    m_sampleRate = audioSettings.samplingRate;
    m_simulator = simulator;

    // Calculate IR size based on max reverb duration
    m_irSize = (int)(MAX_REVERB_DURATION * m_sampleRate);

    IPLAudioSettings audioSettingsCopy = audioSettings;

    // --- Create listener probe source ---
    // This is a persistent IPLSource at the listener position that only runs reflections.
    // It always has a valid IR representing the listener's acoustic space.
    IPLSourceSettings sourceSettings = {};
    sourceSettings.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;

    IPLerror error = iplSourceCreate(simulator, &sourceSettings, &m_listenerSource);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create listener probe source (error: %d)", error);
        return false;
    }

    // Add to simulator — commit will happen via FlushCommit
    iplSourceAdd(m_listenerSource, simulator);
    scene->MarkPendingCommit();

    // Initialize listener probe inputs
    m_listenerInputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
    m_listenerInputs.reverbScale[0] = 1.0f;
    m_listenerInputs.reverbScale[1] = 1.0f;
    m_listenerInputs.reverbScale[2] = 1.0f;
    m_listenerInputs.hybridReverbTransitionTime = 1.0f;
    m_listenerInputs.hybridReverbOverlapPercent = 0.25f;

    // Default position at origin — UpdateListenerProbe will set the real position
    m_listenerInputs.source.origin = {0.0f, 0.0f, 0.0f};
    m_listenerInputs.source.ahead = {0.0f, 0.0f, -1.0f};
    m_listenerInputs.source.up = {0.0f, 1.0f, 0.0f};
    m_listenerInputs.source.right = {1.0f, 0.0f, 0.0f};

    // Push initial inputs
    iplSourceSetInputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &m_listenerInputs);

    // --- Create reflection effect (single convolution) ---
    IPLReflectionEffectSettings effectSettings = {};
    effectSettings.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
    effectSettings.irSize = m_irSize;
    effectSettings.numChannels = AMBISONICS_CHANNELS;

    error = iplReflectionEffectCreate(context, &audioSettingsCopy, &effectSettings, &m_listenerEffect);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create listener reflection effect (error: %d)", error);
        iplSourceRemove(m_listenerSource, simulator);
        iplSourceRelease(&m_listenerSource);
        m_listenerSource = nullptr;
        scene->MarkPendingCommit();
        return false;
    }

    // --- Create Ambisonics decoder ---
    IPLAmbisonicsDecodeEffectSettings decodeSettings = {};
    decodeSettings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
    decodeSettings.hrtf = m_hrtf;
    decodeSettings.maxOrder = AMBISONICS_ORDER;

    error = iplAmbisonicsDecodeEffectCreate(context, &audioSettingsCopy, &decodeSettings, &m_decoder);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create Ambisonics decoder (error: %d)", error);
        iplReflectionEffectRelease(&m_listenerEffect);
        m_listenerEffect = nullptr;
        iplSourceRemove(m_listenerSource, simulator);
        iplSourceRelease(&m_listenerSource);
        m_listenerSource = nullptr;
        scene->MarkPendingCommit();
        return false;
    }

    // --- Create OpenAL source for reverb output ---
    alGenSources(1, &m_alSource);
    ALenum alError = alGetError();
    if (alError != AL_NO_ERROR)
    {
        Msg("! STEAM_AUDIO: Failed to create OpenAL reverb source (error: 0x%04x)", alError);
        iplAmbisonicsDecodeEffectRelease(&m_decoder);
        m_decoder = nullptr;
        iplReflectionEffectRelease(&m_listenerEffect);
        m_listenerEffect = nullptr;
        iplSourceRemove(m_listenerSource, simulator);
        iplSourceRelease(&m_listenerSource);
        m_listenerSource = nullptr;
        scene->MarkPendingCommit();
        return false;
    }

    // Configure reverb source - positioned at listener (relative mode)
    alSourcei(m_alSource, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(m_alSource, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcef(m_alSource, AL_GAIN, 1.0f);
    alSourcef(m_alSource, AL_PITCH, 1.0f);
    alSourcei(m_alSource, AL_LOOPING, AL_FALSE);

    // Create buffers for streaming
    alGenBuffers(NUM_REVERB_BUFFERS, m_alBuffers);
    alError = alGetError();
    if (alError != AL_NO_ERROR)
    {
        Msg("! STEAM_AUDIO: Failed to create OpenAL reverb buffers (error: 0x%04x)", alError);
        alDeleteSources(1, &m_alSource);
        m_alSource = 0;
        iplAmbisonicsDecodeEffectRelease(&m_decoder);
        m_decoder = nullptr;
        iplReflectionEffectRelease(&m_listenerEffect);
        m_listenerEffect = nullptr;
        iplSourceRemove(m_listenerSource, simulator);
        iplSourceRelease(&m_listenerSource);
        m_listenerSource = nullptr;
        scene->MarkPendingCommit();
        return false;
    }

    // --- Allocate dry bus buffer (mono, frameSize) ---
    m_dryBusData.resize(m_frameSize, 0.0f);
    m_dryBusPtr = m_dryBusData.data();
    m_dryBusBuffer.numChannels = 1;
    m_dryBusBuffer.numSamples = m_frameSize;
    m_dryBusBuffer.data = &m_dryBusPtr;

    // --- Allocate Ambisonics buffer (order 2 = 9 channels) ---
    m_ambisonicsData.resize(AMBISONICS_CHANNELS * m_frameSize);
    m_ambisonicsChannels.resize(AMBISONICS_CHANNELS);
    for (int i = 0; i < AMBISONICS_CHANNELS; i++)
    {
        m_ambisonicsChannels[i] = m_ambisonicsData.data() + i * m_frameSize;
    }
    m_ambisonicsBuffer.numChannels = AMBISONICS_CHANNELS;
    m_ambisonicsBuffer.numSamples = m_frameSize;
    m_ambisonicsBuffer.data = m_ambisonicsChannels.data();

    // --- Allocate stereo output buffer (deinterleaved, float) ---
    m_stereoData.resize(2 * m_frameSize);
    m_stereoChannels.resize(2);
    m_stereoChannels[0] = m_stereoData.data();
    m_stereoChannels[1] = m_stereoData.data() + m_frameSize;
    m_stereoBuffer.numChannels = 2;
    m_stereoBuffer.numSamples = m_frameSize;
    m_stereoBuffer.data = m_stereoChannels.data();

    // --- Allocate output smoothing cache (deinterleaved stereo) ---
    m_cachedStereoData.resize(2 * m_frameSize, 0.0f);
    m_cachedStereoChannels.resize(2);
    m_cachedStereoChannels[0] = m_cachedStereoData.data();
    m_cachedStereoChannels[1] = m_cachedStereoData.data() + m_frameSize;
    m_hasCachedOutput = false;

    // --- Allocate interleaved s16 output for OpenAL ---
    m_interleavedOutput.resize(m_frameSize * 2);

    // Initialize listener orientation to identity
    m_listenerCoords.origin = {0.0f, 0.0f, 0.0f};
    m_listenerCoords.ahead = {0.0f, 0.0f, -1.0f};
    m_listenerCoords.up = {0.0f, 1.0f, 0.0f};
    m_listenerCoords.right = {1.0f, 0.0f, 0.0f};

    m_buffersQueued = false;
    m_hasValidIR = false;

    // Initialize outputs to safe defaults
    m_listenerOutputs.reflections.ir = nullptr;
    m_listenerOutputs.reflections.reverbTimes[0] = 0.0f;
    m_listenerOutputs.reflections.reverbTimes[1] = 0.0f;
    m_listenerOutputs.reflections.reverbTimes[2] = 0.0f;

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Listener reverb probe initialized (Ambisonics order: %d, channels: %d, frame: %d, IR: %d samples = %.1fs)",
            AMBISONICS_ORDER, AMBISONICS_CHANNELS, m_frameSize, m_irSize, MAX_REVERB_DURATION);

    return true;
}

void CSteamAudioReverb::Destroy()
{
    // Remove listener probe from simulator
    if (m_listenerSource)
    {
        if (m_simulator)
        {
            iplSourceRemove(m_listenerSource, m_simulator);
            // Defer commit — if scene is being destroyed, it doesn't matter
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

    // Stop and delete OpenAL source
    if (m_alSource)
    {
        alSourceStop(m_alSource);
        alSourcei(m_alSource, AL_BUFFER, 0);
        alDeleteSources(1, &m_alSource);
        m_alSource = 0;
    }

    // Delete OpenAL buffers
    bool hasValidBuffers = false;
    for (int i = 0; i < NUM_REVERB_BUFFERS; i++)
    {
        if (m_alBuffers[i] != 0)
        {
            hasValidBuffers = true;
            break;
        }
    }
    if (hasValidBuffers)
    {
        alDeleteBuffers(NUM_REVERB_BUFFERS, m_alBuffers);
        for (int i = 0; i < NUM_REVERB_BUFFERS; i++)
            m_alBuffers[i] = 0;
    }

    if (m_listenerEffect)
    {
        iplReflectionEffectRelease(&m_listenerEffect);
        m_listenerEffect = nullptr;
    }

    if (m_decoder)
    {
        iplAmbisonicsDecodeEffectRelease(&m_decoder);
        m_decoder = nullptr;
    }

    m_dryBusData.clear();
    m_dryBusPtr = nullptr;
    m_ambisonicsData.clear();
    m_ambisonicsChannels.clear();
    m_stereoData.clear();
    m_stereoChannels.clear();
    m_cachedStereoData.clear();
    m_cachedStereoChannels.clear();
    m_hasCachedOutput = false;
    m_interleavedOutput.clear();
    m_hrtf = nullptr;
    m_buffersQueued = false;
    m_hasValidIR = false;
}

void CSteamAudioReverb::SetListenerOrientation(const Fvector& forward, const Fvector& up)
{
    // X-Ray uses: +X right, +Y up, -Z forward
    // Steam Audio uses the same convention
    m_listenerCoords.ahead = {forward.x, forward.y, -forward.z};
    m_listenerCoords.up = {up.x, up.y, -up.z};

    // Compute right vector (cross product of up and ahead)
    Fvector right;
    right.crossproduct(up, forward);
    m_listenerCoords.right = {right.x, right.y, -right.z};
}

void CSteamAudioReverb::BeginFrame()
{
    // Zero-fill the dry bus — sources will accumulate into it during this frame
    if (!m_dryBusData.empty())
    {
        memset(m_dryBusData.data(), 0, m_dryBusData.size() * sizeof(float));
    }
}

void CSteamAudioReverb::AccumulateDryAudio(const float* data, int numSamples, float distanceGain)
{
    // Additively mix source's dry audio into the shared dry bus, scaled by distance.
    // Without distance scaling, a sound 200m away pumps the same reverb energy as one at 5m.
    int count = std::min(numSamples, m_frameSize);
    float* bus = m_dryBusData.data();
    for (int i = 0; i < count; i++)
        bus[i] += data[i] * distanceGain;
}

void CSteamAudioReverb::UpdateListenerProbe(const Fvector& pos, const Fvector& dir, const Fvector& up)
{
    if (!m_listenerSource)
        return;

    // Convert position to right-handed coords (negate Z)
    m_listenerInputs.source.origin.x = pos.x;
    m_listenerInputs.source.origin.y = pos.y;
    m_listenerInputs.source.origin.z = -pos.z;

    // Set orientation (for reverb probe, orientation affects ray distribution)
    m_listenerInputs.source.ahead.x = dir.x;
    m_listenerInputs.source.ahead.y = dir.y;
    m_listenerInputs.source.ahead.z = -dir.z;

    m_listenerInputs.source.up.x = up.x;
    m_listenerInputs.source.up.y = up.y;
    m_listenerInputs.source.up.z = -up.z;

    // Compute right vector
    Fvector right;
    right.crossproduct(up, dir);
    m_listenerInputs.source.right.x = right.x;
    m_listenerInputs.source.right.y = right.y;
    m_listenerInputs.source.right.z = -right.z;

    iplSourceSetInputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &m_listenerInputs);
}

void CSteamAudioReverb::EndFrame()
{
    if (!m_enabled || !m_listenerEffect || !m_decoder || !m_alSource || m_frameSize == 0)
        return;

    // Fetch listener probe outputs (IR from latest reflection simulation)
    if (m_listenerSource)
    {
        iplSourceGetOutputs(m_listenerSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &m_listenerOutputs);
    }

    // Track IR validity
    bool irValid = (m_listenerOutputs.reflections.ir != nullptr);
    if (irValid && !m_hasValidIR)
    {
        m_hasValidIR = true;
        Msg("STEAM_AUDIO: Listener probe received first valid IR — reverbTimes: [%.3f, %.3f, %.3f]",
            m_listenerOutputs.reflections.reverbTimes[0],
            m_listenerOutputs.reflections.reverbTimes[1],
            m_listenerOutputs.reflections.reverbTimes[2]);
    }

    m_dbgFrameCount++;

    // Debug logging
    if (g_SA_DebugLogging && (m_dbgFrameCount % 60 == 0))
    {
        // Measure dry bus energy
        float dryEnergy = 0.0f;
        for (int i = 0; i < m_frameSize; i++)
        {
            float s = m_dryBusData[i];
            dryEnergy += s * s;
        }

        Msg("STEAM_AUDIO: Reverb stats - IR valid: %s, dry bus energy: %.8f, reverb times: [%.3f, %.3f, %.3f]",
            m_hasValidIR ? "yes" : "no", dryEnergy,
            m_listenerOutputs.reflections.reverbTimes[0],
            m_listenerOutputs.reflections.reverbTimes[1],
            m_listenerOutputs.reflections.reverbTimes[2]);
    }

    // Apply convolution: dry bus → Ambisonics via listener probe's IR
    // The reflection effect's internal overlap-save state naturally produces tails
    // when dry input goes silent — no decay pool needed.
    if (m_hasValidIR)
    {
        // nullptr for mixer — output goes directly to Ambisonics buffer
        iplReflectionEffectApply(m_listenerEffect, &m_listenerOutputs.reflections,
                                 &m_dryBusBuffer, &m_ambisonicsBuffer, nullptr);
    }
    else
    {
        // No valid IR yet — zero the Ambisonics buffer
        memset(m_ambisonicsData.data(), 0, m_ambisonicsData.size() * sizeof(float));
    }

    // Decode Ambisonics to stereo using binaural HRTF
    IPLAmbisonicsDecodeEffectParams decodeParams = {};
    decodeParams.order = AMBISONICS_ORDER;
    decodeParams.hrtf = m_hrtf;
    decodeParams.orientation = m_listenerCoords;
    decodeParams.binaural = psSoundFlags.test(ss_SA_Binaural) ? IPL_TRUE : IPL_FALSE;

    iplAmbisonicsDecodeEffectApply(m_decoder, &decodeParams, &m_ambisonicsBuffer, &m_stereoBuffer);

    // --- Output smoothing cache ---
    // The dry bus is sparse (~once per 24 frames per source), so raw convolution
    // output flickers. The cache exponentially blends toward new output and gently
    // decays when the dry bus is empty, providing temporal continuity.
    {
        // Measure current stereo output energy to detect contributions
        float stereoEnergy = 0.0f;
        for (int i = 0; i < m_frameSize; i++)
        {
            float l = m_stereoChannels[0][i];
            float r = m_stereoChannels[1][i];
            stereoEnergy += l * l + r * r;
        }
        bool hasContribution = (stereoEnergy > 1e-12f);

        // Time step — frameSize / sampleRate gives seconds per frame
        float dt = (float)m_frameSize / (float)m_sampleRate;

        float* cacheL = m_cachedStereoChannels[0];
        float* cacheR = m_cachedStereoChannels[1];
        float* newL = m_stereoChannels[0];
        float* newR = m_stereoChannels[1];

        if (hasContribution)
        {
            if (!m_hasCachedOutput)
            {
                // First contribution ever — snap cache to current output
                memcpy(cacheL, newL, m_frameSize * sizeof(float));
                memcpy(cacheR, newR, m_frameSize * sizeof(float));
                m_hasCachedOutput = true;
            }
            else
            {
                // Blend cache toward new output: cache += (new - cache) * alpha
                float alpha = 1.0f - expf(-REVERB_BLEND_RATE * dt);
                for (int i = 0; i < m_frameSize; i++)
                {
                    cacheL[i] += (newL[i] - cacheL[i]) * alpha;
                    cacheR[i] += (newR[i] - cacheR[i]) * alpha;
                }
            }
        }
        else if (m_hasCachedOutput)
        {
            // No contribution this frame — gently decay the cache
            float decay = expf(-REVERB_DECAY_RATE * dt);
            for (int i = 0; i < m_frameSize; i++)
            {
                cacheL[i] *= decay;
                cacheR[i] *= decay;
            }
        }

        // Debug: measure output energy
        if (g_SA_DebugLogging && (m_dbgFrameCount % 60 == 0))
        {
            float cacheEnergy = 0.0f;
            for (int i = 0; i < m_frameSize; i++)
                cacheEnergy += cacheL[i] * cacheL[i] + cacheR[i] * cacheR[i];
            Msg("STEAM_AUDIO: [DIAG] Stereo energy: %.8f, Cache energy: %.8f, binaural: %s, wetLevel: %.2f",
                stereoEnergy, cacheEnergy, decodeParams.binaural ? "ON" : "OFF", m_wetLevel);
        }
    }

    // Convert smoothed cache to interleaved s16 for OpenAL
    {
        float* left = m_cachedStereoChannels[0];
        float* right = m_cachedStereoChannels[1];
        float wet = m_wetLevel;
        const float scale = 32767.0f * wet;

        for (int i = 0; i < m_frameSize; i++)
        {
            float l = left[i] * scale;
            float r = right[i] * scale;
            if (l > 32767.0f) l = 32767.0f;
            if (l < -32768.0f) l = -32768.0f;
            if (r > 32767.0f) r = 32767.0f;
            if (r < -32768.0f) r = -32768.0f;
            m_interleavedOutput[i * 2 + 0] = (s16)l;
            m_interleavedOutput[i * 2 + 1] = (s16)r;
        }
    }

    // Check for processed buffers to recycle
    ALint processed = 0;
    alGetSourcei(m_alSource, AL_BUFFERS_PROCESSED, &processed);

    while (processed > 0)
    {
        ALuint bufferID;
        alSourceUnqueueBuffers(m_alSource, 1, &bufferID);
        processed--;

        alBufferData(bufferID, AL_FORMAT_STEREO16, m_interleavedOutput.data(),
                     m_frameSize * 2 * sizeof(s16), m_sampleRate);
        alSourceQueueBuffers(m_alSource, 1, &bufferID);
    }

    // Initial buffer fill - queue silence to start streaming
    if (!m_buffersQueued)
    {
        memset(m_interleavedOutput.data(), 0, m_interleavedOutput.size() * sizeof(s16));

        for (int i = 0; i < NUM_REVERB_BUFFERS; i++)
        {
            alBufferData(m_alBuffers[i], AL_FORMAT_STEREO16, m_interleavedOutput.data(),
                         m_frameSize * 2 * sizeof(s16), m_sampleRate);
            alSourceQueueBuffers(m_alSource, 1, &m_alBuffers[i]);
        }
        m_buffersQueued = true;
        alSourcePlay(m_alSource);
    }

    // Ensure source is playing
    ALint state;
    alGetSourcei(m_alSource, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING)
    {
        if (g_SA_DebugLogging)
            Msg("STEAM_AUDIO: [DIAG] Reverb AL source stopped — restarting");
        alSourcePlay(m_alSource);
    }
}
