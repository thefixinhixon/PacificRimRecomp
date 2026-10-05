// dlc_test_main.cpp - headless harness for the DLC importer.
// Usage: dlc_test <package|archive|folder> <userDataRoot> <titleId>
// Probes and installs, printing one line per package. Exit code 0
// when at least one package was installed, 1 otherwise.
#include "dlcimport.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QStringList args = app.arguments().mid(1);
    if (args.size() != 3) {
        out << "usage: dlc_test <package|archive|folder> <userDataRoot> "
               "<titleId>\n";
        return 1;
    }

    dlcimport::Outcome outcome;
    QString error;
    const bool ok = dlcimport::importDlc(
        args[0], args[1], args[2],
        [&out](const QString &name) { out << "installing: " << name << "\n"; },
        &outcome, &error);
    for (const QString &name : outcome.names)
        out << "installed: " << name << "\n";
    for (const QString &skip : outcome.skipped)
        out << "skipped: " << skip << "\n";
    out << "total installed: " << outcome.installed << "\n";
    if (!ok) {
        out << "failed: " << error << "\n";
        return 1;
    }
    return 0;
}
