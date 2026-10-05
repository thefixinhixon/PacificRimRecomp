// stfspackage.cpp - see stfspackage.h for the format notes.
//
// Layout summary (all confirmed against 584108C2033F0001, PIRS):
//
//   Header (common to LIVE/PIRS/CON, after the signature region):
//     0x340  u32 BE  headerSize     (0xAD0E in the reference file)
//     0x34C  u64 BE  contentSize    (payload region length)
//     0x360  u32 BE  titleId
//     0x379  volume descriptor (0x24 bytes):
//              +0x00 u8         descriptor size (0x24)
//              +0x04 u24 BE     file table block number (0)
//              +0x08 u8[20]     root hash: SHA-1 of the top hash table
//              +0x1C u32 BE     data block count (1964)
//              +0x20 u32 BE     data block offset (0)
//   Payload starts at headerSize rounded up to a 0x1000 boundary
//   (0xB000 in the reference file).
//
//   Blocks: payload is a sequence of data blocks of 0x1000 bytes,
//   grouped in 170s. Each group is PRECEDED by its level-0 hash table
//   block. Higher tables are wedged in at span boundaries (all
//   positions below SHA-1-confirmed against the full-size LIVE
//   package C5C556893A09D5B523C8080B21CCF09666EB1C1F58, 37486 data
//   blocks, on 2026-10-03 - see dataBlockOffset()):
//     - the level-1 table for span 0 sits after group 0, immediately
//       before level-0 table 1;
//     - when a volume needs a level-2 root (more than 28900 data
//       blocks), the root table and level-1 table 1 sit back to back
//       immediately before level-0 table 170:
//       [group 169 data][L2][L1 #1][L0 #170][group 170 data]...
//     - level-1 table j >= 2 likewise sits immediately before
//       level-0 table 170*j.
//   A hash table block holds 170 interleaved 24-byte entries - NOT a
//   packed hash array as some documentation claims:
//              entry = SHA-1 of the data block (20 bytes)
//                    + u32 BE info (0x80 = allocated, low 24 bits =
//                      next block number, 0xFFFFFF = end of chain)
//   (Info words are only meaningful in level-0 entries; upper-level
//   entries carry 0 there.)
//   File data is a chain: follow next pointers, do not assume files
//   are contiguous (the reference package happens to be packed, the
//   format does not promise it).
//
//   File table: a chain of data blocks starting at the descriptor"s
//   file table block number, 64 entries of 0x40 bytes per block:
//              +0x00 char name[0x28]
//              +0x28 u8   name length (low 6 bits) + flags
//                          (0x80 = directory; 0x40 also set on every
//                          entry of the reference package, meaning
//                          unclear - names are capped at 0x28 chars,
//                          so masking the length to 6 bits is safe)
//              +0x29 u24 LE blocks allocated   (LE, not BE!)
//              +0x2C u24 LE (identical to +0x29 in every entry of the
//                          reference package; unused here)
//              +0x2F u24 LE start block        (LE, not BE!)
//              +0x32 u16 BE parent entry index (0xFFFF = root)
//              +0x34 u32 BE file size
//              +0x38 u32    creation time (Xbox packed datetime)
//              +0x3C u32    last write time
//   The documentation"s big-endian claim for the two u24 fields is
//   wrong for real packages; the little-endian reading reproduces a
//   perfectly packed block map and hash-verified content.
#include "stfs/stfspackage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

constexpr qint64 kBlockSize = 0x1000;
constexpr int kBlocksPerGroup = 0xAA; // 170 data blocks per hash table
constexpr int kHashEntrySize = 24;    // 20-byte SHA-1 + 4-byte info
constexpr quint32 kChainEnd = 0xFFFFFF;

quint32 readU32BE(const QByteArray &b, int off)
{
    return (quint32(quint8(b[off])) << 24) | (quint32(quint8(b[off + 1])) << 16) |
           (quint32(quint8(b[off + 2])) << 8) | quint32(quint8(b[off + 3]));
}
quint64 readU64BE(const QByteArray &b, int off)
{
    return (quint64(readU32BE(b, off)) << 32) | readU32BE(b, off + 4);
}
quint32 readU24BE(const QByteArray &b, int off)
{
    return (quint32(quint8(b[off])) << 16) | (quint32(quint8(b[off + 1])) << 8) |
           quint32(quint8(b[off + 2]));
}
quint32 readU24LE(const QByteArray &b, int off)
{
    return quint32(quint8(b[off])) | (quint32(quint8(b[off + 1])) << 8) |
           (quint32(quint8(b[off + 2])) << 16);
}
quint16 readU16BE(const QByteArray &b, int off)
{
    return (quint16(quint8(b[off])) << 8) | quint16(quint8(b[off + 1]));
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

} // namespace

StfsPackage::StfsPackage() = default;
StfsPackage::~StfsPackage() = default;

bool StfsPackage::readAt(qint64 offset, qint64 size, QByteArray *out, QString *error)
{
    if (!file_->seek(offset))
        return fail(error, QStringLiteral("Seek to %1 failed").arg(offset));
    *out = file_->read(size);
    if (out->size() != size)
        return fail(error, QStringLiteral("Short read at offset %1").arg(offset));
    return true;
}

int StfsPackage::treeLevels() const
{
    // One level-0 table covers 170 data blocks, one level-1 table
    // covers 170 level-0 tables (28900 blocks), one level-2 table
    // covers 170 level-1 tables (4913000 blocks, ~19 GB of payload -
    // beyond any real Xbox 360 package, so three levels always do).
    if (dataBlockCount_ > quint32(kBlocksPerGroup * kBlocksPerGroup))
        return 3;
    if (dataBlockCount_ > quint32(kBlocksPerGroup))
        return 2;
    return 1;
}

qint64 StfsPackage::dataBlockOffset(quint32 block) const
{
    // Count the hash table blocks physically preceding data block N:
    //   - one level-0 table per group, its own group's included (the
    //     table sits immediately before its group): group + 1;
    //   - level-1 tables: table 0 sits before group 1, and table j
    //     (j >= 1) sits before group 170*j, so a block in span s =
    //     block / 28900 has s + 1 of them in front of it (just the
    //     first one once past group 0);
    //   - the single level-2 root table of a three-level volume,
    //     wedged immediately before level-1 table 1, i.e. in front of
    //     every data block from 28900 on.
    // The previous formula missed level-1 table 1 and the root table,
    // so from block 28900 onward every read landed two blocks early
    // and chain walks read "hash entries" out of the root table
    // (confirmed by SHA-1 scan of the 37486-block LIVE package: root
    // table at physical 29071, level-1 #1 at 29072, level-0 #170 at
    // 29073, data block 28900 at 29074).
    const quint64 group = block / kBlocksPerGroup;
    const quint64 span = block / (kBlocksPerGroup * kBlocksPerGroup);
    quint64 physical = quint64(block) + group + 1;
    if (group >= 1)
        physical += span + 1;
    if (treeLevels() == 3 &&
        block >= quint32(kBlocksPerGroup * kBlocksPerGroup))
        physical += 1;
    return dataStart_ + qint64(physical) * kBlockSize;
}

qint64 StfsPackage::hashTableOffset(int level, quint32 tableIndex) const
{
    if (level == 0) {
        // A level-0 table immediately precedes its first data block.
        return dataBlockOffset(tableIndex * kBlocksPerGroup) - kBlockSize;
    }
    if (level == 1) {
        // A level-1 table immediately precedes the first level-0
        // table of its span: table 0 before level-0 table 1, table j
        // before level-0 table 170*j.
        const quint32 firstL0 = tableIndex == 0
                                    ? 1
                                    : tableIndex * quint32(kBlocksPerGroup);
        return hashTableOffset(0, firstL0) - kBlockSize;
    }
    // Level 2: the single root table sits immediately before
    // level-1 table 1 (SHA-1-confirmed, see dataBlockOffset()).
    return hashTableOffset(1, 1) - kBlockSize;
}

bool StfsPackage::hashEntry(quint32 block, QByteArray *hash, quint32 *next,
                            QString *error)
{
    QByteArray raw;
    const qint64 off = hashTableOffset(0, block / kBlocksPerGroup) +
                       qint64(block % kBlocksPerGroup) * kHashEntrySize;
    if (!readAt(off, kHashEntrySize, &raw, error))
        return false;
    if (hash)
        *hash = raw.left(20);
    if (next)
        *next = readU32BE(raw, 20) & kChainEnd;
    return true;
}

bool StfsPackage::open(const QString &path, QString *error)
{
    file_.reset(new QFile(path));
    if (!file_->open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot open %1: %2")
                               .arg(path, file_->errorString()));

    QByteArray header;
    if (!readAt(0, 0x400, &header, error))
        return false;

    magic_ = QString::fromLatin1(header.left(4)).trimmed();
    if (magic_ != QStringLiteral("LIVE") && magic_ != QStringLiteral("PIRS") &&
        magic_ != QStringLiteral("CON"))
        return fail(error, QStringLiteral("Not an STFS package (magic \"%1\")")
                               .arg(QString::fromLatin1(header.left(4))));

    const quint32 headerSize = readU32BE(header, 0x340);
    dataStart_ = (qint64(headerSize) + kBlockSize - 1) & ~(kBlockSize - 1);
    if (dataStart_ <= 0 || dataStart_ >= file_->size())
        return fail(error, QStringLiteral("Bogus header size %1").arg(headerSize));

    titleId_ = QStringLiteral("%1").arg(readU32BE(header, 0x360), 8, 16,
                                        QLatin1Char(0x30))
                   .toUpper();

    // Volume descriptor at 0x379.
    if (quint8(header[0x379]) != 0x24)
        return fail(error, QStringLiteral("Volume descriptor size is %1, expected 0x24")
                               .arg(quint8(header[0x379])));
    fileTableBlock_ = readU24BE(header, 0x379 + 0x04);
    rootHash_ = header.mid(0x379 + 0x08, 20);
    dataBlockCount_ = readU32BE(header, 0x379 + 0x1C);
    dataBlockOffset_ = readU32BE(header, 0x379 + 0x20);
    if (dataBlockCount_ == 0)
        return fail(error, QStringLiteral("Volume declares zero data blocks"));

    // Optional integrity anchor: the descriptor's root hash is the
    // SHA-1 of the volume's top-level hash table - a level-0 table
    // for the smallest volumes, level-1 up to 28900 data blocks,
    // level-2 above that. Checked here only when verification was
    // requested.
    if (verifyHashes_) {
        QByteArray top;
        if (!readAt(hashTableOffset(treeLevels() - 1, 0), kBlockSize, &top,
                    error))
            return false;
        if (QCryptographicHash::hash(top, QCryptographicHash::Sha1) != rootHash_)
            return fail(error, QStringLiteral("Root hash mismatch - package is corrupt"));
    }

    return parseFileTable(error);
}

bool StfsPackage::parseFileTable(QString *error)
{
    rawEntries_.clear();
    quint32 block = fileTableBlock_ + dataBlockOffset_;
    // The chain length is bounded by the volume size; the guard keeps
    // a corrupt next-pointer from looping forever.
    for (quint32 steps = 0; steps <= dataBlockCount_; ++steps) {
        if (block >= dataBlockCount_)
            return fail(error, QStringLiteral("File table chain leaves the volume"));
        QByteArray raw;
        if (!readAt(dataBlockOffset(block), kBlockSize, &raw, error))
            return false;
        for (int i = 0; i < 64; ++i) {
            const int off = i * 0x40;
            RawEntry e;
            const quint8 flags = quint8(raw[off + 0x28]);
            const int nameLength = flags & 0x3F;
            if (nameLength == 0) {
                rawEntries_.push_back(e); // Empty slot; keeps indices honest
                continue;
            }
            e.name = QString::fromUtf8(raw.mid(off, nameLength));
            e.isDir = (flags & 0x80) != 0;
            e.blocksAllocated = readU24LE(raw, off + 0x29);
            e.startBlock = readU24LE(raw, off + 0x2F) + dataBlockOffset_;
            e.parent = readU16BE(raw, off + 0x32);
            e.size = readU32BE(raw, off + 0x34);
            rawEntries_.push_back(e);
        }
        quint32 next = 0;
        if (!hashEntry(block, nullptr, &next, error))
            return false;
        if (next == kChainEnd)
            return true;
        block = next;
    }
    return fail(error, QStringLiteral("File table chain never terminates"));
}

QString StfsPackage::entryPath(int index) const
{
    // Walk the parent chain; bounded by the entry count so a corrupt
    // package cannot loop.
    QStringList parts;
    int cur = index;
    for (int guard = 0; guard < rawEntries_.size() && cur >= 0 &&
                        cur < rawEntries_.size();
         ++guard) {
        const RawEntry &e = rawEntries_[cur];
        if (!e.name.isEmpty())
            parts.prepend(e.name);
        if (e.parent == 0xFFFF)
            break;
        cur = e.parent;
    }
    return parts.join(QStringLiteral("/"));
}

QVector<StfsPackage::Entry> StfsPackage::entries() const
{
    QVector<Entry> out;
    for (int i = 0; i < rawEntries_.size(); ++i) {
        const RawEntry &e = rawEntries_[i];
        if (e.name.isEmpty())
            continue;
        out.push_back({entryPath(i), e.isDir, e.size});
    }
    return out;
}

bool StfsPackage::extractAll(const QString &destDir,
                             const ProgressCallback &progress, QString *error)
{
    QDir dest(destDir);
    if (!dest.exists() && !QDir().mkpath(destDir))
        return fail(error, QStringLiteral("Cannot create %1").arg(destDir));

    const QVector<Entry> list = entries();
    qint64 done = 0;
    for (int i = 0; i < rawEntries_.size(); ++i) {
        const RawEntry &e = rawEntries_[i];
        if (e.name.isEmpty())
            continue;
        const QString path = entryPath(i);
        if (progress)
            progress(done, list.size(), path);
        if (e.isDir) {
            if (!dest.mkpath(path))
                return fail(error, QStringLiteral("Cannot create folder %1").arg(path));
            ++done;
            continue;
        }

        const QString target = dest.filePath(path);
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
            return fail(error, QStringLiteral("Cannot create folder for %1").arg(path));
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly))
            return fail(error, QStringLiteral("Cannot write %1: %2")
                                   .arg(target, out.errorString()));

        // Follow the block chain, streaming one block at a time.
        quint64 remaining = e.size;
        quint32 block = e.startBlock;
        quint32 cap = e.blocksAllocated > 0
                          ? e.blocksAllocated + 1
                          : quint32((e.size + kBlockSize - 1) / kBlockSize) + 1;
        for (quint32 steps = 0; remaining > 0; ++steps) {
            if (block == kChainEnd || block >= dataBlockCount_ || steps > cap)
                return fail(error, QStringLiteral("Block chain for %1 ends early")
                                       .arg(path));
            QByteArray raw;
            if (!readAt(dataBlockOffset(block), kBlockSize, &raw, error))
                return false;
            if (verifyHashes_) {
                QByteArray want;
                if (!hashEntry(block, &want, nullptr, error))
                    return false;
                if (QCryptographicHash::hash(raw, QCryptographicHash::Sha1) != want)
                    return fail(error, QStringLiteral("Hash mismatch in %1 (block %2)")
                                           .arg(path).arg(block));
            }
            const qint64 chunk = qMin<qint64>(remaining, kBlockSize);
            if (out.write(raw.constData(), chunk) != chunk)
                return fail(error, QStringLiteral("Write to %1 failed").arg(target));
            remaining -= quint64(chunk);
            quint32 next = 0;
            if (!hashEntry(block, nullptr, &next, error))
                return false;
            block = next;
        }
        out.close();
        ++done;
    }
    if (progress)
        progress(done, list.size(), QString());
    return true;
}
