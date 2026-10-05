// gamesettings.cpp - see gamesettings.h for the defaults policy.
#include "gamesettings.h"

#include <QCoreApplication>
#include <QSettings>

QSettings *settingsObject()
{
    // Org/app names are set in main.cpp from the active profile, so
    // each game build gets its own ini file (~/.config/HouseRecomp/
    // <launcher name>.ini) and settings never bleed across games.
    // IniFormat explicitly: the file stays hand-editable.
    static QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                              QStringLiteral("HouseRecomp"),
                              QCoreApplication::applicationName());
    return &settings;
}

GameSettings GameSettings::load()
{
    GameSettings s; // defaults from the struct - the single source of truth
    QSettings *q = settingsObject();
    q->beginGroup(QStringLiteral("settings"));
    // Graphics
    s.resolutionScale = q->value(QStringLiteral("resolution_scale"), s.resolutionScale).toInt();
    s.upscaling       = q->value(QStringLiteral("upscaling"), s.upscaling).toString();
    s.swapPostEffect  = q->value(QStringLiteral("swap_post_effect"), s.swapPostEffect).toString();
    s.anisotropic     = q->value(QStringLiteral("anisotropic_override"), s.anisotropic).toInt();
    s.native2xMsaa    = q->value(QStringLiteral("native_2x_msaa"), s.native2xMsaa).toBool();
    s.asyncShaderCompilation = q->value(QStringLiteral("async_shader_compilation"), s.asyncShaderCompilation).toBool();
    s.pipelineThreads = q->value(QStringLiteral("vulkan_pipeline_creation_threads"), s.pipelineThreads).toInt();
    s.vsync           = q->value(QStringLiteral("vsync"), s.vsync).toBool();
    s.frameLimit      = q->value(QStringLiteral("frame_limit"), s.frameLimit).toInt();
    s.windowed        = q->value(QStringLiteral("windowed"), s.windowed).toBool();
    // Audio
    s.audioGain       = q->value(QStringLiteral("audio_gain"), s.audioGain).toDouble();
    s.audioHighpassHz = q->value(QStringLiteral("audio_highpass_hz"), s.audioHighpassHz).toInt();
    s.audioForceStereo = q->value(QStringLiteral("audio_force_stereo"), s.audioForceStereo).toBool();
    s.audioFrontOnly   = q->value(QStringLiteral("audio_front_only"), s.audioFrontOnly).toBool();
    s.audioMaxQFrames  = q->value(QStringLiteral("audio_maxqframes"), s.audioMaxQFrames).toInt();
    s.xmaWorker        = q->value(QStringLiteral("xma_worker"), s.xmaWorker).toBool();
    // Storage & logs
    s.userDataRoot    = q->value(QStringLiteral("user_data_root"), s.userDataRoot).toString();
    s.logDir          = q->value(QStringLiteral("log_dir"), s.logDir).toString();
    s.logLevel        = q->value(QStringLiteral("log_level"), s.logLevel).toString();
    // Advanced
    s.sparseSharedMemory = q->value(QStringLiteral("vulkan_sparse_shared_memory"), s.sparseSharedMemory).toBool();
    s.asyncSkipIncompleteFrames = q->value(QStringLiteral("vulkan_async_skip_incomplete_frames"), s.asyncSkipIncompleteFrames).toBool();
    s.userLanguage    = q->value(QStringLiteral("user_language"), s.userLanguage).toInt();
    s.userCountry     = q->value(QStringLiteral("user_country"), s.userCountry).toInt();
    q->endGroup();
    return s;
}

void GameSettings::save() const
{
    QSettings *q = settingsObject();
    q->beginGroup(QStringLiteral("settings"));
    // Graphics
    q->setValue(QStringLiteral("resolution_scale"), resolutionScale);
    q->setValue(QStringLiteral("upscaling"), upscaling);
    q->setValue(QStringLiteral("swap_post_effect"), swapPostEffect);
    q->setValue(QStringLiteral("anisotropic_override"), anisotropic);
    q->setValue(QStringLiteral("native_2x_msaa"), native2xMsaa);
    q->setValue(QStringLiteral("async_shader_compilation"), asyncShaderCompilation);
    q->setValue(QStringLiteral("vulkan_pipeline_creation_threads"), pipelineThreads);
    q->setValue(QStringLiteral("vsync"), vsync);
    q->setValue(QStringLiteral("frame_limit"), frameLimit);
    q->setValue(QStringLiteral("windowed"), windowed);
    // Audio
    q->setValue(QStringLiteral("audio_gain"), audioGain);
    q->setValue(QStringLiteral("audio_highpass_hz"), audioHighpassHz);
    q->setValue(QStringLiteral("audio_force_stereo"), audioForceStereo);
    q->setValue(QStringLiteral("audio_front_only"), audioFrontOnly);
    q->setValue(QStringLiteral("audio_maxqframes"), audioMaxQFrames);
    q->setValue(QStringLiteral("xma_worker"), xmaWorker);
    // Storage & logs
    q->setValue(QStringLiteral("user_data_root"), userDataRoot);
    q->setValue(QStringLiteral("log_dir"), logDir);
    q->setValue(QStringLiteral("log_level"), logLevel);
    // Advanced
    q->setValue(QStringLiteral("vulkan_sparse_shared_memory"), sparseSharedMemory);
    q->setValue(QStringLiteral("vulkan_async_skip_incomplete_frames"), asyncSkipIncompleteFrames);
    q->setValue(QStringLiteral("user_language"), userLanguage);
    q->setValue(QStringLiteral("user_country"), userCountry);
    q->endGroup();
    q->sync();
}
