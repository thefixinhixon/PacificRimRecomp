// gamesettings.h - launcher-wide user settings, persisted via QSettings.
//
// DEFAULTS LIVE HERE, in this struct, and nowhere else. The loader reads
// each key with the struct's own current value as the fallback
// (settings.value(key, s.field)), so the struct, the UI and the file
// can never disagree about a default. (Learned the hard way: a previous
// launcher had one default in the struct and another in the UI struct,
// and FSR silently came up wrong.)
#pragma once

#include <QString>

struct GameSettings {
    // --- Graphics ---
    // Resolution is an integer render-scale multiplier over the game's
    // 720p internal resolution (1 = 720p, 2 = 1440p, 3 = 2160p) - the
    // scheme the shipped 'Splosion Man launcher proved; fractional
    // scales are unproven. The post effect is stored as the runtime's
    // own swap_post_effect value string.
    int     resolutionScale = 1;                       // 1 / 2 / 3
    QString upscaling  = QStringLiteral("FSR");        // FSR / Bilinear
    QString swapPostEffect = QStringLiteral("fxaa");   // none / fxaa / fxaa_extreme
    int     anisotropic = 16;        // anisotropic_override; 0 = game's own setting
    bool    native2xMsaa = false;    // native_2x_msaa
    bool    asyncShaderCompilation = true; // async_shader_compilation
    int     pipelineThreads = 0;     // vulkan_pipeline_creation_threads; 0 = runtime default
#if defined(TP_GAME_REALSTEEL) || defined(TP_GAME_PACIFICRIM)
    // Real Steel: the runtime's vsync emulation runs this game's
    // fights at half speed (proven 2026-10-04), so this build
    // defaults vsync OFF. The runtime's 60 fps frame limiter still
    // paces the game. Users can re-enable it in settings.
    bool    vsync      = false;
#else
    bool    vsync      = true;
#endif
    int     frameLimit = 0;    // 0 = unlimited
    bool    windowed   = false;

    // --- Audio ---
    double  audioGain  = 1.0;  // Neutral defaults on purpose - not the
    int     audioHighpassHz = 0; // 0.50 / 100 Hz leftovers from Dante
    bool    audioForceStereo = false; // audio_force_stereo
    bool    audioFrontOnly   = false; // audio_front_only
    int     audioMaxQFrames  = 0;     // audio_maxqframes; 0 = runtime default
    bool    xmaWorker        = true;  // xma_worker

    // --- Storage & logs ---
    QString userDataRoot;                        // user_data_root; empty = runtime default
    QString logDir;                              // empty = <data root>/logs
    QString logLevel = QStringLiteral("info");   // info / off / debug

    // --- Advanced ---
    bool    sparseSharedMemory = false;        // vulkan_sparse_shared_memory
    bool    asyncSkipIncompleteFrames = false; // vulkan_async_skip_incomplete_frames
    int     userLanguage = 0; // user_language; 0 = runtime default
    int     userCountry  = 0; // user_country; 0 = runtime default

    static GameSettings load();
    void save() const;
};

// The one QSettings factory used everywhere (IniFormat, user scope).
class QSettings;
QSettings *settingsObject();
