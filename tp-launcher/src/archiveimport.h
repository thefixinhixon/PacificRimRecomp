// archiveimport.h - smart import: accept a .rar / .zip / .7z archive,
// unpack it, and locate the XBLA game inside it.
//
// Game archives in the wild are usually a RAR/ZIP wrapped around the
// stock XBLA content layout (Content/0000000000000000/<titleid>/
// 000D0000/<package>) -- sometimes around an already-extracted game
// folder instead. resolveArchive() unpacks with a 7z-family tool and
// reports which of the two it found, so the import flow in
// mainwindow.cpp can proceed as if the user had picked the package
// (or the folder) directly.
#pragma once

#include <QString>

namespace archiveimport {

// True when the file's magic bytes say zip / rar / 7z archive.
bool isArchiveFile(const QString &path);

// Path to a 7z-family executable: one bundled next to the launcher
// first (the AppImage ships 7zz there), then PATH (7zz, then 7z).
// Empty string when no unpacker is available.
QString sevenZipExecutable();

struct Resolved {
    bool ok = false;
    QString kind;  // "stfs": path is a package file to import.
                   // "folder": path is a dir whose CONTENTS are the
                   // game data (it holds default.xex).
    QString path;
    QString error; // Plain-language failure for the user.
};

// Unpack archivePath into scratchDir (must already exist; the caller
// owns cleanup) and find the game. When several STFS packages are
// present, one whose embedded title id equals wantTitleId wins;
// otherwise the largest package that parses. An extracted game folder
// (a directory containing default.xex) wins over packages: it is the
// game, already unpacked.
Resolved resolveArchive(const QString &archivePath,
                        const QString &scratchDir,
                        const QString &wantTitleId);

} // namespace archiveimport
