// platform.h - the launcher's operating-system seam.
//
// Everything that differs between Linux and Windows lives behind
// these three functions, so the rest of the launcher (detection,
// launch plans, the UI) is written once, in plain Qt. The Windows
// branches are written but NOT compiled on this machine - there is
// no Windows kit here - so they are kept deliberately simple and
// obviously correct; Phase 2 (see WINDOWS.md) builds and verifies
// them. Linux behavior is unchanged by construction: the Linux
// branches are the code that used to live inline in gamedetect.cpp
// and launcher.cpp, moved, not rewritten.
#pragma once

#include <QString>
#include <QStringList>

struct GameProfile;

namespace platform {

// Folders that may contain a Games library: detection tests the
// base itself and each immediate subfolder as a candidate game
// root. Order matters only as a tie-break - a folder must still
// hold the profile's own title (see isGameRoot) to count.
//   Linux:   ~/Games, /mnt/*/Games, /media/<user>/*/Games
//            (mount points enumerated in name order, so the list
//            never depends on readdir order)
//   Windows: %USERPROFILE%\Games plus <drive>:\Games for every
//            drive (game libraries usually live on a data drive)
QStringList gameRootSearchBases();

// The game's program files as ONE matched set: exe first, then the
// runtime libs that must sit next to it. Mixing files across build
// sets corrupts at runtime, so the set is only ever taken whole
// from one folder (see resolveExecutableSet in launcher.cpp).
//   Linux:   {<exeName>, librexruntime.so, librexgpu-xenos.so}
//   Windows: {<exeName>.exe, librexruntime.dll, librexgpu-xenos.dll}
//            (names UNCONFIRMED - see the TODO in platform.cpp)
QStringList programSetFiles(const GameProfile &profile);

// Pre-launch cleanup of orphaned guest-memory arenas, best effort.
//   Linux:   removes leaked /dev/shm/xenia_memory_* files (the
//            SIGBUS landmine - see platform.cpp).
//   Windows: no-op (returns true); the backing cannot leak there.
// Returns false only on platforms where a real cleanup ran and
// failed outright; the Linux cleanup is heuristic and always
// reports true (a missed orphan is harmless).
bool vacuumGuestMemory();

} // namespace platform
