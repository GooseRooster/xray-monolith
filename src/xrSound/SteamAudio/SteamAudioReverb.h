#pragma once

#include <phonon.h>
#include <AL/al.h>

class CSteamAudioScene;

/**
 * CSteamAudioReverb - Global reverb via a single listener reverb probe.
 *
 * Instead of per-source reflection effects (which suffer from IR pre-delay
 * that makes transient sounds like gunshots produce zero reverb), this system
 * uses a single persistent IPLSource placed at the listener position.
 *
 * Architecture:
 *   Source A ─dry→ ┐
 *   Source B ─dry→ ┼→ DryBus → [ReflectionEffect] → Ambi → Stereo → OpenAL
 *   Source C ─dry→ ┘       ↑
 *                    ListenerProbe IR (always valid)
 *
 * Each source contributes its dry audio (distance-scaled) to a shared mono
 * dry bus via AccumulateDryAudio(). EndFrame() convolves the bus with the
 * listener probe's IR and streams the result to OpenAL.
 *
 * The convolution effect's internal overlap-save state naturally produces
 * reverb tails when dry input stops — no decay pool needed.
 */
class CSteamAudioReverb
{
public:
    CSteamAudioReverb();
    ~CSteamAudioReverb();

    // Initialize with scene and HRTF
    bool Initialize(CSteamAudioScene* scene, IPLHRTF hrtf);
    void Destroy();

    bool IsInitialized() const { return m_listenerSource != nullptr && m_alSource != 0; }

    // Frame lifecycle
    void BeginFrame();   // Zero-fill dry bus before sources contribute
    void EndFrame();     // Convolve dry bus with listener IR, stream to OpenAL

    // Sources call this to contribute dry audio to the shared reverb bus.
    // distanceGain = minDist / max(dist, minDist) so distant sounds contribute less.
    void AccumulateDryAudio(const float* data, int numSamples, float distanceGain);

    // Update listener probe position (called from update_listener each frame)
    void UpdateListenerProbe(const Fvector& pos, const Fvector& dir, const Fvector& up);

    // Update listener orientation for HRTF decode
    void SetListenerOrientation(const Fvector& forward, const Fvector& up);

    // Reverb parameters
    void SetReverbEnabled(bool enabled) { m_enabled = enabled; }
    bool IsReverbEnabled() const { return m_enabled; }

    void SetWetLevel(float wet) { m_wetLevel = wet; }
    float GetWetLevel() const { return m_wetLevel; }

private:
    // --- Listener reverb probe ---
    IPLSource m_listenerSource = nullptr;          // Persistent probe at listener position
    IPLSimulationInputs m_listenerInputs = {};     // Probe simulation inputs
    IPLSimulationOutputs m_listenerOutputs = {};   // Probe simulation outputs (contains IR)
    IPLReflectionEffect m_listenerEffect = nullptr; // Single convolution effect
    bool m_hasValidIR = false;                     // True once probe has usable IR

    // --- Dry bus ---
    // All sources accumulate their distance-scaled dry audio here each frame.
    xr_vector<float> m_dryBusData;
    IPLAudioBuffer m_dryBusBuffer = {};
    float* m_dryBusPtr = nullptr;  // Points to m_dryBusData.data() for IPLAudioBuffer

    // --- Steam Audio objects ---
    IPLAmbisonicsDecodeEffect m_decoder = nullptr;
    IPLHRTF m_hrtf = nullptr;

    // --- OpenAL reverb output source ---
    static constexpr int NUM_REVERB_BUFFERS = 3;
    ALuint m_alSource = 0;
    ALuint m_alBuffers[NUM_REVERB_BUFFERS] = {0};
    int m_sampleRate = 44100;

    // --- Ambisonics buffer (order 2 = 9 channels) ---
    static constexpr int AMBISONICS_ORDER = 2;
    static constexpr int AMBISONICS_CHANNELS = (AMBISONICS_ORDER + 1) * (AMBISONICS_ORDER + 1);  // 9

    static constexpr float MAX_REVERB_DURATION = 2.0f;  // seconds
    int m_irSize = 0;

    xr_vector<float> m_ambisonicsData;
    xr_vector<float*> m_ambisonicsChannels;
    IPLAudioBuffer m_ambisonicsBuffer = {};

    // --- Stereo output buffer (deinterleaved) ---
    xr_vector<float> m_stereoData;
    xr_vector<float*> m_stereoChannels;
    IPLAudioBuffer m_stereoBuffer = {};

    // --- Output smoothing cache ---
    // The dry bus is sparse (fill_block fires ~once per 24 frames per source),
    // so raw convolution output flickers. The cache provides temporal continuity:
    // - When new output arrives, blend cache toward it (REVERB_BLEND_RATE)
    // - When dry bus is silent, gently decay the cache (REVERB_DECAY_RATE)
    xr_vector<float> m_cachedStereoData;
    xr_vector<float*> m_cachedStereoChannels;
    bool m_hasCachedOutput = false;
    static constexpr float REVERB_BLEND_RATE = 4.0f;   // per second
    static constexpr float REVERB_DECAY_RATE = 1.0f;    // per second

    // --- Interleaved stereo for OpenAL (s16 format) ---
    xr_vector<s16> m_interleavedOutput;

    // --- Listener orientation for HRTF decode ---
    IPLCoordinateSpace3 m_listenerCoords = {};

    // --- State ---
    bool m_enabled = true;
    bool m_buffersQueued = false;
    float m_wetLevel = 1.0f;
    int m_frameSize = 0;

    // Cached simulator handle for cleanup
    IPLSimulator m_simulator = nullptr;

    // --- Debug stats ---
    int m_dbgFrameCount = 0;
};
