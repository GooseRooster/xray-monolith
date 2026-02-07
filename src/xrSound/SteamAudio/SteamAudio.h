#pragma once

#include <phonon.h>

// Forward declare to avoid header pollution
class CSteamAudioScene;
class CSteamAudioSimulator;
class CSteamAudioReverb;

/**
 * CSteamAudio - Singleton managing Steam Audio context and global state.
 *
 * Responsible for:
 * - IPLContext creation and destruction
 * - Audio settings configuration
 * - Global enable/disable state
 */
class CSteamAudio
{
public:
    static CSteamAudio& Instance();

    // Lifecycle
    bool Initialize();
    void Shutdown();
    bool IsAvailable() const { return m_bInitialized; }

    // Accessors for child systems
    IPLContext GetContext() const { return m_context; }
    const IPLAudioSettings& GetAudioSettings() const { return m_audioSettings; }

    // Error tracking for rate-limited logging
    static int GetValidationErrorCount() { return s_validationErrorCount; }
    static void ResetValidationErrorCount() { s_validationErrorCount = 0; }

private:
    CSteamAudio();
    ~CSteamAudio();

    // Non-copyable
    CSteamAudio(const CSteamAudio&) = delete;
    CSteamAudio& operator=(const CSteamAudio&) = delete;

    // Steam Audio log callback with rate limiting
    static void IPLCALL LogCallback(IPLLogLevel level, const char* message);

    bool m_bInitialized = false;
    IPLContext m_context = nullptr;
    IPLAudioSettings m_audioSettings = {};

    // Rate-limiting for validation error logging
    static int s_validationErrorCount;
    static float s_lastErrorLogTime;
    static constexpr float ERROR_LOG_INTERVAL = 1.0f;  // Log at most once per second
    static constexpr int MAX_ERRORS_BEFORE_WARNING = 100;
};
