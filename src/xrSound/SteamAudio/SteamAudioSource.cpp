#include "stdafx.h"
#include "SteamAudioSource.h"
#include "SteamAudio.h"
#include "SteamAudioScene.h"
#include "../SoundRender_CoreA.h"

#include <atomic>

// Debug counters for tracking source lifecycle - helps diagnose memory leaks
static std::atomic<int> s_totalSourcesCreated{0};
static std::atomic<int> s_totalSourcesDestroyed{0};

// Global debug flag - controlled via snd_sa_debug console command
// When enabled, logs detailed Steam Audio diagnostics
int g_SA_DebugLogging = 0;

// Static accumulation buffer for convolution reverb — shared by all sources
static xr_vector<float> s_accumBuffer;
static int s_accumMaxSamples = 0;

CSteamAudioSource::CSteamAudioSource()
{
    ++s_totalSourcesCreated;
}

CSteamAudioSource::~CSteamAudioSource()
{
    Destroy();
    ++s_totalSourcesDestroyed;
}

// Debug function to log source lifecycle stats - call from console command or debug UI
void SteamAudioSource_LogStats()
{
    int created = s_totalSourcesCreated.load();
    int destroyed = s_totalSourcesDestroyed.load();
    int leaking = created - destroyed;
    Msg("STEAM_AUDIO: Source stats - Created: %d, Destroyed: %d, Leaking: %d",
        created, destroyed, leaking);
}

bool CSteamAudioSource::Initialize(CSteamAudioScene* scene)
{
    if (!scene || !scene->IsReady())
    {
        return false;
    }

    if (!CSteamAudio::Instance().IsAvailable())
    {
        return false;
    }

    IPLContext context = CSteamAudio::Instance().GetContext();
    IPLSimulator simulator = scene->GetSimulator();
    m_simulator = simulator;  // Cache for defensive removal in Destroy()
    const IPLAudioSettings& audioSettings = CSteamAudio::Instance().GetAudioSettings();

    // Per-source only needs DIRECT simulation (occlusion, transmission, air absorption).
    // Reflections are handled globally by the listener reverb probe in CSteamAudioReverb.
    IPLSimulationFlags simFlags = IPL_SIMULATIONFLAGS_DIRECT;

    IPLSourceSettings sourceSettings = {};
    sourceSettings.flags = simFlags;

    IPLerror error = iplSourceCreate(simulator, &sourceSettings, &m_source);
    if (error != IPL_STATUS_SUCCESS)
    {
        return false;
    }

    // Add source to simulator — use batched commit to avoid racing with simulation threads.
    iplSourceAdd(m_source, simulator);
    scene->MarkPendingCommit();

    // Create direct effect for audio processing
    IPLDirectEffectSettings effectSettings = {};
    effectSettings.numChannels = 1;  // Mono input

    IPLAudioSettings audioSettingsCopy = audioSettings;
    error = iplDirectEffectCreate(context, &audioSettingsCopy, &effectSettings, &m_directEffect);
    if (error != IPL_STATUS_SUCCESS)
    {
        iplSourceRemove(m_source, simulator);
        iplSourceRelease(&m_source);
        m_source = nullptr;
        return false;
    }

    // Initialize processing buffers (one frameSize each)
    m_inputBuffer.resize(audioSettings.frameSize);
    m_outputBuffer.resize(audioSettings.frameSize);

    // Initialize outputs to safe "no effect" defaults so the first frame isn't silent.
    m_outputs.direct.distanceAttenuation = 1.0f;
    m_outputs.direct.occlusion = 1.0f;
    m_outputs.direct.transmission[0] = 1.0f;
    m_outputs.direct.transmission[1] = 1.0f;
    m_outputs.direct.transmission[2] = 1.0f;
    m_outputs.direct.airAbsorption[0] = 1.0f;
    m_outputs.direct.airAbsorption[1] = 1.0f;
    m_outputs.direct.airAbsorption[2] = 1.0f;

    // Set default simulation inputs — direct only
    m_inputs.flags = simFlags;
    m_inputs.directFlags = (IPLDirectSimulationFlags)(
        IPL_DIRECTSIMULATIONFLAGS_DISTANCEATTENUATION |
        IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION |
        IPL_DIRECTSIMULATIONFLAGS_OCCLUSION |
        IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);

    m_inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
    m_inputs.occlusionRadius = 2.0f;
    m_inputs.numOcclusionSamples = psSA_OcclusionRays;
    m_inputs.numTransmissionRays = 4;

    m_inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;
    m_inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;

    return true;
}

void CSteamAudioSource::Destroy()
{
    if (m_directEffect)
    {
        iplDirectEffectRelease(&m_directEffect);
        m_directEffect = nullptr;
    }

    if (m_source)
    {
        // Defensively remove from simulator if still added
        if (m_simulator)
        {
            iplSourceRemove(m_source, m_simulator);
        }
        iplSourceRelease(&m_source);
        m_source = nullptr;
    }
    m_simulator = nullptr;

    m_inputBuffer.clear();
    m_outputBuffer.clear();
    m_outputsValid = false;
}

void CSteamAudioSource::UpdatePosition(const Fvector& pos, float minDistance, float listenerDist)
{
    m_position = pos;

    // Convert X-Ray left-handed to Steam Audio right-handed: negate Z
    m_inputs.source.origin.x = pos.x;
    m_inputs.source.origin.y = pos.y;
    m_inputs.source.origin.z = -pos.z;

    m_inputs.source.ahead.x = 0.0f;
    m_inputs.source.ahead.y = 0.0f;
    m_inputs.source.ahead.z = -1.0f;

    m_inputs.source.up.x = 0.0f;
    m_inputs.source.up.y = 1.0f;
    m_inputs.source.up.z = 0.0f;

    m_inputs.source.right.x = 1.0f;
    m_inputs.source.right.y = 0.0f;
    m_inputs.source.right.z = 0.0f;

    // Set minDistance from OGG metadata
    m_inputs.distanceAttenuationModel.minDistance = minDistance;

    // Scale occlusion radius with distance for reliable volumetric sampling.
    // At close range: use source physical radius (minDistance or 2m minimum).
    // At far range: expand sampling sphere so rays probe wider area.
    // Ramps from base at 10m to ~3x at 50m, capped at 8m.
    float baseRadius = std::max(2.0f, minDistance);
    float distFactor = 1.0f + 0.05f * std::max(0.0f, listenerDist - 10.0f);
    m_inputs.occlusionRadius = std::min(baseRadius * distFactor, 8.0f);

    // Push inputs to Steam Audio
    if (m_source)
    {
        iplSourceSetInputs(m_source, m_inputs.flags, &m_inputs);
    }
}

void CSteamAudioSource::FetchOutputs()
{
    if (m_source)
    {
        iplSourceGetOutputs(m_source, m_inputs.flags, &m_outputs);
        m_outputsValid = true;
    }
}

float CSteamAudioSource::GetOcclusion() const
{
    if (!m_outputsValid)
        return 1.0f;
    return m_outputs.direct.occlusion;
}

float CSteamAudioSource::GetSmoothedOcclusion(float dt)
{
    if (!m_outputsValid)
        return m_smoothedOcclusion;

    float raw = m_outputs.direct.occlusion;

    const float smoothingFast = 4.0f;
    const float smoothingSlow = 2.0f;

    float smoothingFactor = (raw < m_smoothedOcclusion) ? smoothingFast : smoothingSlow;
    float alpha = 1.0f - expf(-smoothingFactor * dt);

    m_smoothedOcclusion += (raw - m_smoothedOcclusion) * alpha;

    // Configurable occlusion floor via snd_steam_audio_occlusion_min cvar.
    // Default 0.0 allows full occlusion; transmission handles what leaks through.
    // Set to 0.15 to restore old behavior if full occlusion feels too aggressive.
    if (m_smoothedOcclusion < psSA_OcclusionMin)
        m_smoothedOcclusion = psSA_OcclusionMin;

    return m_smoothedOcclusion;
}

void CSteamAudioSource::GetTransmission(float out[3]) const
{
    if (!m_outputsValid)
    {
        out[0] = out[1] = out[2] = 1.0f;
        return;
    }
    out[0] = m_outputs.direct.transmission[0];
    out[1] = m_outputs.direct.transmission[1];
    out[2] = m_outputs.direct.transmission[2];
}

float CSteamAudioSource::GetDistanceAttenuation() const
{
    if (!m_outputsValid)
        return 1.0f;
    return m_outputs.direct.distanceAttenuation;
}

void CSteamAudioSource::InitAccumulationBuffer(int frameSize)
{
    s_accumMaxSamples = frameSize;
    s_accumBuffer.assign(frameSize, 0.0f);
}

void CSteamAudioSource::ClearAccumulationBuffer()
{
    if (!s_accumBuffer.empty())
        memset(s_accumBuffer.data(), 0, s_accumMaxSamples * sizeof(float));
}

float* CSteamAudioSource::GetAccumulationBuffer()
{
    return s_accumBuffer.empty() ? nullptr : s_accumBuffer.data();
}

int CSteamAudioSource::GetAccumulationSampleCount()
{
    return s_accumMaxSamples;
}

void CSteamAudioSource::ProcessBuffer(s16* buffer, int numSamples, int sampleRate)
{
    // Ensure we have fresh outputs before processing
    if (m_source && !m_outputsValid)
    {
        FetchOutputs();
    }

    if (!m_directEffect || !m_outputsValid)
        return;

    // Build flags based on console settings
    IPLDirectEffectFlags flags = (IPLDirectEffectFlags)0;

    // Air absorption — always apply when any SA processing is active
    if (psSoundFlags.test(ss_SA_Occlusion))
    {
        flags = (IPLDirectEffectFlags)(flags | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION);
    }

    // Transmission
    if (psSoundFlags.test(ss_SA_Transmission))
    {
        flags = (IPLDirectEffectFlags)(flags | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION);
    }

    // Short-circuit if no effects needed
    if (flags == 0)
    {
        return;
    }

    const IPLAudioSettings& audioSettings = CSteamAudio::Instance().GetAudioSettings();
    const int frameSize = audioSettings.frameSize;

    if (sampleRate != audioSettings.samplingRate)
    {
        static bool warned = false;
        if (!warned)
        {
            Msg("! STEAM_AUDIO: Sample rate mismatch (%d vs %d), processing anyway",
                sampleRate, audioSettings.samplingRate);
            warned = true;
        }
    }

    // Set up direct effect parameters
    IPLDirectEffectParams params = m_outputs.direct;
    params.flags = flags;

    // Simple chunked processing: process frameSize-aligned chunks, zero-pad the tail.
    // Direct effects are stateless per-sample gain multipliers, so zero-padding
    // the last partial chunk has no audible artifact (unlike stateful HRTF convolution).
    const float scale = 1.0f / 32768.0f;
    const float invScale = 32767.0f;

    for (int offset = 0; offset < numSamples; offset += frameSize)
    {
        int chunkSamples = std::min(frameSize, numSamples - offset);

        // Convert s16 to float
        for (int i = 0; i < chunkSamples; i++)
            m_inputBuffer[i] = buffer[offset + i] * scale;

        // Zero-pad partial chunk
        for (int i = chunkSamples; i < frameSize; i++)
            m_inputBuffer[i] = 0.0f;

        float* inPtr = m_inputBuffer.data();
        float* outPtr = m_outputBuffer.data();

        IPLAudioBuffer inBuf = {};
        inBuf.numChannels = 1;
        inBuf.numSamples = frameSize;
        inBuf.data = &inPtr;

        IPLAudioBuffer outBuf = {};
        outBuf.numChannels = 1;
        outBuf.numSamples = frameSize;
        outBuf.data = &outPtr;

        // Apply direct effect (transmission, air absorption)
        iplDirectEffectApply(m_directEffect, &params, &inBuf, &outBuf);

        // Accumulate first chunk into shared buffer for convolution reverb
        if (psSA_Convolution && !s_accumBuffer.empty() && offset == 0)
        {
            int count = std::min(chunkSamples, s_accumMaxSamples);
            for (int i = 0; i < count; i++)
                s_accumBuffer[i] += m_outputBuffer[i];
        }

        // Convert float back to s16 (only the real samples, not zero-padding)
        for (int i = 0; i < chunkSamples; i++)
        {
            float sample = m_outputBuffer[i] * invScale;
            if (sample > 32767.0f) sample = 32767.0f;
            if (sample < -32768.0f) sample = -32768.0f;
            buffer[offset + i] = (s16)sample;
        }
    }
}
