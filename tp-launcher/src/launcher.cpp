// launcher.cpp - see launcher.h.
#include "launcher.h"

#include "gameprofile.h"
#include "gamedetect.h"
#include "gamesettings.h"
#include "platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

namespace {

QString quoteIfNeeded(const QString &arg)
{
    if (arg.contains(QLatin1Char(' ')))
        return QStringLiteral("\"%1\"").arg(arg);
    return arg;
}

// Boolean cvar value in the runtime's spelling.
QString boolValue(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

// The runtime's present effect for an upscaling choice. "fsr" only does
// anything because the SDK is built with REXGLUE_ENABLE_FIDELITYFX=ON -
// with it off, ParsePresentEffect silently falls back to bilinear (a
// previous launcher shipped an FSR toggle that did nothing for exactly
// that reason; verify features, not UI).
QString presentEffectFor(const QString &upscaling)
{
    return upscaling.compare(QStringLiteral("FSR"), Qt::CaseInsensitive) == 0
               ? QStringLiteral("fsr")
               : QStringLiteral("bilinear");
}

// Probe one folder for the full program set (the platform file
// list for this profile, see platform::programSetFiles). Returns
// the files missing from it; an empty list means the set is
// complete there.
QStringList missingFromSet(const GameProfile &profile, const QString &dir)
{
    QStringList missing;
    const QDir d(dir);
    for (const QString &file : platform::programSetFiles(profile))
        if (!QFileInfo(d.filePath(file)).isFile())
            missing << file;
    return missing;
}

} // namespace

ExeSetResolution resolveExecutableSet(const GameProfile &profile,
                                      const QString &gameRoot)
{
    ExeSetResolution result;

    // Search order (see launcher.h). Deduplicated on cleaned paths -
    // the game root is usually also a detection candidate.
    QStringList order;
    const auto queue = [&order](const QString &dir) {
        if (dir.isEmpty())
            return;
        const QString cleaned = QDir::cleanPath(dir);
        if (!order.contains(cleaned))
            order << cleaned;
    };
    queue(gameRoot);
    queue(QCoreApplication::applicationDirPath());
    for (const QString &root : candidateGameRoots(profile))
        queue(root);

    const QString exeFileName = platform::programSetFiles(profile).constFirst();
    for (const QString &dir : order) {
        result.searched << dir;
        const QStringList missing = missingFromSet(profile, dir);
        if (missing.isEmpty()) {
            result.found = true;
            result.dir = dir;
            result.program = QDir(dir).filePath(exeFileName);
            return result;
        }
        // An exe without its libs is the "copied the binary but not
        // the libs" mistake - name it in the report so it is
        // obvious why that folder did not count.
        if (!missing.contains(exeFileName)) {
            result.incomplete << QStringLiteral("%1 (missing %2)")
                                     .arg(dir, missing.join(QStringLiteral(", ")));
        }
    }
    return result;
}

LaunchPlan buildLaunchPlan(const GameProfile &profile,
                           const GameSettings &settings,
                           const QString &gameRoot,
                           const QString &dataRoot)
{
    LaunchPlan plan;
    plan.dataRoot = dataRoot;
    plan.programFiles = platform::programSetFiles(profile);

    // Resolve the program set independently of the data root: an
    // imported root holds gamedata/ only, and its exe set may live
    // in the launcher's folder or another install (launcher.h).
    const ExeSetResolution exeSet = resolveExecutableSet(profile, gameRoot);
    plan.exeSetFound = exeSet.found;
    plan.exeDir = exeSet.dir;
    plan.exeSearched = exeSet.searched;
    plan.exeIncomplete = exeSet.incomplete;
    plan.program = exeSet.found
                       ? exeSet.program
                       : QDir(gameRoot).filePath(
                             platform::programSetFiles(profile).constFirst());
    // Start in the exe's folder when the set lives elsewhere: the
    // runtime expects its libs/plugins next to the binary, and every
    // path that points at game data is absolute anyway. When the set
    // is in the game root this is the game root, as before.
    plan.workingDir = exeSet.found ? exeSet.dir : gameRoot;

    // Where the game's data sits - mirrors the validity rule in
    // gamedetect.cpp (gamedata/ wins when it holds the xex).
    const QString gamedataDir = QDir(gameRoot).filePath(QStringLiteral("gamedata"));
    const QString gameDataRoot =
        QFile::exists(QDir(gamedataDir).filePath(QStringLiteral("default.xex")))
            ? gamedataDir
            : gameRoot;

    QStringList args;

    // 1. Forced args from the profile (proven with the staged build).
    args << profile.forcedArgs;

    // 2. Derived args.
    args << QStringLiteral("--game_data_root=") + gameDataRoot;
    args << QStringLiteral("--present_effect=") + presentEffectFor(settings.upscaling);

    // The runtime's default log path sits next to the exe, which is a
    // read-only mount inside an AppImage - point it at the writable
    // data root instead (that abort cost a debugging session once).
    // Settings can redirect the log folder (log_dir, empty = a logs
    // folder under the data root); the effective folder rides on the
    // plan because last-command.txt is written there too.
    plan.logDir = settings.logDir.isEmpty()
                      ? QDir(dataRoot).filePath(QStringLiteral("logs"))
                      : settings.logDir;
    args << QStringLiteral("--log_file=") +
            QDir(plan.logDir).filePath(QStringLiteral("game.log"));
    args << QStringLiteral("--log_level=") + settings.logLevel;

    // Saves/profile root is a user choice too; unset = runtime default.
    if (!settings.userDataRoot.isEmpty())
        args << QStringLiteral("--user_data_root=") + settings.userDataRoot;

    // 3. Settings-derived args, grouped like the Settings page. The
    // cvar spellings come from the proven 'Splosion Man launcher (same
    // ReXGlue runtime family); each is to be verified observably in a
    // real PLAY run. Args whose value means "runtime default" (0 for
    // the int knobs) are omitted rather than passed explicitly.
    //
    // Graphics
    args << QStringLiteral("--resolution_scale=") +
            QString::number(settings.resolutionScale);
    args << QStringLiteral("--swap_post_effect=") + settings.swapPostEffect;
    if (settings.anisotropic != 0)
        args << QStringLiteral("--anisotropic_override=") +
                QString::number(settings.anisotropic);
    args << QStringLiteral("--native_2x_msaa=") + boolValue(settings.native2xMsaa);
    args << QStringLiteral("--async_shader_compilation=") +
            boolValue(settings.asyncShaderCompilation);
    if (settings.pipelineThreads != 0)
        args << QStringLiteral("--vulkan_pipeline_creation_threads=") +
                QString::number(settings.pipelineThreads);
    args << QStringLiteral("--vsync=") + boolValue(settings.vsync);
    // The runtime arg is fullscreen; the UI speaks windowed.
    args << QStringLiteral("--fullscreen=") + boolValue(!settings.windowed);
    args << QStringLiteral("--frame_limit=") +
            QString::number(settings.frameLimit);
    //
    // Audio
    args << QStringLiteral("--audio_output_gain=") +
            QString::number(settings.audioGain, 'f', 2);
    args << QStringLiteral("--audio_highpass_hz=") +
            QString::number(settings.audioHighpassHz);
    args << QStringLiteral("--audio_force_stereo=") +
            boolValue(settings.audioForceStereo);
    args << QStringLiteral("--audio_front_only=") +
            boolValue(settings.audioFrontOnly);
    if (settings.audioMaxQFrames != 0)
        args << QStringLiteral("--audio_maxqframes=") +
                QString::number(settings.audioMaxQFrames);
    args << QStringLiteral("--xma_worker=") + boolValue(settings.xmaWorker);
    //
    // Advanced. Sparse shared memory stays defaulted off: it
    // black-screens on AMD RADV (RX 6600), which is why both prior
    // house launchers forced it off; it is a user knob now, not a
    // hidden override, but the default is unchanged.
    args << QStringLiteral("--vulkan_sparse_shared_memory=") +
            boolValue(settings.sparseSharedMemory);
    args << QStringLiteral("--vulkan_async_skip_incomplete_frames=") +
            boolValue(settings.asyncSkipIncompleteFrames);
    if (settings.userLanguage != 0)
        args << QStringLiteral("--user_language=") +
                QString::number(settings.userLanguage);
    if (settings.userCountry != 0)
        args << QStringLiteral("--user_country=") +
                QString::number(settings.userCountry);

    plan.args = args;

    QStringList display;
    display << plan.program;
    for (const QString &a : args)
        display << quoteIfNeeded(a);
    plan.commandLine = display.join(QLatin1Char(' '));
    return plan;
}

bool executeLaunchPlan(const LaunchPlan &plan, QString *errorOut)
{
    // The plan's effective log folder (settings log_dir or the default
    // under the data root) - last-command.txt lives with the game log.
    const QString logsDir = plan.logDir.isEmpty()
                                ? QDir(plan.dataRoot).filePath(QStringLiteral("logs"))
                                : plan.logDir;
    if (!QDir().mkpath(logsDir)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not create log folder: %1").arg(logsDir);
        return false;
    }

    // Record the exact command for post-mortems ("what did we actually
    // launch?") - written before spawning, so it exists even if the
    // game crashes instantly.
    QFile cmdFile(QDir(logsDir).filePath(QStringLiteral("last-command.txt")));
    if (cmdFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&cmdFile);
        out << plan.commandLine << "\n";
    }

    // No complete set anywhere: say exactly where we looked (and
    // which folders had the exe but not the libs) instead of leaving
    // the user guessing.
    if (!plan.exeSetFound) {
        if (errorOut) {
            QStringList lines;
            lines << QStringLiteral("Game program files not found - %1, "
                                    "%2 and %3 must be together in one folder.")
                         .arg(QFileInfo(plan.program).fileName(),
                              plan.programFiles.value(1),
                              plan.programFiles.value(2));
            lines << QStringLiteral("Searched:");
            for (const QString &dir : plan.exeSearched)
                lines << QStringLiteral("  %1").arg(dir);
            if (!plan.exeIncomplete.isEmpty()) {
                lines << QStringLiteral("Had the exe but not the full set:");
                for (const QString &note : plan.exeIncomplete)
                    lines << QStringLiteral("  %1").arg(note);
            }
            *errorOut = lines.join(QLatin1Char('\n'));
        }
        return false;
    }

    // IsFile check gives a clearer error than a bare spawn failure.
    if (!QFileInfo(plan.program).isFile()) {
        if (errorOut)
            *errorOut = QStringLiteral("Game executable not found: %1").arg(plan.program);
        return false;
    }

    if (!QProcess::startDetached(plan.program, plan.args, plan.workingDir)) {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to start: %1").arg(plan.commandLine);
        return false;
    }
    return true;
}
