#include "stdafx.h"
#include "SteamAudioSource.h"
#include "SteamAudio.h"
#include "SteamAudioScene.h"
#include "SteamAudioReverb.h"
#include "../SoundRender_CoreA.h"

#include <atomic>

// Debug counters for tracking source lifecycle - helps diagnose memory leaks
static std::atomic<int> s_totalSourcesCreated{0};
static std::atomic<int> s_totalSourcesDestroyed{0};

// Global debug flag - controlled via snd_sa_debug console command
// When enabled, logs detailed Steam Audio diagnostics
int g_SA_DebugLogging = 0;

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

    // Initialize input buffers
    m_inputBuffer.resize(audioSettings.frameSize);
    m_outputBuffer.resize(audioSettings.frameSize);

    // Create binaural effect for direct sound HRTF spatialization
    IPLBinauralEffectSettings binauralSettings = {};
    binauralSettings.hrtf = CSteamAudio::Instance().GetHRTF();

    error = iplBinauralEffectCreate(context, &audioSettingsCopy, &binauralSettings, &m_binauralEffect);
    if (error == IPL_STATUS_SUCCESS)
    {
        m_hasBinauralEffect = true;

        // Allocate stereo output buffer for binaural processing
        m_stereoData.resize(2 * audioSettings.frameSize);
        m_stereoChannels.resize(2);
        m_stereoChannels[0] = m_stereoData.data();
        m_stereoChannels[1] = m_stereoData.data() + audioSettings.frameSize;
        m_stereoBuffer.numChannels = 2;
        m_stereoBuffer.numSamples = audioSettings.frameSize;
        m_stereoBuffer.data = m_stereoChannels.data();
    }
    else
    {
        // Non-fatal - source works without binaural, just uses standard panning
        m_hasBinauralEffect = false;
        m_binauralEffect = nullptr;
    }

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
    if (m_binauralEffect)
    {
        iplBinauralEffectRelease(&m_binauralEffect);
        m_binauralEffect = nullptr;
    }

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
    m_stereoData.clear();
    m_stereoChannels.clear();
    m_outputsValid = false;
    m_hasBinauralEffect = false;
}

void CSteamAudioSource::UpdatePosition(const Fvector& pos, float minDistance)
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

    // Scale occlusion radius with source size
    m_inputs.occlusionRadius = std::max(2.0f, minDistance);

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

    const float MIN_OCCLUSION = 0.15f;
    if (m_smoothedOcclusion < MIN_OCCLUSION)
        m_smoothedOcclusion = MIN_OCCLUSION;

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

bool CSteamAudioSource::ProcessBuffer(s16* buffer, int numSamples, int sampleRate,
                                      const Fvector& listenerPos, const Fvector& listenerDir, const Fvector& listenerUp)
{
    // Ensure we have fresh outputs before processing
    if (m_source && !m_outputsValid)
    {
        FetchOutputs();
    }

    if (!m_directEffect || !m_outputsValid)
        return false;

    // Build flags based on console settings
    IPLDirectEffectFlags flags = (IPLDirectEffectFlags)0;

    // Air absorption
    if (psSoundFlags.test(ss_SA_Occlusion) || psSoundFlags.test(ss_SA_Binaural))
    {
        flags = (IPLDirectEffectFlags)(flags | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION);
    }

    // Transmission
    if (psSoundFlags.test(ss_SA_Transmission))
    {
        flags = (IPLDirectEffectFlags)(flags | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION);
    }

    bool wantBinaural = (m_hasBinauralEffect && psSoundFlags.test(ss_SA_Binaural));

    // Short-circuit if no effects needed
    if (flags == 0 && !wantBinaural && !psSoundFlags.test(ss_SA_Reverb))
    {
        return false;
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

    // --- CHUNKED PROCESSING ---
    int alignedSamples = ((numSamples + frameSize - 1) / frameSize) * frameSize;

    if ((int)m_inputBuffer.size() < alignedSamples)
    {
        m_inputBuffer.resize(alignedSamples, 0.0f);
        m_outputBuffer.resize(alignedSamples, 0.0f);
    }

    // Convert s16 to float [-1, 1]
    const float scale = 1.0f / 32768.0f;
    for (int i = 0; i < numSamples; i++)
    {
        m_inputBuffer[i] = buffer[i] * scale;
    }
    for (int i = numSamples; i < alignedSamples; i++)
    {
        m_inputBuffer[i] = 0.0f;
    }

    // --- Contribute dry audio to reverb bus (first chunk only, pre-DirectEffect) ---
    // Scale by inverse-distance so far sounds contribute proportionally less reverb energy
    if (psSoundFlags.test(ss_SA_Reverb) && SoundRenderA)
    {
        CSteamAudioReverb* reverb = SoundRenderA->GetSteamReverb();
        if (reverb && reverb->IsInitialized())
        {
            float dx = m_position.x - listenerPos.x;
            float dy = m_position.y - listenerPos.y;
            float dz = m_position.z - listenerPos.z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            float minDist = m_inputs.distanceAttenuationModel.minDistance;
            float distGain = minDist / std::max(dist, minDist);
            reverb->AccumulateDryAudio(m_inputBuffer.data(), frameSize, distGain);
        }
    }

    // Set up direct effect parameters
    IPLDirectEffectParams params = m_outputs.direct;
    params.flags = flags;

    // Pre-compute binaural parameters
    IPLBinauralEffectParams binauralParams = {};
    if (wantBinaural)
    {
        IPLContext context = CSteamAudio::Instance().GetContext();

        IPLVector3 srcPos = {m_position.x, m_position.y, -m_position.z};
        IPLVector3 lstPos = {listenerPos.x, listenerPos.y, -listenerPos.z};
        IPLVector3 lstAhead = {listenerDir.x, listenerDir.y, -listenerDir.z};
        IPLVector3 lstUp = {listenerUp.x, listenerUp.y, -listenerUp.z};

        IPLVector3 direction = iplCalculateRelativeDirection(context, srcPos, lstPos, lstAhead, lstUp);

        float lenSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
        if (lenSq < 0.001f)
        {
            direction = {0.0f, 0.0f, 1.0f};
        }

        binauralParams.direction = direction;
        binauralParams.hrtf = CSteamAudio::Instance().GetHRTF();
        binauralParams.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
        binauralParams.peakDelays = nullptr;

        // Scale spatialBlend with distance
        {
            float dx = srcPos.x - lstPos.x;
            float dy = srcPos.y - lstPos.y;
            float dz = srcPos.z - lstPos.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            float minDist = m_inputs.distanceAttenuationModel.minDistance;

            if (distSq <= minDist * minDist)
            {
                binauralParams.spatialBlend = 1.0f;
            }
            else
            {
                float dist = sqrtf(distSq);
                float ratio = minDist / dist;
                static constexpr float BLEND_FLOOR = 0.3f;
                float falloff = ratio * ratio;
                binauralParams.spatialBlend = BLEND_FLOOR + (1.0f - BLEND_FLOOR) * falloff;
            }
        }

        if (g_SA_DebugLogging)
        {
            static int s_binauralLogCounter = 0;
            s_binauralLogCounter++;
            if (s_binauralLogCounter % 200 == 0)
            {
                Msg("STEAM_AUDIO: Binaural dir=(%.2f, %.2f, %.2f) blend=%.2f srcPos=(%.1f, %.1f, %.1f) "
                    "lstPos=(%.1f, %.1f, %.1f) lstAhead=(%.2f, %.2f, %.2f)",
                    direction.x, direction.y, direction.z,
                    binauralParams.spatialBlend,
                    srcPos.x, srcPos.y, srcPos.z,
                    lstPos.x, lstPos.y, lstPos.z,
                    lstAhead.x, lstAhead.y, lstAhead.z);
            }
        }
    }

    // --- Main chunk processing loop ---
    const float invScale = 32767.0f;

    for (int offset = 0; offset < alignedSamples; offset += frameSize)
    {
        float* chunkIn = m_inputBuffer.data() + offset;
        float* chunkOut = m_outputBuffer.data() + offset;

        IPLAudioBuffer inBuf = {};
        inBuf.numChannels = 1;
        inBuf.numSamples = frameSize;
        inBuf.data = &chunkIn;

        IPLAudioBuffer outBuf = {};
        outBuf.numChannels = 1;
        outBuf.numSamples = frameSize;
        outBuf.data = &chunkOut;

        // Apply direct effect (transmission, air absorption as applicable)
        iplDirectEffectApply(m_directEffect, &params, &inBuf, &outBuf);

        // Apply binaural HRTF: mono chunk → stereo chunk
        if (wantBinaural)
        {
            iplBinauralEffectApply(m_binauralEffect, &binauralParams, &outBuf, &m_stereoBuffer);

            int writeEnd = std::min(offset + frameSize, numSamples);
            for (int i = offset; i < writeEnd; i++)
            {
                int local = i - offset;
                float l = m_stereoChannels[0][local] * invScale;
                float r = m_stereoChannels[1][local] * invScale;

                if (l > 32767.0f) l = 32767.0f;
                if (l < -32768.0f) l = -32768.0f;
                if (r > 32767.0f) r = 32767.0f;
                if (r < -32768.0f) r = -32768.0f;

                buffer[i * 2 + 0] = (s16)l;
                buffer[i * 2 + 1] = (s16)r;
            }
        }
    }

    // Binaural path: stereo s16 was already written inside the chunk loop
    if (wantBinaural)
    {
        return true;  // Output is stereo
    }

    // Non-binaural path: convert mono float back to s16
    for (int i = 0; i < numSamples; i++)
    {
        float sample = m_outputBuffer[i] * invScale;
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;
        buffer[i] = (s16)sample;
    }

    return false;  // Output is mono
}
