#pragma once
#include <QObject>

#include "control/controlobject.h"
#include "control/controlpushbutton.h"

/// Skin controls that can be use in controller mappings.
class SkinControls : public QObject {
    Q_OBJECT
  public:
    /// Creates the standard skin ControlObjects.
    SkinControls();

  signals:
    /// Emitted when a skin or controller requests Mixxx to quit gracefully.
    void quitRequested();
    /// Emitted when a skin or controller requests the preferences dialog.
    void showPreferencesRequested();
    /// Emitted when a skin or controller requests toggling fullscreen mode.
    void toggleFullscreenRequested();

  private slots:
    void slotQuitRequest(double value);
    void slotShowPreferencesRequest(double value);
    void slotToggleFullscreenRequest(double value);

  private:
    void setActionRequested(ControlPushButton* pControl, double value);

    ControlPushButton m_quit;
    ControlObject m_quitAvailable;
    ControlPushButton m_showPreferences;
    ControlObject m_showPreferencesAvailable;
    ControlPushButton m_toggleFullscreen;
    ControlObject m_toggleFullscreenAvailable;
    ControlPushButton m_showEffectRack;
    ControlPushButton m_showLibraryCoverArt;
    ControlPushButton m_showMicrophones;
    ControlPushButton m_showPreviewDecks;
    ControlPushButton m_showSamplers;
    ControlPushButton m_show4EffectUnits;
    ControlPushButton m_showCoverArt;
    ControlPushButton m_showMaximizedLibrary;
    ControlPushButton m_showMixer;
    ControlPushButton m_showSettings;
    ControlPushButton m_showSpinnies;
    ControlPushButton m_showVinylControl;
};
