// stfs_test_main.cpp - headless harness for the STFS reader.
// Usage: stfs_test [--verify] <package> <destDir>
// Lists the package contents, extracts them, and reports per-file
// sizes. Exit code 0 on success, 1 on any failure.
#include "stfs/stfspackage.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QStringList args = app.arguments().mid(1);

    bool verify = false;
    if (args.contains(QStringLiteral("--verify"))) {
        verify = true;
        args.removeAll(QStringLiteral("--verify"));
    }
    if (args.size() != 2) {
        out << "usage: stfs_test [--verify] <package> <destDir>\n";
        return 1;
    }

    StfsPackage pkg;
    pkg.setVerifyHashes(verify);
    QString error;
    if (!pkg.open(args[0], &error)) {
        out << "open failed: " << error << "\n";
        return 1;
    }
    out << "magic=" << pkg.magic() << " titleId=" << pkg.titleId()
        << " verify=" << (verify ? "on" : "off") << "\n";

    const QVector<StfsPackage::Entry> list = pkg.entries();
    quint64 totalBytes = 0;
    for (const StfsPackage::Entry &e : list) {
        out << (e.isDir ? "dir  " : "file ") << e.size << " " << e.path << "\n";
        totalBytes += e.size;
    }
    out << "entries=" << list.size() << " bytes=" << totalBytes << "\n";

    if (!pkg.extractAll(args[1], {}, &error)) {
        out << "extract failed: " << error << "\n";
        return 1;
    }
    out << "extracted OK\n";
    return 0;
}
