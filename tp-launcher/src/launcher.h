// launcher.h - turn a profile + settings + game root into a running game.
#pragma once

#include <QString>
#include <QStringList>

struct GameProfile;
struct GameSettings;

// Everything needed to start the game, assembled in one place so the UI
// (and last-command.txt) can show the exact command before it runs.
struct LaunchPlan {
    QString program;    // Full path to the game exe
    QStringList args;   // Ordered argument list
    QString workingDir; // Folder the game starts in (see buildLaunchPlan)
    QString dataRoot;   // Logs + per-user data anchor (defaults to game root)
    QString logDir;     // Effective log folder (settings log_dir, else
                        // <dataRoot>/logs): --log_file points inside it
                        // and last-command.txt is written there too
    QString commandLine; // Human-readable form, for display + the log file
    QStringList programFiles; // Matched-set file names for this
                              // profile (platform::programSetFiles);
                              // carried for error text

    // Executable-set resolution outcome (resolveExecutableSet). When
    // exeSetFound is false, program is only a best-effort path and
    // executeLaunchPlan reports exeSearched/exeIncomplete instead of
    // spawning anything.
    bool exeSetFound = false;
    QString exeDir;            // Folder the complete set was found in
    QStringList exeSearched;   // Every folder examined, in order
    QStringList exeIncomplete; // "dir (missing ...)" notes: exe seen
                               // there, but without the full set
};

// The game's program files: the exe plus the runtime libs it ships
// with (file names per platform, see platform::programSetFiles)
// form a MATCHED set -
// mixing files across build sets corrupts at runtime ("free():
// invalid pointer"), so the set is only ever taken whole from one
// folder, never pieced together.
struct ExeSetResolution {
    bool found = false;
    QString dir;            // Folder holding the complete set
    QString program;        // Full path to the exe (dir + exeName)
    QStringList searched;   // Every folder examined, in order
    QStringList incomplete; // Folders with the exe but missing libs
};

// The data root and the program files are resolved independently: an
// imported package produces a data-only root (gamedata/, no exe),
// while the exe set may live in the launcher's own folder (the
// AppImage layout) or in another install of the same game. Search
// order: (1) the game root itself, (2) the launcher's own folder,
// (3) every other validated candidate root for this profile.
ExeSetResolution resolveExecutableSet(const GameProfile &profile,
                                      const QString &gameRoot);

// gameDataRoot is where the game's data sits (gameRoot/gamedata when that
// exists, else the game root itself, matching isGameRoot's rule).
LaunchPlan buildLaunchPlan(const GameProfile &profile,
                           const GameSettings &settings,
                           const QString &gameRoot,
                           const QString &dataRoot);

// Write last-command.txt into the plan's log folder, then start the game
// detached (the launcher does not wait on it). Returns false + error
// text on failure.
bool executeLaunchPlan(const LaunchPlan &plan, QString *errorOut);
