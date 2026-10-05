// dlcimport.h - install downloadable-content STFS packages into a
// user data root, in the layout the runtime's content system reads:
//
//   <root>/0000000000000000/<TITLEID>/<CTYPE>/<PackageName>/<files...>
//   <root>/0000000000000000/<TITLEID>/Headers/<CTYPE>/<PackageName>.header
//
// The .header record is the XCONTENT_AGGREGATE_DATA struct the SDK's
// ContentManager::WriteContentHeaderFile writes when content is
// installed, plus a host-order license-mask trailer aggregated from
// the package's own license table (without it, games treat the
// content as not purchased). This replicates, field for field, the
// tooling that produced the verified Real Steel / Pacific Rim DLC
// installs of 2026-10-05 - see dlc/gen_headers.py in those projects.
//
// QtCore only (like stfspackage) so the dlc_test harness can drive it
// headlessly.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace dlcimport {

struct PackageInfo {
    QString fileName;    // Basename; becomes the content name on disk
    QString displayName; // From the STFS header (fallback: fileName)
    QString titleId;     // 8 hex digits, uppercase
    quint32 contentType = 0; // STFS content type (2 = marketplace/DLC)
    quint32 licenseMask = 0; // Aggregated from the header license table
};

// Read the metadata the installer needs out of a package's header.
// Returns false (with *error set) when the file is not an STFS
// package at all.
bool probePackage(const QString &path, PackageInfo *info,
                  QString *error = nullptr);

struct Outcome {
    int installed = 0;
    QStringList names;   // Display names of what was installed
    QStringList skipped; // "file: reason" for packages not installed
};

// Called when work on a package starts (display name or file name).
using ProgressCallback = std::function<void(const QString &name)>;

// Import DLC from wherever the user pointed:
//   - a single STFS package file,
//   - a .rar/.zip/.7z archive (unpacked with the bundled 7-Zip; every
//     package inside whose title id matches is installed), or
//   - a folder, searched recursively for matching packages.
// Packages for a different title are skipped and reported, never
// installed into this game's tree. Returns true when at least one
// package was installed.
bool importDlc(const QString &pickedPath, const QString &userDataRoot,
               const QString &wantTitleId,
               const ProgressCallback &progress, Outcome *outcome,
               QString *error = nullptr);

} // namespace dlcimport
