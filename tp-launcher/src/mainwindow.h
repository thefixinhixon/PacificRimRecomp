// mainwindow.h - UI: Game tab (status + PLAY + import) and a
// scrollable Settings tab with the full option surface.
#pragma once

#include "gameprofile.h"
#include "gamesettings.h"

#include <QMainWindow>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onPlayClicked();
    void onImportClicked();
    void onDlcImportClicked();
    void onSettingsChanged();
    void onResetDefaultsClicked();
    void onBrowseUserDataRoot();
    void onBrowseLogDir();
    void onOpenDataFolder();

private:
    void buildGameTab();
    void buildSettingsTab();
    void loadSettingsIntoUi();
    GameSettings collectSettings() const;
    void refreshGameStatus();
    void refreshBanner();
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void updateOpenDataButton();

    GameProfile profile_;   // The compiled-in profile (TP_GAME)
    GameSettings settings_; // Last loaded/saved settings
    QString gameRoot_;      // Resolved at startup (may be empty)
    QString dataRoot_;      // Logs anchor; defaults to the game root

    QLabel *bannerLabel_ = nullptr;
    QPixmap bannerPixmap_; // Source art; refreshBanner() fits it whole
    QSize bannerSize_;     // Last cropped size (guards redundant sets)
    QLabel *statusLabel_ = nullptr;
    QLabel *rootLabel_ = nullptr;
    QLabel *commandLabel_ = nullptr;
    QPushButton *playButton_ = nullptr;
    QPushButton *importButton_ = nullptr;
    QPushButton *dlcImportButton_ = nullptr;

    // Graphics
    QComboBox *resolutionCombo_ = nullptr;
    QComboBox *upscalingCombo_ = nullptr;
    QComboBox *postEffectCombo_ = nullptr;
    QSpinBox *anisotropicSpin_ = nullptr;
    QCheckBox *nativeMsaaCheck_ = nullptr;
    QCheckBox *asyncShadersCheck_ = nullptr;
    QSpinBox *pipelineThreadsSpin_ = nullptr;
    QCheckBox *vsyncCheck_ = nullptr;
    QSpinBox *frameLimitSpin_ = nullptr;
    QCheckBox *windowedCheck_ = nullptr;
    // Audio
    QDoubleSpinBox *audioGainSpin_ = nullptr;
    QSpinBox *audioHighpassSpin_ = nullptr;
    QCheckBox *forceStereoCheck_ = nullptr;
    QCheckBox *frontOnlyCheck_ = nullptr;
    QSpinBox *audioMaxQFramesSpin_ = nullptr;
    QCheckBox *xmaWorkerCheck_ = nullptr;
    // Storage & logs
    QLineEdit *userDataRootEdit_ = nullptr;
    QPushButton *openDataButton_ = nullptr;
    QLineEdit *logDirEdit_ = nullptr;
    QComboBox *logLevelCombo_ = nullptr;
    // Advanced
    QCheckBox *sparseMemoryCheck_ = nullptr;
    QCheckBox *asyncSkipCheck_ = nullptr;
    QSpinBox *userLanguageSpin_ = nullptr;
    QSpinBox *userCountrySpin_ = nullptr;

    QPushButton *resetButton_ = nullptr;
};
