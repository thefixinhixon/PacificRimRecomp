// gamedetect.h - find the folder a profile's game lives in.
//
// A folder counts as the game root when it holds game data for this
// title: default.xex under gamedata/ or at the top level, with the
// title id embedded in the xex matching the profile whenever it can
// be read (presence alone would confuse the several recomp folders
// sharing one Games library). The exe of the profile is NOT required
// in the root - program files are resolved separately (launcher.h).
// The found root is remembered in QSettings and RE-VALIDATED on
// every startup - a root that no longer checks out is discarded and
// searched for again (a previous launcher trusted a stale root and
// launched from /tmp).
#pragma once

#include <QString>
#include <QVector>

struct GameProfile;

// Full validity check for a candidate folder.
bool isGameRoot(const GameProfile &profile, const QString &dir);

// The stored root for this profile, or empty if none/invalid.
QString storedGameRoot(const GameProfile &profile);
void storeGameRoot(const GameProfile &profile, const QString &dir);

// Stored root if still valid, else a fresh search. Stores fresh hits.
QString resolveGameRoot(const GameProfile &profile);

// The search itself (also used by a future "browse/rescan" action).
QString searchGameRoot(const GameProfile &profile);

// Every folder that currently validates as a game root for this
// profile, in a deterministic order: the stored root first, then the
// same bases the search uses - but collecting ALL hits, with mount
// points sorted by name so the order never depends on filesystem
// enumeration order. Used to locate a complete executable set when
// the active root holds game data only (e.g. right after an import).
QVector<QString> candidateGameRoots(const GameProfile &profile);
