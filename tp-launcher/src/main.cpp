// main.cpp - house launcher entry point.
//
// One codebase, per-game builds: the TP_GAME CMake option picks the
// active profile (see gameprofile.h), and everything user-visible -
// product name, settings file, theme, art - follows from it.
#include "gameprofile.h"
#include "mainwindow.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QIcon>
#include <QSettings>

namespace {
// Load the profile"s theme stylesheet from resources and apply it.
// The .qss holds @ACCENT@/@ACCENT_HOVER@ placeholders, filled from the
// game profile so a per-game theme is just another .qss file.
void applyTheme(QApplication &app, const GameProfile &profile)
{
    QFile qss(profile.themeFile);
    if (!qss.open(QIODevice::ReadOnly | QIODevice::Text))
        return; // Unthemed is survivable; a missing resource is a build bug.
    QString style = QString::fromUtf8(qss.readAll());

    const QColor accent(profile.accent);
    if (accent.isValid()) {
        style.replace(QStringLiteral("@ACCENT_HOVER@"),
                      accent.lighter(118).name());
        style.replace(QStringLiteral("@ACCENT@"), accent.name());
    }
    app.setStyleSheet(style);
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const GameProfile &profile = activeProfile();
    QCoreApplication::setOrganizationName(QStringLiteral("HouseRecomp"));
    QCoreApplication::setApplicationName(profile.launcherName);
    // All settings go through QSettings IniFormat (see gamesettings.cpp);
    // make it the default too so any future QSettings use matches.
    QSettings::setDefaultFormat(QSettings::IniFormat);

    app.setWindowIcon(QIcon(QStringLiteral(":/assets/icon.png")));
    applyTheme(app, profile);

    MainWindow window;
    window.show();
    return app.exec();
}
