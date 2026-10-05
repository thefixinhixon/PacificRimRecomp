// archiveimport.cpp - see archiveimport.h.
#include "archiveimport.h"

#include "stfs/stfspackage.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace archiveimport {

namespace {

QByteArray readHead(const QString &path, qint64 n)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.read(n);
}

bool isStfsMagic(const QByteArray &head)
{
    return head.startsWith("LIVE") || head.startsWith("PIRS") ||
           head.startsWith("CON ");
}

// 7z exit code 1 means "warning" (e.g. minor header gripes in
// third-party RARs) with the payload normally intact, so 0 and 1
// both count as success -- the caller verifies results on disk.
bool runUnpacker(const QString &exe, const QStringList &args,
                 QString *error)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(exe, args);
    if (!proc.waitForStarted(10000)) {
        *error = QStringLiteral("Could not start the archive tool (%1).")
                     .arg(exe);
        return false;
    }
    if (!proc.waitForFinished(15 * 60 * 1000)) {
        proc.kill();
        *error = QStringLiteral("Unpacking the archive timed out.");
        return false;
    }
    const int code = proc.exitCode();
    if (code != 0 && code != 1) {
        const QString tail =
            QString::fromLocal8Bit(proc.readAll()).trimmed().right(300);
        *error = QStringLiteral("The archive tool failed (exit %1).\n%2")
                     .arg(code)
                     .arg(tail);
        return false;
    }
    return true;
}

} // namespace

bool isArchiveFile(const QString &path)
{
    const QByteArray head = readHead(path, 8);
    if (head.startsWith("PK\x03\x04") || head.startsWith("PK\x05\x06"))
        return true; // zip
    if (head.startsWith("Rar!"))
        return true; // rar
    // 7z magic: 37 7A BC AF 27 1C
    if (head.size() >= 6 &&
        static_cast<unsigned char>(head[0]) == 0x37 &&
        static_cast<unsigned char>(head[1]) == 0x7A &&
        static_cast<unsigned char>(head[2]) == 0xBC &&
        static_cast<unsigned char>(head[3]) == 0xAF &&
        static_cast<unsigned char>(head[4]) == 0x27 &&
        static_cast<unsigned char>(head[5]) == 0x1C)
        return true;
    return false;
}

QString sevenZipExecutable()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList names{QStringLiteral("7zz"), QStringLiteral("7z")};
    for (const QString &name : names) {
        // Bundled copy first. Windows binaries carry an .exe suffix
        // (the Windows package ships 7z.exe + 7z.dll), so probe both
        // spellings; on other platforms the suffixed name just misses.
        for (const QString &cand : {name, name + QStringLiteral(".exe")}) {
            const QString bundled = QDir(appDir).filePath(cand);
            if (QFileInfo(bundled).isExecutable())
                return bundled;
        }
        const QString onPath = QStandardPaths::findExecutable(name);
        if (!onPath.isEmpty())
            return onPath;
    }
    return {};
}

Resolved resolveArchive(const QString &archivePath,
                        const QString &scratchDir,
                        const QString &wantTitleId)
{
    Resolved out;
    const QString exe = sevenZipExecutable();
    if (exe.isEmpty()) {
        out.error = QStringLiteral(
            "No archive tool found. The launcher needs 7-Zip to open "
            ".rar/.zip archives (release packages bundle it next to "
            "the launcher; a system 7-Zip also works).");
        return out;
    }

    QString error;
    const QStringList args{QStringLiteral("x"), QStringLiteral("-y"),
                           QStringLiteral("-o") + scratchDir,
                           QStringLiteral("--"), archivePath};
    if (!runUnpacker(exe, args, &error)) {
        out.error = error;
        return out;
    }

    // Walk the unpacked tree once: find an extracted game folder and
    // every STFS-magic file.
    QString folderRoot;
    int folderDepth = 1 << 30;
    struct Candidate {
        QString path;
        qint64 size = 0;
    };
    QVector<Candidate> candidates;
    QDirIterator it(scratchDir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.fileName().compare(QStringLiteral("default.xex"),
                                  Qt::CaseInsensitive) == 0) {
            const int depth = p.count(QLatin1Char('/'));
            if (depth < folderDepth) {
                folderDepth = depth;
                folderRoot = fi.absolutePath();
            }
            continue;
        }
        if (fi.size() >= 4096 && isStfsMagic(readHead(p, 4)))
            candidates.push_back({p, fi.size()});
    }

    if (!folderRoot.isEmpty()) {
        out.ok = true;
        out.kind = QStringLiteral("folder");
        out.path = folderRoot;
        return out;
    }

    // Prefer a package whose own title id matches the profile's;
    // otherwise the largest one that parses.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) {
                  return a.size > b.size;
              });
    QString firstParseable;
    for (const Candidate &c : candidates) {
        StfsPackage probe;
        QString perr;
        if (!probe.open(c.path, &perr))
            continue;
        if (!wantTitleId.isEmpty() &&
            probe.titleId().compare(wantTitleId, Qt::CaseInsensitive) == 0) {
            out.ok = true;
            out.kind = QStringLiteral("stfs");
            out.path = c.path;
            return out;
        }
        if (firstParseable.isEmpty())
            firstParseable = c.path;
    }
    if (!firstParseable.isEmpty()) {
        out.ok = true;
        out.kind = QStringLiteral("stfs");
        out.path = firstParseable;
        return out;
    }

    out.error = QStringLiteral(
        "The archive unpacked, but it contains no XBLA package and no "
        "extracted game folder (default.xex) -- nothing to import.");
    return out;
}

} // namespace archiveimport
