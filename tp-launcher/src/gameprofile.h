// gameprofile.h - what the launcher knows about one recomp game.
//
// Profiles are compiled in (one per game). A profile is deliberately
// just data: detection, settings and launching are all written against
// it, so adding a game is a new entry here, not new code.
//
// Which profile a build serves is chosen at configure time by the
// TP_GAME CMake option (see CMakeLists.txt); activeProfile() resolves
// it via the TP_GAME_* compile definition the build sets.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct GameProfile {
    QString name;         // Display name, e.g. "The Maw"
    QString exeName;      // Game binary file name, e.g. "themaw"
    QString titleId;      // Xbox 360 title id, e.g. "584108C2"
    QStringList forcedArgs; // Always passed, in order (see launcher.cpp)
    QString accent;       // Theme accent; substituted into the .qss at startup
    QString launcherName; // Product name: window title + QSettings app name
    QString themeFile;    // Stylesheet resource, e.g. ":/theme/maw.qss"
};

// The Maw (Twisted Pixel, XBLA). Title id 584108C2.
//
// forcedArgs holds only what the user cannot change. The staged launch
// script also proved --vulkan_async_skip_incomplete_frames=false, but
// that flag is now a user setting (Advanced section, default off - the
// same value) so it is emitted from settings in launcher.cpp instead.
// --present_effect is NOT here either: it is derived from the user"s
// upscaling setting at launch time (launcher.cpp).
inline const GameProfile &mawProfile()
{
    static const GameProfile profile{
        QStringLiteral("The Maw"),
        QStringLiteral("themaw"),
        QStringLiteral("584108C2"),
        {
            QStringLiteral("--gpu_plugin=xenos"),
            QStringLiteral("--license_mask=1"),
        },
        QStringLiteral("#7a4fd0"), // theme accent (drives the .qss)
        QStringLiteral("TP Maw Launcher"),
        QStringLiteral(":/theme/maw.qss"),
    };
    return profile;
}

// 'Splosion Man (Twisted Pixel, XBLA). Title id 5841098F.
// Same runtime family and the same proven forced args as The Maw; the
// settings-derived flags (async-skip, sparse memory, ...) likewise come
// from settings in launcher.cpp.
inline const GameProfile &splosionProfile()
{
    static const GameProfile profile{
        QStringLiteral("'Splosion Man"),
        QStringLiteral("splosionman"),
        QStringLiteral("5841098F"),
        {
            QStringLiteral("--gpu_plugin=xenos"),
            QStringLiteral("--license_mask=1"),
        },
        QStringLiteral("#f06414"), // hot orange (drives the .qss)
        QStringLiteral("TP Splosionman Launcher"),
        QStringLiteral(":/theme/splosion.qss"),
    };
    return profile;
}

// Real Steel (Yuke's, XBLA). Title id 584111E0.
//
// --gpu_allow_invalid_fetch_constants=true is a title requirement, not
// a preference: the game issues texture fetches with 'invalid' fetch
// constants thousands of times per session, and without the runtime's
// tolerance flag those draws misrender (flickering/green textures -
// proven 2026-10-04). Vsync is the opposite story: it stays a user
// setting, but this build's DEFAULT is off (gamesettings.h) because
// the runtime's vsync emulation runs this game's fights at half
// speed; the 60 fps frame limiter still paces it.
inline const GameProfile &realsteelProfile()
{
    static const GameProfile profile{
        QStringLiteral("Real Steel"),
        QStringLiteral("realsteel"),
        QStringLiteral("584111E0"),
        {
            QStringLiteral("--gpu_plugin=xenos"),
            QStringLiteral("--license_mask=1"),
            QStringLiteral("--gpu_allow_invalid_fetch_constants=true"),
        },
        QStringLiteral("#1fb6c9"), // cage-light cyan (drives the .qss)
        QStringLiteral("Factual Metal Launcher"),
        QStringLiteral(":/theme/realsteel.qss"),
    };
    return profile;
}


// Pacific Rim (Yuke's). Title id 584112C1. Same engine framework as
// Real Steel, so the same proven forced args carry over (including the
// invalid-fetch-constants tolerance). Vsync defaults OFF for this
// build (gamesettings.h): every staged launch of this title has used
// --vsync=false, with the runtime's frame limiter pacing it, exactly
// as with Real Steel. The game's save data is integrity-hashed with
// XeCryptSha512 - the runtime implements it (house SDK, 2026-10-05);
// with the old stub the game rejected every save as corrupted.
inline const GameProfile &pacificrimProfile()
{
    static const GameProfile profile{
        QStringLiteral("Pacific Rim"),
        QStringLiteral("pacificrim"),
        QStringLiteral("584112C1"),
        {
            QStringLiteral("--gpu_plugin=xenos"),
            QStringLiteral("--license_mask=1"),
            QStringLiteral("--gpu_allow_invalid_fetch_constants=true"),
        },
        QStringLiteral("#d97c2e"), // burnt orange, off the title logo
        QStringLiteral("Shatterdome Launcher"),
        QStringLiteral(":/theme/pacificrim.qss"),
    };
    return profile;
}

// The profile this build serves, selected by the TP_GAME_* compile
// definition (CMakeLists.txt). Maw is the default/fallback.
#if defined(TP_GAME_SPLOSIONMAN)
inline const GameProfile &activeProfile()
{
    return splosionProfile();
}
#elif defined(TP_GAME_REALSTEEL)
inline const GameProfile &activeProfile()
{
    return realsteelProfile();
}
#elif defined(TP_GAME_PACIFICRIM)
inline const GameProfile &activeProfile()
{
    return pacificrimProfile();
}
#else
inline const GameProfile &activeProfile()
{
    return mawProfile();
}
#endif

inline QVector<GameProfile> allProfiles()
{
    return {mawProfile(), splosionProfile(), realsteelProfile(), pacificrimProfile()};
}
