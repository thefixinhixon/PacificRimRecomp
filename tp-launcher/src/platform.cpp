// platform.cpp - see platform.h. Linux implementations are the
// pre-refactor inline code from gamedetect.cpp / launcher.cpp,
// moved verbatim in behavior; Windows branches wait for Phase 2.
#include "platform.h"

#include "gameprofile.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace platform {

QStringList gameRootSearchBases()
{
#if defined(Q_OS_WIN)
    QStringList bases;

    // The user's own Games folder, then <drive>:\Games for every
    // drive - game libraries usually live on a data drive, not C:.
    bases << QDir::home().filePath(QStringLiteral("Games"));
    for (const QFileInfo &drive : QDir::drives())
        bases << QDir(drive.absoluteFilePath()).filePath(QStringLiteral("Games"));
    return bases;
#else
    QStringList bases;

    // 1. ~/Games
    bases << QDir::home().filePath(QStringLiteral("Games"));

    // 2. /mnt/*/Games  (the house library drive: /mnt/sdb1/Games).
    //    Name order, not readdir order: the list must be
    //    deterministic (candidateGameRoots relies on it, and a
    //    search result should never depend on filesystem luck).
    const QStringList mntEntries =
        QDir(QStringLiteral("/mnt"))
            .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &e : mntEntries)
        bases << QStringLiteral("/mnt/%1/Games").arg(e);

    // 3. /media/<user>/*/Games  (udisks-mounted external drives)
    const QString user = QFileInfo(QDir::homePath()).fileName();
    const QString mediaUser = QStringLiteral("/media/") + user;
    const QStringList mediaEntries =
        QDir(mediaUser).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &e : mediaEntries)
        bases << mediaUser + QStringLiteral("/") + e + QStringLiteral("/Games");

    return bases;
#endif
}

QStringList programSetFiles(const GameProfile &profile)
{
#if defined(Q_OS_WIN)
    // Confirmed against the first green Windows CI build
    // (SplosionManRecomp run #8, 2026-10-03): the SDK stages
    // rexruntime.dll + rexgpu-xenos.dll -- no lib prefix, unlike
    // the Linux .so names. Every consumer goes through here.
    return {profile.exeName + QStringLiteral(".exe"),
            QStringLiteral("rexruntime.dll"),
            QStringLiteral("rexgpu-xenos.dll")};
#else
    return {profile.exeName,
            QStringLiteral("librexruntime.so"),
            QStringLiteral("librexgpu-xenos.so")};
#endif
}

bool vacuumGuestMemory()
{
#if defined(Q_OS_WIN)
    // Nothing to clean on Windows. The runtime backs guest RAM
    // with a named pagefile-backed mapping object there, not with
    // a file in a shared temp folder: the object is owned by the
    // game process and the kernel destroys it when the process
    // exits, crashed or not. The leaked-arena problem below is a
    // /dev/shm (tmpfs file) problem and has no Windows equivalent.
    // (Phase 1 reasoning only - no Windows build exists yet; if
    // the Windows runtime turns out to back guest RAM with real
    // files after all, this is the one place to revisit.)
    return true;
#else
    // The runtime backs guest RAM with a ~4.8 GB xenia_memory_* file
    // per run in /dev/shm. Crashed/killed runs leak them; once tmpfs
    // fills, the next run dies with SIGBUS right after "Guest memory
    // arena mapped". Jason's hand-written launch.sh just rm's them
    // all before launch, which is safe there because no game is
    // running yet. This launcher may be asked to relaunch while a
    // game is up, so be more careful:
    //
    //   1. Never touch a file any process has mapped (best effort:
    //      scan /proc/<pid>/maps; other users' maps may be
    //      unreadable, which is why rule 2 also exists).
    //   2. Never touch a file modified in the last 60 seconds (a
    //      game that just started may not have its arena in a maps
    //      file we could read).
    //   3. Only our own files can actually be removed -
    //      QFile::remove on another user's arena just fails, which
    //      is the safe outcome anyway.
    //
    // It is a heuristic, not a lock: worst realistic case is leaving
    // an orphan behind (harmless), not deleting a live arena.
    const QString shmDir = QStringLiteral("/dev/shm");
    const QStringList entries =
        QDir(shmDir).entryList({QStringLiteral("xenia_memory_*")}, QDir::Files);

    QStringList mapped;
    const QStringList pids =
        QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool numeric = false;
        pid.toInt(&numeric);
        if (!numeric)
            continue;
        QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
        if (!maps.open(QIODevice::ReadOnly | QIODevice::Text))
            continue; // not ours to inspect - rule 2 still protects it
        const QString content = QString::fromUtf8(maps.readAll());
        for (const QString &name : entries) {
            if (content.contains(name))
                mapped << name;
        }
    }

    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-60);
    for (const QString &name : entries) {
        if (mapped.contains(name))
            continue;
        const QFileInfo info(QDir(shmDir).filePath(name));
        if (info.lastModified() > cutoff)
            continue;
        QFile::remove(info.absoluteFilePath());
    }
    return true;
#endif
}

} // namespace platform
