// dlcimport.cpp - see dlcimport.h.
#include "dlcimport.h"

#include "archiveimport.h"
#include "stfs/stfspackage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

namespace {

quint32 be32(const QByteArray &b, int off)
{
    return (quint32(quint8(b[off])) << 24) |
           (quint32(quint8(b[off + 1])) << 16) |
           (quint32(quint8(b[off + 2])) << 8) |
           quint32(quint8(b[off + 3]));
}

quint64 be64(const QByteArray &b, int off)
{
    return (quint64(be32(b, off)) << 32) | be32(b, off + 4);
}

void putBe32(QByteArray &b, int off, quint32 v)
{
    b[off] = char((v >> 24) & 0xFF);
    b[off + 1] = char((v >> 16) & 0xFF);
    b[off + 2] = char((v >> 8) & 0xFF);
    b[off + 3] = char(v & 0xFF);
}

void putLe32(QByteArray &b, quint32 v)
{
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
    b.append(char((v >> 16) & 0xFF));
    b.append(char((v >> 24) & 0xFF));
}

bool isStfsFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(4);
    return head.startsWith("LIVE") || head.startsWith("PIRS") ||
           head.startsWith("CON ");
}

// Same 7-Zip contract as archiveimport::resolveArchive: exit 0 and 1
// both mean the payload landed (1 = warnings only).
bool runUnpacker(const QString &exe, const QString &archive,
                 const QString &destDir, QString *error)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    const QStringList args{QStringLiteral("x"), QStringLiteral("-y"),
                           QStringLiteral("-o") + destDir,
                           QStringLiteral("--"), archive};
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

// Build the .header record for one package (see the header comment
// in dlcimport.h). Layout mirrors XCONTENT_AGGREGATE_DATA as the SDK
// writes it: device id 1 (HDD), content type, display name UTF-16BE
// at 0x008, file name ASCII at 0x108, title id at 0x140.
QByteArray buildHeaderRecord(const dlcimport::PackageInfo &info)
{
    QByteArray buf(0x148, '\0');
    putBe32(buf, 0x000, 1); // device_id: HDD
    putBe32(buf, 0x004, info.contentType);
    const QString display = info.displayName.left(127);
    for (int i = 0; i < display.size(); ++i) {
        const ushort u = display[i].unicode();
        buf[0x008 + i * 2] = char((u >> 8) & 0xFF);
        buf[0x008 + i * 2 + 1] = char(u & 0xFF);
    }
    const QByteArray name = info.fileName.toLatin1().left(42);
    for (int i = 0; i < name.size(); ++i)
        buf[0x108 + i] = name[i];
    putBe32(buf, 0x140, info.titleId.toUInt(nullptr, 16));
    if (info.licenseMask != 0)
        putLe32(buf, info.licenseMask); // host-order trailer
    return buf;
}

bool installOne(const QString &pkgPath, const QString &userDataRoot,
                const QString &wantTitleId, dlcimport::Outcome *outcome,
                const dlcimport::ProgressCallback &progress)
{
    const QString base = QFileInfo(pkgPath).fileName();
    dlcimport::PackageInfo info;
    QString error;
    if (!dlcimport::probePackage(pkgPath, &info, &error)) {
        outcome->skipped.append(QStringLiteral("%1: %2").arg(base, error));
        return false;
    }
    if (info.titleId != wantTitleId.toUpper()) {
        outcome->skipped.append(
            QStringLiteral("%1: package is for title %2, not %3")
                .arg(base, info.titleId, wantTitleId.toUpper()));
        return false;
    }
    if (progress)
        progress(info.displayName);

    const QString typeDir =
        QStringLiteral("%1").arg(info.contentType, 8, 16, QLatin1Char('0'))
            .toUpper();
    const QString titleRoot =
        QDir(userDataRoot)
            .filePath(QStringLiteral("0000000000000000"))
            + QLatin1Char('/') + info.titleId;
    const QString destDir =
        QDir(QDir(titleRoot).filePath(typeDir)).filePath(info.fileName);
    const QString headerPath =
        QDir(QDir(QDir(titleRoot).filePath(QStringLiteral("Headers")))
                 .filePath(typeDir))
            .filePath(info.fileName + QStringLiteral(".header"));

    // Re-import replaces: a stale partial extraction would confuse
    // the content system more than a clean one.
    if (QFileInfo::exists(destDir))
        QDir(destDir).removeRecursively();

    StfsPackage pkg;
    if (!pkg.open(pkgPath, &error)) {
        outcome->skipped.append(QStringLiteral("%1: %2").arg(base, error));
        return false;
    }
    if (!pkg.extractAll(destDir, {}, &error)) {
        outcome->skipped.append(QStringLiteral("%1: %2").arg(base, error));
        return false;
    }

    QDir().mkpath(QFileInfo(headerPath).absolutePath());
    QFile headerFile(headerPath);
    if (!headerFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        outcome->skipped.append(
            QStringLiteral("%1: could not write its content header")
                .arg(base));
        return false;
    }
    headerFile.write(buildHeaderRecord(info));
    headerFile.close();

    ++outcome->installed;
    outcome->names.append(info.displayName);
    return true;
}

} // namespace

namespace dlcimport {

bool probePackage(const QString &path, PackageInfo *info, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("could not open the file");
        return false;
    }
    const QByteArray head = f.read(0x2000);
    if (head.size() < 0x494 ||
        !(head.startsWith("LIVE") || head.startsWith("PIRS") ||
          head.startsWith("CON "))) {
        if (error)
            *error = QStringLiteral("not an STFS package");
        return false;
    }
    info->fileName = QFileInfo(path).fileName();
    info->titleId =
        QStringLiteral("%1").arg(be32(head, 0x360), 8, 16, QLatin1Char('0'))
            .toUpper();
    info->contentType = be32(head, 0x344);
    // License table: 16 records of {u64 licensee, u32 bits, u32
    // flags} at 0x22C; the aggregated mask is the OR of the bits of
    // every flagged entry.
    quint32 mask = 0;
    for (int i = 0; i < 16; ++i) {
        const int off = 0x22C + i * 16;
        const quint32 bits = be32(head, off + 8);
        const quint32 flags = be32(head, off + 12);
        if (flags)
            mask |= bits;
    }
    info->licenseMask = mask;
    // Display name: ASCII-range UTF-16LE pairs at 0x412 (this is the
    // field layout the verified DLC tooling read; the name is also
    // what lands in the .header record).
    QString display;
    for (int i = 0; i < 64; ++i) {
        const quint8 lo = quint8(head[0x412 + i * 2]);
        const quint8 hi = quint8(head[0x412 + i * 2 + 1]);
        if (hi != 0 || lo == 0 || lo < 0x20 || lo >= 0x7F)
            break;
        display.append(QChar(lo));
    }
    info->displayName = display.isEmpty() ? info->fileName : display;
    return true;
}

bool importDlc(const QString &pickedPath, const QString &userDataRoot,
               const QString &wantTitleId, const ProgressCallback &progress,
               Outcome *outcome, QString *error)
{
    QStringList packages;
    QTemporaryDir scratch; // Archive payload, when the pick is one

    const QFileInfo picked(pickedPath);
    if (picked.isDir()) {
        QDirIterator walk(pickedPath, QDir::Files | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
        while (walk.hasNext()) {
            const QString candidate = walk.next();
            if (!isStfsFile(candidate))
                continue;
            PackageInfo info;
            if (probePackage(candidate, &info) &&
                info.titleId == wantTitleId.toUpper())
                packages.append(candidate);
        }
    } else if (archiveimport::isArchiveFile(pickedPath)) {
        const QString exe = archiveimport::sevenZipExecutable();
        if (exe.isEmpty()) {
            if (error)
                *error = QStringLiteral(
                    "No archive tool found. The launcher needs 7-Zip "
                    "to open .rar/.zip archives (release packages "
                    "bundle it next to the launcher; a system 7-Zip "
                    "also works).");
            return false;
        }
        if (!scratch.isValid()) {
            if (error)
                *error = QStringLiteral("Could not create a temp folder.");
            return false;
        }
        QString unpackError;
        if (!runUnpacker(exe, pickedPath, scratch.path(), &unpackError)) {
            if (error)
                *error = unpackError;
            return false;
        }
        QDirIterator walk(scratch.path(), QDir::Files | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
        while (walk.hasNext()) {
            const QString candidate = walk.next();
            if (!isStfsFile(candidate))
                continue;
            PackageInfo info;
            if (probePackage(candidate, &info) &&
                info.titleId == wantTitleId.toUpper())
                packages.append(candidate);
        }
    } else if (isStfsFile(pickedPath)) {
        // A package picked directly is installed (or reported) even
        // if its title differs - the user aimed at this exact file.
        packages.append(pickedPath);
    } else {
        if (error)
            *error = QStringLiteral(
                "That file is not an STFS package or an archive.");
        return false;
    }

    if (packages.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                "No downloadable-content packages for this title "
                "(%1) were found there.")
                         .arg(wantTitleId.toUpper());
        return false;
    }

    packages.sort();
    for (const QString &pkg : packages)
        installOne(pkg, userDataRoot, wantTitleId, outcome, progress);
    return outcome->installed > 0;
}

} // namespace dlcimport
