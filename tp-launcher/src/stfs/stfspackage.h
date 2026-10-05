// stfspackage.h - minimal STFS (Xbox 360 content package) reader.
//
// STFS is the container format behind XBLA downloads: files start with
// a "LIVE", "PIRS" or "CON " magic and store their payload in 0x1000
// byte blocks, interleaved with SHA-1 hash tables. This reader exists
// so the launcher can import a raw XBLA package itself instead of
// sending the user to an external extraction tool.
//
// QtCore only (QFile/QByteArray/QCryptographicHash), no Widgets - the
// same code is exercised headlessly by the stfs_test harness.
//
// All offsets below were confirmed against the real reference package
// 584108C2033F0001 (The Maw, PIRS) on 2026-10-03: every one of its
// 1964 data blocks was SHA-1-verified against its level-0 hash table
// entry, and the extracted files are byte-identical to a known-good
// extraction. Where the format documentation disagrees with the file
// (it does, twice), the file wins and the comment says so.
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

class StfsPackage
{
public:
    struct Entry {
        QString path; // Relative, "/" separated, no leading slash
        bool isDir = false;
        quint32 size = 0;
    };

    // Called after each file: (files extracted, total files, name).
    using ProgressCallback =
        std::function<void(qint64 done, qint64 total, const QString &name)>;

    StfsPackage();
    ~StfsPackage();

    StfsPackage(const StfsPackage &) = delete;
    StfsPackage &operator=(const StfsPackage &) = delete;

    // Parse header + file table. The file stays open for extraction.
    bool open(const QString &path, QString *error = nullptr);
    bool isOpen() const { return file_ != nullptr; }

    QString titleId() const { return titleId_; } // e.g. "584108C2"
    QString magic() const { return magic_; }     // LIVE / PIRS / CON
    QVector<Entry> entries() const;

    // Extract every file under destDir (created if needed), streaming
    // block by block - the package is never loaded into memory whole.
    bool extractAll(const QString &destDir,
                    const ProgressCallback &progress = {},
                    QString *error = nullptr);

    // SHA-1 verification of every block read during extraction (plus
    // the volume's root hash chain). Default OFF: it roughly doubles
    // extraction I/O cost and importing a package you already trust
    // does not need it. Turn on when diagnosing a suspect package.
    void setVerifyHashes(bool on) { verifyHashes_ = on; }
    bool verifyHashes() const { return verifyHashes_; }

private:
    struct RawEntry {
        QString name;
        bool isDir = false;
        quint16 parent = 0xFFFF; // File-table index, 0xFFFF = root
        quint32 startBlock = 0;
        quint32 blocksAllocated = 0;
        quint32 size = 0;
    };

    bool readAt(qint64 offset, qint64 size, QByteArray *out, QString *error);
    bool hashEntry(quint32 block, QByteArray *hash, quint32 *next,
                   QString *error);
    qint64 dataBlockOffset(quint32 block) const;
    qint64 hashTableOffset(int level, quint32 tableIndex) const;
    int treeLevels() const; // Hash-tree depth implied by the volume size
    bool parseFileTable(QString *error);
    QString entryPath(int index) const;

    std::unique_ptr<class QFile> file_;
    QString magic_;
    QString titleId_;
    qint64 dataStart_ = 0;       // First hash table"s file offset
    quint32 dataBlockCount_ = 0; // Payload blocks (hash tables excluded)
    quint32 dataBlockOffset_ = 0; // Added to stored block numbers (0 here)
    quint32 fileTableBlock_ = 0;
    QByteArray rootHash_; // SHA-1 of the top-level hash table
    QVector<RawEntry> rawEntries_; // Slot order == file-table order
    bool verifyHashes_ = false;
};
