// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmautotiling.h"
#include <QDBusConnection>
#include <QTimer>
#include "../common/dbusutils.h"
#include "../common/perscreenhelpers.h"
#include "../common/screenhelper.h"
#include "../common/screenprovider.h"
#include <KPluginFactory>
#include "../../src/config/configdefaults.h"
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"
#include "../../src/core/interfaces.h"
#include "../../src/autotile/AlgorithmRegistry.h"
#include "../../src/autotile/TilingAlgorithm.h"
#include "../../src/autotile/TilingState.h"

K_PLUGIN_CLASS_WITH_JSON(PlasmaZones::KCMAutotiling, "kcm_plasmazones_autotiling.json")

namespace PlasmaZones {

KCMAutotiling::KCMAutotiling(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    m_settings = new Settings(this);
    setButtons(Apply | Default);

    m_screenHelper = std::make_unique<ScreenHelper>(m_settings, this);
    connect(m_screenHelper.get(), &ScreenHelper::screensChanged, this, &KCMAutotiling::screensChanged);
    connect(m_screenHelper.get(), &ScreenHelper::needsSave, this, [this]() {
        setNeedsSave(true);
    });

    m_screenHelper->refreshScreens();

    // Reload when another process or sub-KCM saves settings
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onExternalSettingsChanged()));

    // Listen for screen changes from the daemon
    m_screenHelper->connectToDaemonSignals();
}

KCMAutotiling::~KCMAutotiling() = default;

// ── Load / Save ─────────────────────────────────────────────────────────

void KCMAutotiling::load()
{
    KQuickConfigModule::load();
    m_settings->load();
    m_screenHelper->refreshScreens();
    emitAllChanged();
    setNeedsSave(false);
}

void KCMAutotiling::save()
{
    m_saving = true;
    m_settings->save();

    KCMDBus::notifyReload();

    KQuickConfigModule::save();
    setNeedsSave(false);
    QTimer::singleShot(0, this, [this]() {
        m_saving = false;
    });
}

void KCMAutotiling::onExternalSettingsChanged()
{
    if (!m_saving) {
        load();
    }
}

void KCMAutotiling::defaults()
{
    KQuickConfigModule::defaults();

    setAutotileEnabled(ConfigDefaults::autotileEnabled());
    setAutotileAlgorithm(ConfigDefaults::autotileAlgorithm());
    setAutotileSplitRatio(ConfigDefaults::autotileSplitRatio());
    setAutotileMasterCount(ConfigDefaults::autotileMasterCount());
    setAutotileCenteredMasterSplitRatio(ConfigDefaults::autotileCenteredMasterSplitRatio());
    setAutotileCenteredMasterMasterCount(ConfigDefaults::autotileCenteredMasterMasterCount());
    setAutotileInnerGap(ConfigDefaults::autotileInnerGap());
    setAutotileOuterGap(ConfigDefaults::autotileOuterGap());
    setAutotileUsePerSideOuterGap(ConfigDefaults::autotileUsePerSideOuterGap());
    setAutotileOuterGapTop(ConfigDefaults::autotileOuterGapTop());
    setAutotileOuterGapBottom(ConfigDefaults::autotileOuterGapBottom());
    setAutotileOuterGapLeft(ConfigDefaults::autotileOuterGapLeft());
    setAutotileOuterGapRight(ConfigDefaults::autotileOuterGapRight());
    setAutotileFocusNewWindows(ConfigDefaults::autotileFocusNewWindows());
    setAutotileSmartGaps(ConfigDefaults::autotileSmartGaps());
    setAutotileMaxWindows(ConfigDefaults::autotileMaxWindows());
    setAutotileInsertPosition(ConfigDefaults::autotileInsertPosition());
    setAutotileFocusFollowsMouse(ConfigDefaults::autotileFocusFollowsMouse());
    setAutotileRespectMinimumSize(ConfigDefaults::autotileRespectMinimumSize());
    setAutotileHideTitleBars(ConfigDefaults::autotileHideTitleBars());
    setAutotileShowBorder(ConfigDefaults::autotileShowBorder());
    setAutotileBorderWidth(ConfigDefaults::autotileBorderWidth());
    setAutotileBorderRadius(ConfigDefaults::autotileBorderRadius());
    setAutotileBorderColor(ConfigDefaults::autotileBorderColor());
    setAutotileInactiveBorderColor(ConfigDefaults::autotileInactiveBorderColor());
    setAutotileUseSystemBorderColors(ConfigDefaults::autotileUseSystemBorderColors());
}

void KCMAutotiling::emitAllChanged()
{
    Q_EMIT autotileEnabledChanged();
    Q_EMIT autotileAlgorithmChanged();
    Q_EMIT autotileSplitRatioChanged();
    Q_EMIT autotileMasterCountChanged();
    Q_EMIT autotileCenteredMasterSplitRatioChanged();
    Q_EMIT autotileCenteredMasterMasterCountChanged();
    Q_EMIT autotileInnerGapChanged();
    Q_EMIT autotileOuterGapChanged();
    Q_EMIT autotileSmartGapsChanged();
    Q_EMIT autotileUsePerSideOuterGapChanged();
    Q_EMIT autotileOuterGapTopChanged();
    Q_EMIT autotileOuterGapBottomChanged();
    Q_EMIT autotileOuterGapLeftChanged();
    Q_EMIT autotileOuterGapRightChanged();
    Q_EMIT autotileFocusNewWindowsChanged();
    Q_EMIT autotileMaxWindowsChanged();
    Q_EMIT autotileInsertPositionChanged();
    Q_EMIT autotileFocusFollowsMouseChanged();
    Q_EMIT autotileRespectMinimumSizeChanged();
    Q_EMIT autotileHideTitleBarsChanged();
    Q_EMIT autotileShowBorderChanged();
    Q_EMIT autotileBorderWidthChanged();
    Q_EMIT autotileBorderRadiusChanged();
    Q_EMIT autotileBorderColorChanged();
    Q_EMIT autotileInactiveBorderColorChanged();
    Q_EMIT autotileUseSystemBorderColorsChanged();
    Q_EMIT screensChanged();
}

// ── Enable ──────────────────────────────────────────────────────────────

bool KCMAutotiling::autotileEnabled() const
{
    return m_settings->autotileEnabled();
}

void KCMAutotiling::setAutotileEnabled(bool enabled)
{
    if (m_settings->autotileEnabled() != enabled) {
        m_settings->setAutotileEnabled(enabled);
        Q_EMIT autotileEnabledChanged();
        setNeedsSave(true);
    }
}

// ── Algorithm getters ───────────────────────────────────────────────────

QString KCMAutotiling::autotileAlgorithm() const
{
    return m_settings->autotileAlgorithm();
}

qreal KCMAutotiling::autotileSplitRatio() const
{
    return m_settings->autotileSplitRatio();
}

int KCMAutotiling::autotileMasterCount() const
{
    return m_settings->autotileMasterCount();
}

qreal KCMAutotiling::autotileCenteredMasterSplitRatio() const
{
    return m_settings->autotileCenteredMasterSplitRatio();
}

int KCMAutotiling::autotileCenteredMasterMasterCount() const
{
    return m_settings->autotileCenteredMasterMasterCount();
}

// ── Algorithm setters ───────────────────────────────────────────────────

void KCMAutotiling::setAutotileAlgorithm(const QString& algorithm)
{
    if (m_settings->autotileAlgorithm() != algorithm) {
        m_settings->setAutotileAlgorithm(algorithm);
        Q_EMIT autotileAlgorithmChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileSplitRatio(qreal ratio)
{
    ratio = qBound(0.1, ratio, 0.9);
    if (!qFuzzyCompare(1.0 + m_settings->autotileSplitRatio(), 1.0 + ratio)) {
        m_settings->setAutotileSplitRatio(ratio);
        Q_EMIT autotileSplitRatioChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileMasterCount(int count)
{
    count = qBound(1, count, 5);
    if (m_settings->autotileMasterCount() != count) {
        m_settings->setAutotileMasterCount(count);
        Q_EMIT autotileMasterCountChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileCenteredMasterSplitRatio(qreal ratio)
{
    ratio = qBound(0.1, ratio, 0.9);
    if (!qFuzzyCompare(1.0 + m_settings->autotileCenteredMasterSplitRatio(), 1.0 + ratio)) {
        m_settings->setAutotileCenteredMasterSplitRatio(ratio);
        Q_EMIT autotileCenteredMasterSplitRatioChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileCenteredMasterMasterCount(int count)
{
    count = qBound(1, count, 5);
    if (m_settings->autotileCenteredMasterMasterCount() != count) {
        m_settings->setAutotileCenteredMasterMasterCount(count);
        Q_EMIT autotileCenteredMasterMasterCountChanged();
        setNeedsSave(true);
    }
}

// ── Gaps getters ────────────────────────────────────────────────────────

int KCMAutotiling::autotileInnerGap() const
{
    return m_settings->autotileInnerGap();
}

int KCMAutotiling::autotileOuterGap() const
{
    return m_settings->autotileOuterGap();
}

bool KCMAutotiling::autotileSmartGaps() const
{
    return m_settings->autotileSmartGaps();
}

bool KCMAutotiling::autotileUsePerSideOuterGap() const
{
    return m_settings->autotileUsePerSideOuterGap();
}

int KCMAutotiling::autotileOuterGapTop() const
{
    return m_settings->autotileOuterGapTop();
}

int KCMAutotiling::autotileOuterGapBottom() const
{
    return m_settings->autotileOuterGapBottom();
}

int KCMAutotiling::autotileOuterGapLeft() const
{
    return m_settings->autotileOuterGapLeft();
}

int KCMAutotiling::autotileOuterGapRight() const
{
    return m_settings->autotileOuterGapRight();
}

// ── Gaps setters ────────────────────────────────────────────────────────

void KCMAutotiling::setAutotileInnerGap(int gap)
{
    gap = qBound(0, gap, 50);
    if (m_settings->autotileInnerGap() != gap) {
        m_settings->setAutotileInnerGap(gap);
        Q_EMIT autotileInnerGapChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileOuterGap(int gap)
{
    gap = qBound(0, gap, 50);
    if (m_settings->autotileOuterGap() != gap) {
        m_settings->setAutotileOuterGap(gap);
        Q_EMIT autotileOuterGapChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileSmartGaps(bool smart)
{
    if (m_settings->autotileSmartGaps() != smart) {
        m_settings->setAutotileSmartGaps(smart);
        Q_EMIT autotileSmartGapsChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileUsePerSideOuterGap(bool enabled)
{
    if (m_settings->autotileUsePerSideOuterGap() != enabled) {
        m_settings->setAutotileUsePerSideOuterGap(enabled);
        Q_EMIT autotileUsePerSideOuterGapChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileOuterGapTop(int gap)
{
    gap = qBound(0, gap, 50);
    if (m_settings->autotileOuterGapTop() != gap) {
        m_settings->setAutotileOuterGapTop(gap);
        Q_EMIT autotileOuterGapTopChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileOuterGapBottom(int gap)
{
    gap = qBound(0, gap, 50);
    if (m_settings->autotileOuterGapBottom() != gap) {
        m_settings->setAutotileOuterGapBottom(gap);
        Q_EMIT autotileOuterGapBottomChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileOuterGapLeft(int gap)
{
    gap = qBound(0, gap, 50);
    if (m_settings->autotileOuterGapLeft() != gap) {
        m_settings->setAutotileOuterGapLeft(gap);
        Q_EMIT autotileOuterGapLeftChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileOuterGapRight(int gap)
{
    gap = qBound(0, gap, 50);
    if (m_settings->autotileOuterGapRight() != gap) {
        m_settings->setAutotileOuterGapRight(gap);
        Q_EMIT autotileOuterGapRightChanged();
        setNeedsSave(true);
    }
}

// ── Behavior getters ────────────────────────────────────────────────────

bool KCMAutotiling::autotileFocusNewWindows() const
{
    return m_settings->autotileFocusNewWindows();
}

int KCMAutotiling::autotileMaxWindows() const
{
    return m_settings->autotileMaxWindows();
}

int KCMAutotiling::autotileInsertPosition() const
{
    return m_settings->autotileInsertPositionInt();
}

bool KCMAutotiling::autotileFocusFollowsMouse() const
{
    return m_settings->autotileFocusFollowsMouse();
}

bool KCMAutotiling::autotileRespectMinimumSize() const
{
    return m_settings->autotileRespectMinimumSize();
}

// ── Behavior setters ────────────────────────────────────────────────────

void KCMAutotiling::setAutotileFocusNewWindows(bool focus)
{
    if (m_settings->autotileFocusNewWindows() != focus) {
        m_settings->setAutotileFocusNewWindows(focus);
        Q_EMIT autotileFocusNewWindowsChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileMaxWindows(int max)
{
    max = qBound(1, max, 12);
    if (m_settings->autotileMaxWindows() != max) {
        m_settings->setAutotileMaxWindows(max);
        Q_EMIT autotileMaxWindowsChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileInsertPosition(int position)
{
    position = qBound(0, position, 2);
    if (m_settings->autotileInsertPositionInt() != position) {
        m_settings->setAutotileInsertPositionInt(position);
        Q_EMIT autotileInsertPositionChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileFocusFollowsMouse(bool follows)
{
    if (m_settings->autotileFocusFollowsMouse() != follows) {
        m_settings->setAutotileFocusFollowsMouse(follows);
        Q_EMIT autotileFocusFollowsMouseChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileRespectMinimumSize(bool respect)
{
    if (m_settings->autotileRespectMinimumSize() != respect) {
        m_settings->setAutotileRespectMinimumSize(respect);
        Q_EMIT autotileRespectMinimumSizeChanged();
        setNeedsSave(true);
    }
}

// ── Decorations / Borders getters ───────────────────────────────────────

bool KCMAutotiling::autotileHideTitleBars() const
{
    return m_settings->autotileHideTitleBars();
}

bool KCMAutotiling::autotileShowBorder() const
{
    return m_settings->autotileShowBorder();
}

int KCMAutotiling::autotileBorderWidth() const
{
    return m_settings->autotileBorderWidth();
}

int KCMAutotiling::autotileBorderRadius() const
{
    return m_settings->autotileBorderRadius();
}

QColor KCMAutotiling::autotileBorderColor() const
{
    return m_settings->autotileBorderColor();
}

QColor KCMAutotiling::autotileInactiveBorderColor() const
{
    return m_settings->autotileInactiveBorderColor();
}

bool KCMAutotiling::autotileUseSystemBorderColors() const
{
    return m_settings->autotileUseSystemBorderColors();
}

// ── Decorations / Borders setters ───────────────────────────────────────

void KCMAutotiling::setAutotileHideTitleBars(bool hide)
{
    if (m_settings->autotileHideTitleBars() != hide) {
        m_settings->setAutotileHideTitleBars(hide);
        Q_EMIT autotileHideTitleBarsChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileShowBorder(bool show)
{
    if (m_settings->autotileShowBorder() != show) {
        m_settings->setAutotileShowBorder(show);
        Q_EMIT autotileShowBorderChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileBorderWidth(int width)
{
    width = qBound(0, width, 10);
    if (m_settings->autotileBorderWidth() != width) {
        m_settings->setAutotileBorderWidth(width);
        Q_EMIT autotileBorderWidthChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileBorderRadius(int radius)
{
    radius = qBound(0, radius, 20);
    if (m_settings->autotileBorderRadius() != radius) {
        m_settings->setAutotileBorderRadius(radius);
        Q_EMIT autotileBorderRadiusChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileBorderColor(const QColor& color)
{
    if (m_settings->autotileBorderColor() != color) {
        m_settings->setAutotileBorderColor(color);
        Q_EMIT autotileBorderColorChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileInactiveBorderColor(const QColor& color)
{
    if (m_settings->autotileInactiveBorderColor() != color) {
        m_settings->setAutotileInactiveBorderColor(color);
        Q_EMIT autotileInactiveBorderColorChanged();
        setNeedsSave(true);
    }
}

void KCMAutotiling::setAutotileUseSystemBorderColors(bool use)
{
    if (m_settings->autotileUseSystemBorderColors() != use) {
        m_settings->setAutotileUseSystemBorderColors(use);
        Q_EMIT autotileUseSystemBorderColorsChanged();
        setNeedsSave(true);
    }
}

// ── Screens ─────────────────────────────────────────────────────────────

QVariantList KCMAutotiling::screens() const
{
    return m_screenHelper->screens();
}

void KCMAutotiling::refreshScreens()
{
    m_screenHelper->refreshScreens();
}

// ── Algorithm helpers ───────────────────────────────────────────────────

QVariantList KCMAutotiling::availableAlgorithms() const
{
    QVariantList algorithms;
    auto* registry = AlgorithmRegistry::instance();
    for (const QString& id : registry->availableAlgorithms()) {
        TilingAlgorithm* algo = registry->algorithm(id);
        if (algo) {
            QVariantMap algoMap;
            algoMap[QStringLiteral("id")] = id;
            algoMap[QStringLiteral("name")] = algo->name();
            algoMap[QStringLiteral("description")] = algo->description();
            algoMap[QStringLiteral("defaultMaxWindows")] = algo->defaultMaxWindows();
            algorithms.append(algoMap);
        }
    }
    return algorithms;
}

QVariantList KCMAutotiling::generateAlgorithmPreview(const QString& algorithmId, int windowCount, double splitRatio,
                                                     int masterCount) const
{
    auto* registry = AlgorithmRegistry::instance();
    TilingAlgorithm* algo = registry->algorithm(algorithmId);
    if (!algo) {
        return {};
    }

    const int previewSize = 1000;
    const QRect previewRect(0, 0, previewSize, previewSize);

    TilingState state(QStringLiteral("preview"));
    state.setMasterCount(masterCount);
    state.setSplitRatio(splitRatio);

    const int count = qMax(1, windowCount);
    QVector<QRect> zones = algo->calculateZones({count, previewRect, &state, 0, {}});

    return AlgorithmRegistry::zonesToRelativeGeometry(zones, previewRect);
}

// ── Per-screen settings ─────────────────────────────────────────────────

QVariantMap KCMAutotiling::getPerScreenAutotileSettings(const QString& screenName) const
{
    return PerScreen::get(m_settings, screenName, &Settings::getPerScreenAutotileSettings);
}

void KCMAutotiling::setPerScreenAutotileSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    PerScreen::set(m_settings, screenName, key, value, &Settings::setPerScreenAutotileSetting);
    setNeedsSave(true);
}

void KCMAutotiling::clearPerScreenAutotileSettings(const QString& screenName)
{
    PerScreen::clear(m_settings, screenName, &Settings::clearPerScreenAutotileSettings);
    setNeedsSave(true);
}

bool KCMAutotiling::hasPerScreenAutotileSettings(const QString& screenName) const
{
    return PerScreen::has(m_settings, screenName, &Settings::hasPerScreenAutotileSettings);
}

// ── Monitor disable ─────────────────────────────────────────────────────

bool KCMAutotiling::isMonitorDisabled(const QString& screenName) const
{
    return m_screenHelper->isMonitorDisabled(screenName);
}

void KCMAutotiling::setMonitorDisabled(const QString& screenName, bool disabled)
{
    m_screenHelper->setMonitorDisabled(screenName, disabled);
}

} // namespace PlasmaZones

#include "kcmautotiling.moc"
