#include "qmlapplication.h"

#include <QMessageBox>
#include <QQmlEngineExtensionPlugin>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextDocument>

#include "control/controlobject.h"
#include "controllers/controllermanager.h"
#include "mixer/playermanager.h"
#include "moc_qmlapplication.cpp"
#include "preferences/configobject.h"
#include "qml/asyncimageprovider.h"
#include "qml/qmldlgpreferencesproxy.h"
#include "skin/skincontrols.h"
#include "soundio/soundmanager.h"
#include "util/versionstore.h"
#include "waveform/visualsmanager.h"
#include "waveform/waveformwidgetfactory.h"
#if defined(Q_OS_ANDROID)
#include <android/api-level.h>
#include <android/log.h>
#include <android/performance_hint.h>
#endif

Q_IMPORT_QML_PLUGIN(MixxxPlugin)
Q_IMPORT_QML_PLUGIN(Mixxx_ControlsPlugin)

namespace {
const QString kMainQmlFileName = QStringLiteral("qml/main.qml");

// Converts a (capturing) lambda into a function pointer that can be passed to
// qmlRegisterSingletonType.
template<class F>
auto lambda_to_singleton_type_factory_ptr(F&& f) {
    static F fn = std::forward<F>(f);
    return [](QQmlEngine* pEngine, QJSEngine* pScriptEngine) -> QObject* {
        return fn(pEngine, pScriptEngine);
    };
}
} // namespace

namespace mixxx {
namespace qml {

QmlApplication::QmlApplication(
        QApplication* app,
        const CmdlineArgs& args)
        : m_pCoreServices(std::make_unique<mixxx::CoreServices>(args, app)),
          m_visualsManager(std::make_unique<VisualsManager>()),
          m_mainFilePath(m_pCoreServices->getSettings()->getResourcePath() + kMainQmlFileName),
          m_pAppEngine(nullptr),
#if defined(Q_OS_ANDROID)
          m_perfSession(nullptr),
#endif
          m_autoReload() {
    QQuickStyle::setStyle("Basic");

    m_pCoreServices->initialize(app);

    QString configVersion = m_pCoreServices->getSettings()->getValue(
            ConfigKey("[Config]", "Version"), "");
    if (configVersion == VersionStore::FUTURE_UNSTABLE) {
        qDebug() << "Generating a new user profile for safe testing with unstable code";
    } else if (CmdlineArgs::Instance().isAwareOfRisk()) {
        qCritical() << "Existing user profile detected from" << configVersion
                    << "but you said you wanted to play with fire!";
        m_pCoreServices->getSettings()->setValue(
                ConfigKey("[Config]", "did_run_with_unstable"), true);
    } else {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setWindowTitle(tr("Existing user profile detected"));
        msgBox.setText(
                tr("Trying to run Mixxx 3.0 with an existing %0 user profile! "
                   "<br><br>There is <b>serious risks</b> of data loss and "
                   "corruption.<br>We recommend using a test profile folder "
                   "with the '--settings-path' argument. <br><br>If you want "
                   "to continue at your own risk, run Mixxx with the argument "
                   "'--allow-dangerous-data-corruption-risk'.")
                        .arg(configVersion));

        QPushButton* continueButton =
                msgBox.addButton(tr("Ok"), QMessageBox::ActionRole);
        msgBox.exec();
        m_pCoreServices.reset();
        exit(-1);
    }

    SoundDeviceStatus result = m_pCoreServices->getSoundManager()->setupDevices();
    if (result != SoundDeviceStatus::Ok) {
        const int reInt = static_cast<int>(result);
        qCritical() << "Error setting up sound devices:" << reInt;
#ifndef Q_OS_ANDROID
        exit(reInt);
#endif
    }

    // FIXME: DlgPreferences has some initialization logic that must be executed
    // before the GUI is shown, at least for the effects system.
    std::shared_ptr<QDialog> pDlgPreferences = m_pCoreServices->makeDlgPreferences();
    // Without this, QApplication will quit when the last QWidget QWindow is
    // closed because it does not take into account the window created by
    // the QQmlApplicationEngine.
    pDlgPreferences->setAttribute(Qt::WA_QuitOnClose, false);

    // Since DlgPreferences is only meant to be used in the main QML engine, it
    // follows a strict singleton pattern design
    QmlDlgPreferencesProxy::s_pInstance =
            std::make_unique<QmlDlgPreferencesProxy>(pDlgPreferences, this);

    SkinControls* pSkinControls = m_pCoreServices->getSkinControls();
    VERIFY_OR_DEBUG_ASSERT(pSkinControls) {
        return;
    }
    connect(pSkinControls,
            &SkinControls::quitRequested,
            this,
            &QmlApplication::slotSkinQuitRequested,
            Qt::UniqueConnection);
    connect(pSkinControls,
            &SkinControls::showPreferencesRequested,
            this,
            &QmlApplication::slotSkinShowPreferencesRequested,
            Qt::UniqueConnection);
    connect(pSkinControls,
            &SkinControls::toggleFullscreenRequested,
            this,
            &QmlApplication::slotSkinToggleFullscreenRequested,
            Qt::UniqueConnection);

    const QStringList visualGroups =
            m_pCoreServices->getPlayerManager()->getVisualPlayerGroups();
    for (const QString& group : visualGroups) {
        m_visualsManager->addDeck(group);
    }

    m_pCoreServices->getPlayerManager()->connect(
            m_pCoreServices->getPlayerManager().get(),
            &PlayerManager::numberOfDecksChanged,
            this,
            [this](int decks) {
                for (int i = 0; i < decks; ++i) {
                    QString group = PlayerManager::groupForDeck(i);
                    m_visualsManager->addDeckIfNotExist(group);
                }
            });
    loadQml(m_mainFilePath);

    m_pCoreServices->getControllerManager()->setUpDevices();

    connect(&m_autoReload,
            &QmlAutoReload::triggered,
            this,
            [this]() {
                loadQml(m_mainFilePath);
            });

#if defined(Q_OS_ANDROID)
    APerformanceHintManager* manager = APerformanceHint_getManager();
    VERIFY_OR_DEBUG_ASSERT(manager) {
        return;
    }
    int32_t thread32 = gettid();
    m_perfSession = APerformanceHint_createSession(manager, &thread32, 1, 1e9 / 60);
    VERIFY_OR_DEBUG_ASSERT(m_perfSession) {
        __android_log_print(ANDROID_LOG_WARN, "mixxx", "unable to create a ADPF session!");
    }
    else {
        APerformanceHint_setPreferPowerEfficiency(m_perfSession, false);
        __android_log_print(ANDROID_LOG_VERBOSE, "mixxx", "ADPF session ready");
    }
}

void QmlApplication::slotWindowChanged(QQuickWindow* window) {
    if (window) {
        connect(window, &QQuickWindow::afterFrameEnd, this, &QmlApplication::slotFrameSwapped);
    }
    m_frameTimer.restart();
}

void QmlApplication::slotFrameSwapped() {
    VERIFY_OR_DEBUG_ASSERT(m_perfSession) {
        return;
    }
    auto lastFrameDurationNs = m_frameTimer.elapsed().toIntegerNanos();
    auto t = std::chrono::steady_clock::now() - std::chrono::steady_clock::time_point{};
    APerformanceHint_reportActualWorkDuration(m_perfSession,
            lastFrameDurationNs);
    m_frameTimer.restart();
#endif
}

QQuickWindow* QmlApplication::rootWindow() const {
    if (!m_pAppEngine) {
        return nullptr;
    }

    const QList<QObject*> rootObjects = m_pAppEngine->rootObjects();
    for (QObject* pObject : rootObjects) {
        auto* pWindow = qobject_cast<QQuickWindow*>(pObject);
        if (pWindow) {
            return pWindow;
        }
    }
    return nullptr;
}

bool QmlApplication::confirmExit() {
    bool playing = false;
    bool playingSampler = false;
    auto pPlayerManager = m_pCoreServices->getPlayerManager();
    int deckCount = pPlayerManager->numberOfDecks();
    int samplerCount = pPlayerManager->numberOfSamplers();
    for (int i = 0; i < deckCount; ++i) {
        if (ControlObject::toBool(
                    ConfigKey(PlayerManager::groupForDeck(i), "play"))) {
            playing = true;
            break;
        }
    }
    for (int i = 0; i < samplerCount; ++i) {
        if (ControlObject::toBool(
                    ConfigKey(PlayerManager::groupForSampler(i), "play"))) {
            playingSampler = true;
            break;
        }
    }

    if (playing) {
        QMessageBox::StandardButton btn = QMessageBox::question(
                nullptr,
                tr("Confirm Exit"),
                tr("A deck is currently playing. Exit Mixxx?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
        if (btn == QMessageBox::No) {
            return false;
        }
    } else if (playingSampler) {
        QMessageBox::StandardButton btn = QMessageBox::question(
                nullptr,
                tr("Confirm Exit"),
                tr("A sampler is currently playing. Exit Mixxx?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
        if (btn == QMessageBox::No) {
            return false;
        }
    }

    if (QmlDlgPreferencesProxy::s_pInstance &&
            QmlDlgPreferencesProxy::s_pInstance->isVisible()) {
        QMessageBox::StandardButton btn = QMessageBox::question(
                nullptr,
                tr("Confirm Exit"),
                tr("The preferences window is still open.") + "<br>" +
                        tr("Discard any changes and exit Mixxx?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
        if (btn == QMessageBox::No) {
            return false;
        }
        QmlDlgPreferencesProxy::s_pInstance->close();
    }

    return true;
}

void QmlApplication::slotSkinQuitRequested() {
    if (!confirmExit()) {
        return;
    }

    QQuickWindow* pWindow = rootWindow();
    if (pWindow) {
        pWindow->close();
        return;
    }
    qApp->quit();
}

void QmlApplication::slotSkinShowPreferencesRequested() {
    if (QmlDlgPreferencesProxy::s_pInstance) {
        QmlDlgPreferencesProxy::s_pInstance->show();
    }
}

void QmlApplication::slotSkinToggleFullscreenRequested() {
    QQuickWindow* pWindow = rootWindow();
    if (!pWindow) {
        return;
    }

    if (pWindow->visibility() == QWindow::FullScreen) {
        pWindow->showNormal();
    } else {
        pWindow->showFullScreen();
    }
}

QmlApplication::~QmlApplication() {
    // Delete all the QML singletons in order to prevent leak detection in CoreService
    QmlDlgPreferencesProxy::s_pInstance.reset();
    m_visualsManager.reset();
    m_pAppEngine.reset();
    m_pCoreServices.reset();
}

void QmlApplication::loadQml(const QString& path) {
    // QQmlApplicationEngine::load creates a new window but also leaves the old one,
    // so it is necessary to destroy the old QQmlApplicationEngine and create a new one.
    m_pAppEngine = std::make_unique<QQmlApplicationEngine>();

    m_autoReload.clear();
    m_pAppEngine->addUrlInterceptor(&m_autoReload);
    m_pAppEngine->addImportPath(QStringLiteral(":/mixxx.org/imports"));

    // No memory leak here, the QQmlEngine takes ownership of the provider
    QQuickAsyncImageProvider* pImageProvider = new AsyncImageProvider(
            m_pCoreServices->getTrackCollectionManager());
    m_pAppEngine->addImageProvider(AsyncImageProvider::kProviderName, pImageProvider);

    m_pAppEngine->load(path);
    if (m_pAppEngine->rootObjects().isEmpty()) {
        qCritical() << "Failed to load QML file" << path;
    }

#if defined(Q_OS_ANDROID)
    for (auto* item : m_pAppEngine->rootObjects()) {
        auto* pWindow = qobject_cast<QQuickWindow*>(item);
        if (!pWindow) {
            continue;
        }
        slotWindowChanged(pWindow);
        break;
    }
#endif
}

} // namespace qml
} // namespace mixxx
