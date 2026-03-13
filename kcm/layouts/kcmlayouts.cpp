// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmlayouts.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QTimer>
#include "../common/dbusutils.h"
#include "../common/screenhelper.h"
#include "../common/screenprovider.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <KPluginFactory>
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"
#include "../common/layoutmanager.h"

K_PLUGIN_CLASS_WITH_JSON(PlasmaZones::KCMLayouts, "kcm_plasmazones_layouts.json")

namespace PlasmaZones {

KCMLayouts::KCMLayouts(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    m_settings = new Settings(this);
    setButtons(Apply | Default);

    m_layoutManager = std::make_unique<LayoutManager>(
        m_settings,
        [this]() {
            return currentScreenName();
        },
        this);
    connect(m_layoutManager.get(), &LayoutManager::layoutsChanged, this, &KCMLayouts::layoutsChanged);
    connect(m_layoutManager.get(), &LayoutManager::layoutToSelectChanged, this, &KCMLayouts::layoutToSelectChanged);
    connect(m_layoutManager.get(), &LayoutManager::needsSave, this, [this]() {
        setNeedsSave(true);
    });

    m_screenHelper = std::make_unique<ScreenHelper>(m_settings, this);
    connect(m_screenHelper.get(), &ScreenHelper::screensChanged, this, &KCMLayouts::screensChanged);

    m_screenHelper->refreshScreens();

    // Listen for layout changes from the daemon
    m_layoutManager->connectToDaemonSignals();

    // Listen for screen changes
    m_screenHelper->connectToDaemonSignals();

    // Reload when another sub-KCM or process saves settings
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onExternalSettingsChanged()));
}

KCMLayouts::~KCMLayouts() = default;

// ── Load / Save / Defaults ───────────────────────────────────────────────

void KCMLayouts::load()
{
    KQuickConfigModule::load();
    m_settings->load();
    m_layoutManager->clearPendingStates();
    m_layoutManager->loadSync();
    m_screenHelper->refreshScreens();
    emitAllChanged();
    setNeedsSave(false);
}

void KCMLayouts::save()
{
    m_saving = true;
    m_layoutManager->setSaveInProgress(true);

    // Save settings (defaultLayoutId, autotileAlgorithm)
    m_settings->save();

    // Save pending layout states (hidden, auto-assign) via D-Bus
    QStringList failedOperations;
    m_layoutManager->savePendingStates(failedOperations);

    if (!failedOperations.isEmpty()) {
        qWarning() << "Failed operations during save:" << failedOperations;
    }

    KCMDBus::notifyReload();

    m_layoutManager->setSaveInProgress(false);

    // Reload to pick up any daemon-side changes
    m_layoutManager->loadSync();

    KQuickConfigModule::save();
    setNeedsSave(false);
    QTimer::singleShot(0, this, [this]() {
        m_saving = false;
    });
}

void KCMLayouts::onExternalSettingsChanged()
{
    if (!m_saving) {
        load();
    }
}

void KCMLayouts::defaults()
{
    KQuickConfigModule::defaults();

    // Pick the layout with the lowest defaultOrder as default
    int bestOrder = 999;
    QString bestId;
    for (const QVariant& layoutVar : m_layoutManager->layouts()) {
        const QVariantMap layout = layoutVar.toMap();
        int order = layout.value(QStringLiteral("defaultOrder"), 999).toInt();
        if (order < bestOrder) {
            bestOrder = order;
            bestId = layout.value(QStringLiteral("id")).toString();
        }
    }
    if (!bestId.isEmpty()) {
        setDefaultLayoutId(bestId);
    }

    // Reset all layout hidden/auto-assign states
    m_layoutManager->resetAllToDefaults();
    setNeedsSave(true);
}

// ── Layout list ──────────────────────────────────────────────────────────

QVariantList KCMLayouts::layouts() const
{
    return m_layoutManager->layouts();
}

QString KCMLayouts::layoutToSelect() const
{
    return m_layoutManager->layoutToSelect();
}

// ── Settings getters ─────────────────────────────────────────────────────

QString KCMLayouts::defaultLayoutId() const
{
    return m_settings->defaultLayoutId();
}

bool KCMLayouts::autotileEnabled() const
{
    return m_settings->autotileEnabled();
}

QString KCMLayouts::autotileAlgorithm() const
{
    return m_settings->autotileAlgorithm();
}

void KCMLayouts::setAutotileAlgorithm(const QString& algorithm)
{
    if (m_settings->autotileAlgorithm() != algorithm) {
        m_settings->setAutotileAlgorithm(algorithm);
        Q_EMIT autotileAlgorithmChanged();
        setNeedsSave(true);
    }
}

// Font properties (read-only — edited in Snapping KCM)
QString KCMLayouts::labelFontFamily() const
{
    return m_settings->labelFontFamily();
}

qreal KCMLayouts::labelFontSizeScale() const
{
    return m_settings->labelFontSizeScale();
}

int KCMLayouts::labelFontWeight() const
{
    return m_settings->labelFontWeight();
}

bool KCMLayouts::labelFontItalic() const
{
    return m_settings->labelFontItalic();
}

bool KCMLayouts::labelFontUnderline() const
{
    return m_settings->labelFontUnderline();
}

bool KCMLayouts::labelFontStrikeout() const
{
    return m_settings->labelFontStrikeout();
}

// ── Settings setters ─────────────────────────────────────────────────────

void KCMLayouts::setDefaultLayoutId(const QString& layoutId)
{
    if (m_settings->defaultLayoutId() != layoutId) {
        m_settings->setDefaultLayoutId(layoutId);
        Q_EMIT defaultLayoutIdChanged();
        setNeedsSave(true);
    }
}

// ── Layout management (delegates to LayoutManager) ──────────────────────

void KCMLayouts::createNewLayout()
{
    m_layoutManager->createNewLayout();
}

void KCMLayouts::deleteLayout(const QString& layoutId)
{
    m_layoutManager->deleteLayout(layoutId);
}

void KCMLayouts::duplicateLayout(const QString& layoutId)
{
    m_layoutManager->duplicateLayout(layoutId);
}

void KCMLayouts::importLayout(const QString& filePath)
{
    m_layoutManager->importLayout(filePath);
}

void KCMLayouts::exportLayout(const QString& layoutId, const QString& filePath)
{
    m_layoutManager->exportLayout(layoutId, filePath);
}

void KCMLayouts::editLayout(const QString& layoutId)
{
    m_layoutManager->editLayout(layoutId);
}

void KCMLayouts::openEditor()
{
    m_layoutManager->openEditor();
}

void KCMLayouts::setLayoutHidden(const QString& layoutId, bool hidden)
{
    m_layoutManager->setLayoutHidden(layoutId, hidden);
}

void KCMLayouts::setLayoutAutoAssign(const QString& layoutId, bool enabled)
{
    m_layoutManager->setLayoutAutoAssign(layoutId, enabled);
}

// ── Screens ──────────────────────────────────────────────────────────────

QVariantList KCMLayouts::screens() const
{
    return m_screenHelper->screens();
}

QString KCMLayouts::currentScreenName() const
{
    // Return first screen name for editor targeting
    const auto s = m_screenHelper->screens();
    if (!s.isEmpty()) {
        return s.first().toMap().value(QStringLiteral("name")).toString();
    }
    return QString();
}

void KCMLayouts::refreshScreens()
{
    m_screenHelper->refreshScreens();
}

// ── Helpers ──────────────────────────────────────────────────────────────

void KCMLayouts::emitAllChanged()
{
    Q_EMIT layoutsChanged();
    Q_EMIT layoutToSelectChanged();
    Q_EMIT defaultLayoutIdChanged();
    Q_EMIT autotileEnabledChanged();
    Q_EMIT autotileAlgorithmChanged();
    Q_EMIT labelFontFamilyChanged();
    Q_EMIT labelFontSizeScaleChanged();
    Q_EMIT labelFontWeightChanged();
    Q_EMIT labelFontItalicChanged();
    Q_EMIT labelFontUnderlineChanged();
    Q_EMIT labelFontStrikeoutChanged();
    Q_EMIT screensChanged();
}

} // namespace PlasmaZones

#include "kcmlayouts.moc"
