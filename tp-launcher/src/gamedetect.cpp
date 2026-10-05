// gamedetect.cpp - see gamedetect.h.
#include "gamedetect.h"

#include "gameprofile.h"
#include "gamesettings.h"
#include "platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QtEndian>

namespace {

QString settingsKey(const GameProfile &profile)
{
    return QStringLiteral("detection/%1/root").arg(profile.titleId);
}

// A candidate is out of the question if it sits under the system temp
// dir - a stray default.xex in /tmp once made a launcher announce
// "Game: /tmp" and launch from there.
bool isUnderTempDir(const QString &canonicalDir)
{
    if (canonicalDir.isEmpty())
        return false;
    const QString temp = QDir(QDir::tempPath()).canonicalPath();
    return !temp.isEmpty() &&
           (canonicalDir == temp || canonicalDir.startsWith(temp + QStringLiteral("/")));
}

bool checkCandidate(const GameProfile &profile, const QString &dir)
{
    if (dir.isEmpty())
        return false;
    const QString canonical = QFileInfo(dir).canonicalFilePath();
    if (canonical.isEmpty() || isUnderTempDir(canonical))
        return false;
    return isGameRoot(profile, canonical);
}

// Test a base folder and each of its immediate subfolders.
QString searchInBase(const GameProfile &profile, const QString &base)
{
    const QDir dir(base);
    if (!dir.exists())
        return {};
    if (checkCandidate(profile, dir.absolutePath()))
        return QFileInfo(dir.absolutePath()).canonicalFilePath();
    const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &sub : subs) {
        const QString candidate = dir.absoluteFilePath(sub);
        if (checkCandidate(profile, candidate))
            return QFileInfo(candidate).canonicalFilePath();
    }
    return {};
}

} // namespace

namespace {

// Title id embedded in a default.xex, or empty when it cannot be read.
// XEX2 layout: big-endian throughout. The header at 0x14 counts the
// optional-header entries at 0x18 ({key, value} u32 pairs); entry key
// 0x00040006 is the Execution ID, its value is a file offset, and the
// title id is the big-endian u32 at offset+0x0C in that struct.
// Verified against the house copies of both games (the Splosion
// build has its Execution ID at 0x1050, past the first page).
QString xexTitleId(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray head = f.read(0x1000);
    if (head.size() < 0x18 || !head.startsWith(QByteArrayLiteral("XEX2")))
        return {};
    const auto u32At = [&head](qint64 off) -> quint32 {
        if (off + 4 > head.size())
            return 0;
        return qFromBigEndian<quint32>(head.constData() + off);
    };
    const quint32 entries = u32At(0x14);
    for (quint32 i = 0; i < entries && i < 256; ++i) {
        const qint64 at = 0x18 + i * 8;
        if (u32At(at) != 0x00040006u)
            continue;
        const quint32 execOff = u32At(at + 4);
        if (!f.seek(execOff + 0x0C))
            return {};
        const QByteArray tid = f.read(4);
        if (tid.size() != 4)
            return {};
        const quint32 value = qFromBigEndian<quint32>(tid.constData());
        return QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
    }
    return {};
}

} // namespace

bool isGameRoot(const GameProfile &profile, const QString &dir)
{
    if (dir.isEmpty())
        return false;
    const QDir d(dir);
    // A root is its game DATA: default.xex under gamedata/ or at the
    // top level. The exe of the profile is deliberately NOT required
    // here: the program files are resolved separately (see
    // resolveExecutableSet in launcher.cpp) because an imported
    // package produces a data-only root, and in the AppImage layout
    // the set lives with the launcher. Only the first xex that
    // exists is examined.
    QString xex = d.filePath(QStringLiteral("gamedata/default.xex"));
    if (!QFile::exists(xex)) {
        xex = d.filePath(QStringLiteral("default.xex"));
        if (!QFile::exists(xex))
            return false;
    }
    // Presence alone cannot tell titles apart: several recomp game
    // folders share one Games library and every one holds a
    // default.xex (on the house machine, condemned2-files sits next
    // to TheMaw and splosionman). When the embedded title id of the
    // xex can be read it must match the profile; an unreadable xex
    // falls back to presence-only so unusual-but-legit layouts
    // still count.
    const QString embedded = xexTitleId(xex);
    return embedded.isEmpty() ||
           embedded.compare(profile.titleId, Qt::CaseInsensitive) == 0;
}

QString storedGameRoot(const GameProfile &profile)
{
    const QString root = settingsObject()
                             ->value(settingsKey(profile))
                             .toString();
    return isGameRoot(profile, root) ? root : QString{};
}

void storeGameRoot(const GameProfile &profile, const QString &dir)
{
    QSettings *q = settingsObject();
    q->setValue(settingsKey(profile), dir);
    q->sync();
}

QString searchGameRoot(const GameProfile &profile)
{
    const QStringList bases = platform::gameRootSearchBases();

    for (const QString &base : bases) {
        const QString hit = searchInBase(profile, base);
        if (!hit.isEmpty())
            return hit;
    }

    // 4. Ancestors of the launcher's own folder - covers the launcher
    //    being dropped inside (or next to) the game folder itself.
    QDir up(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        if (checkCandidate(profile, up.absolutePath()))
            return up.canonicalPath();
        if (!up.cdUp())
            break;
    }
    return {};
}

QString resolveGameRoot(const GameProfile &profile)
{
    const QString stored = storedGameRoot(profile);
    if (!stored.isEmpty())
        return stored;
    const QString found = searchGameRoot(profile);
    if (!found.isEmpty())
        storeGameRoot(profile, found);
    return found;
}

QVector<QString> candidateGameRoots(const GameProfile &profile)
{
    QVector<QString> roots;
    const auto add = [&roots](const QString &dir) {
        if (!dir.isEmpty() && !roots.contains(dir))
            roots << dir;
    };

    // The stored root first - it is the user's current game.
    add(storedGameRoot(profile));

    // Same bases as searchGameRoot() (platform::gameRootSearchBases,
    // which enumerates mount points in name order), but collecting
    // every hit: candidate order must be deterministic, not readdir
    // order (on the house machine /mnt enumerates "striped" before
    // "sdb1" in readdir order).
    const QStringList bases = platform::gameRootSearchBases();

    // Like searchInBase, but keeps every candidate instead of the
    // first one.
    const auto collectInBase = [&profile, &add](const QString &base) {
        const QDir dir(base);
        if (!dir.exists())
            return;
        if (checkCandidate(profile, dir.absolutePath()))
            add(QFileInfo(dir.absolutePath()).canonicalFilePath());
        const QStringList subs =
            dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &sub : subs) {
            const QString candidate = dir.absoluteFilePath(sub);
            if (checkCandidate(profile, candidate))
                add(QFileInfo(candidate).canonicalFilePath());
        }
    };
    for (const QString &base : bases)
        collectInBase(base);

    // Launcher-folder ancestors, as in searchGameRoot().
    QDir up(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        if (checkCandidate(profile, up.absolutePath()))
            add(up.canonicalPath());
        if (!up.cdUp())
            break;
    }
    return roots;
}
