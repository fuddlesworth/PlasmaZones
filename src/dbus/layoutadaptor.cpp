// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutadaptor.h"
#include "dbushelpers.h"
#include "../core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include "../core/layoutfactory.h"
#include <PhosphorZones/Zone.h>
#include "../core/constants.h"
#include <PhosphorZones/LayoutUtils.h>
#include "../common/layoutpreviewserialize.h"
#include "../core/unifiedlayoutlist.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include "../core/virtualdesktopmanager.h"
#include "../core/activitymanager.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include "../core/screenmanager.h"
#include "../core/utils.h"

#include <PhosphorLayoutApi/AlgorithmMetadata.h>
#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorLayoutApi/LayoutPreview.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScreen>
#include <QThread>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Constructor and Signal Setup
// ═══════════════════════════════════════════════════════════════════════════════

LayoutAdaptor::LayoutAdaptor(LayoutManager* manager, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_layoutManager(manager)
{
    Q_ASSERT(manager);
    connectLayoutManagerSignals();
    m_layoutSourceCoalesce.setSingleShot(true);
    m_layoutSourceCoalesce.setInterval(200);
    connect(&m_layoutSourceCoalesce, &QTimer::timeout, this, [this]() {
        Q_EMIT layoutListChanged();
    });
}

LayoutAdaptor::LayoutAdaptor(LayoutManager* manager, VirtualDesktopManager* vdm, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_layoutManager(manager)
    , m_virtualDesktopManager(vdm)
{
    Q_ASSERT(manager);
    connectLayoutManagerSignals();
    connectVirtualDesktopSignals();
    m_layoutSourceCoalesce.setSingleShot(true);
    m_layoutSourceCoalesce.setInterval(200);
    connect(&m_layoutSourceCoalesce, &QTimer::timeout, this, [this]() {
        Q_EMIT layoutListChanged();
    });
}

void LayoutAdaptor::connectLayoutManagerSignals()
{
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &LayoutAdaptor::onActiveLayoutChanged);
    connect(m_layoutManager, &LayoutManager::layoutsChanged, this, &LayoutAdaptor::onLayoutsChanged);
    connect(m_layoutManager, &LayoutManager::layoutAssigned, this, &LayoutAdaptor::onLayoutAssigned);
}

void LayoutAdaptor::onActiveLayoutChanged(PhosphorZones::Layout* layout)
{
    m_cachedActiveLayoutId = QUuid();
    m_cachedActiveLayoutJson.clear();
    if (layout) {
        // Compact serialisation: this is the hottest layoutChanged emit path
        // (fires on every active-layout switch). Pretty-printing drops ~30%
        // of the payload over the bus with no functional difference —
        // QJsonDocument::fromJson round-trips either form identically.
        Q_EMIT layoutChanged(QString::fromUtf8(QJsonDocument(layout->toJson()).toJson(QJsonDocument::Compact)));
        qCInfo(lcDbusLayout) << "Emitting activeLayoutIdChanged D-Bus signal for:" << layout->id().toString();
        Q_EMIT activeLayoutIdChanged(layout->id().toString());
    }
}

void LayoutAdaptor::onLayoutsChanged()
{
    invalidateCache();
    Q_EMIT layoutListChanged();
}

void LayoutAdaptor::onLayoutAssigned(const QString& screen, int virtualDesktop, PhosphorZones::Layout* layout)
{
    // Don't echo back to the KCM during setAssignmentEntry — the KCM initiated the
    // change and will reload from KConfig. The daemon's internal layoutAssigned signal
    // still fires for geometry recalc, but the D-Bus screenLayoutChanged is suppressed.
    if (m_suppressScreenLayoutSignal)
        return;

    if (layout) {
        Q_EMIT screenLayoutChanged(screen, layout->id().toString(), virtualDesktop);
    } else {
        // layout is nullptr for autotile assignments or cleared assignments.
        // Resolve the assignment ID at the EXACT level that changed:
        // - virtualDesktop > 0: per-desktop change → resolve at that desktop
        // - virtualDesktop == 0: base/display-default change → resolve at base (desktop=0)
        // Using currentDesktop() for base changes would leak per-desktop entries
        // into the display-default signal, causing the KCM to show wrong values.
        int desktop = virtualDesktop;
        QString activity = (virtualDesktop > 0 && m_activityManager) ? m_activityManager->currentActivity() : QString();
        QString assignmentId = m_layoutManager->assignmentIdForScreen(screen, desktop, activity);
        Q_EMIT screenLayoutChanged(screen, assignmentId, virtualDesktop);
    }
}

void LayoutAdaptor::setVirtualDesktopManager(VirtualDesktopManager* vdm)
{
    if (m_virtualDesktopManager) {
        disconnect(m_virtualDesktopManager, nullptr, this, nullptr);
    }

    m_virtualDesktopManager = vdm;
    connectVirtualDesktopSignals();
}

void LayoutAdaptor::connectVirtualDesktopSignals()
{
    if (m_virtualDesktopManager) {
        connect(m_virtualDesktopManager, &VirtualDesktopManager::desktopCountChanged, this,
                &LayoutAdaptor::virtualDesktopCountChanged);
    }
}

void LayoutAdaptor::invalidateCache()
{
    m_cachedActiveLayoutJson.clear();
    m_cachedActiveLayoutId = QUuid();
    m_cachedLayoutJson.clear();
}

void LayoutAdaptor::notifyLayoutListChanged()
{
    invalidateCache();
    Q_EMIT layoutListChanged();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Methods
// ═══════════════════════════════════════════════════════════════════════════════

std::optional<QUuid> LayoutAdaptor::parseAndValidateUuid(const QString& id, const QString& operation) const
{
    return DbusHelpers::parseAndValidateUuid(id, operation, lcDbusLayout);
}

PhosphorZones::Layout* LayoutAdaptor::getValidatedLayout(const QString& id, const QString& operation)
{
    auto uuidOpt = parseAndValidateUuid(id, operation);
    if (!uuidOpt) {
        return nullptr;
    }

    auto* layout = m_layoutManager->layoutById(*uuidOpt);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Cannot" << operation << "- layout not found:" << id;
        return nullptr;
    }

    return layout;
}

bool LayoutAdaptor::validateNonEmpty(const QString& value, const QString& paramName, const QString& operation) const
{
    return DbusHelpers::validateNonEmpty(value, paramName, operation, lcDbusLayout);
}

std::optional<QJsonObject> LayoutAdaptor::parseJsonObject(const QString& jsonString, const QString& operation) const
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcDbusLayout) << "Invalid JSON for" << operation << "- parse error:" << parseError.errorString()
                                << "at offset" << parseError.offset;
        return std::nullopt;
    }

    if (!doc.isObject()) {
        qCWarning(lcDbusLayout) << "JSON for" << operation << "is not an object";
        return std::nullopt;
    }

    return doc.object();
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Layout Queries
// ═══════════════════════════════════════════════════════════════════════════════

QString LayoutAdaptor::getActiveLayout()
{
    // Return the default layout (settings-based fallback) rather than
    // the transient internal active layout, so KCM and other D-Bus
    // consumers see the user's configured default.
    auto* layout = m_layoutManager->defaultLayout();
    if (!layout) {
        return QString();
    }

    if (m_cachedActiveLayoutId == layout->id() && !m_cachedActiveLayoutJson.isEmpty()) {
        return m_cachedActiveLayoutJson;
    }

    m_cachedActiveLayoutJson = QString::fromUtf8(QJsonDocument(layout->toJson()).toJson());
    m_cachedActiveLayoutId = layout->id();
    return m_cachedActiveLayoutJson;
}

QStringList LayoutAdaptor::getLayoutList()
{
    QStringList result;

    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, /*includeAutotile=*/true,
        PhosphorZones::LayoutUtils::buildCustomOrder(m_settings, /*includeManual=*/true, /*includeAutotile=*/true));
    for (const auto& entry : entries) {
        QJsonObject json = PlasmaZones::toJson(entry);

        // Enrich manual-layout entries with Layout-specific fields that
        // LayoutPreview doesn't carry (hasSystemOrigin, hiddenFromSelector,
        // defaultOrder, allow-lists). Autotile entries have no Layout to
        // look up so they skip this block.
        auto uuidOpt = Utils::parseUuid(entry.id);
        if (uuidOpt) {
            PhosphorZones::Layout* layout = m_layoutManager->layoutById(*uuidOpt);
            if (layout) {
                json[QStringLiteral("hasSystemOrigin")] = layout->hasSystemOrigin();
                json[QStringLiteral("hiddenFromSelector")] = layout->hiddenFromSelector();
                if (layout->defaultOrder() != 999) {
                    json[QStringLiteral("defaultOrder")] = layout->defaultOrder();
                }

                // Include allow-lists so KCM can show the filter badge
                PhosphorZones::LayoutUtils::serializeAllowLists(json, layout->allowedScreens(),
                                                                layout->allowedDesktops(), layout->allowedActivities());
            }
        }

        result.append(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
    }

    return result;
}

QString LayoutAdaptor::getLayout(const QString& id)
{
    // Handle autotile algorithm preview layouts
    if (PhosphorLayout::LayoutId::isAutotile(id)) {
        QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(id);
        auto* registry = PhosphorTiles::AlgorithmRegistry::instance();
        auto* algo = registry ? registry->algorithm(algoId) : nullptr;
        if (!algo) {
            qCWarning(lcDbusLayout) << "Autotile algorithm not found:" << algoId;
            return QString();
        }
        // previewFromAlgorithm applies configured params (active-algorithm
        // maxWindows, master-count, split-ratio) when the caller passes a
        // non-positive windowCount, so no adapter helper is needed here.
        PhosphorLayout::LayoutPreview preview = PhosphorTiles::previewFromAlgorithm(algoId, algo, -1);
        QJsonObject json = PlasmaZones::toJson(preview);
        // Apply stored per-algorithm overrides (gaps, visibility, shader)
        QJsonObject overrides = m_layoutManager->loadAutotileOverrides(algoId);
        for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
            json[it.key()] = it.value();
        }
        return QString::fromUtf8(QJsonDocument(json).toJson());
    }

    auto uuidOpt = parseAndValidateUuid(id, QStringLiteral("get layout"));
    if (!uuidOpt) {
        return QString();
    }
    QUuid uuid = *uuidOpt;

    if (m_cachedLayoutJson.contains(uuid)) {
        return m_cachedLayoutJson[uuid];
    }

    auto* layout = m_layoutManager->layoutById(uuid);
    if (!layout) {
        qCWarning(lcDbusLayout) << "PhosphorZones::Layout not found:" << id;
        return QString();
    }

    QString json = QString::fromUtf8(QJsonDocument(layout->toJson()).toJson());
    m_cachedLayoutJson[uuid] = json;
    return json;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Visibility Filtering
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutAdaptor::invalidateLayoutJsonCacheFor(const QUuid& uuid)
{
    m_cachedLayoutJson.remove(uuid);
    if (m_cachedActiveLayoutId == uuid) {
        m_cachedActiveLayoutId = QUuid();
        m_cachedActiveLayoutJson.clear();
    }
}

// Property-name constants for the compact layoutPropertyChanged signal.
// Centralizes the wire format so the three mutators and any future
// subscriber-side dispatcher cannot drift on spelling. Stored as
// `const QString` (via QStringLiteral's static UTF-16 storage) so the
// emit sites don't allocate a temporary QString on every property change.
namespace {
const QString kPropHidden = QStringLiteral("hidden");
const QString kPropAutoAssign = QStringLiteral("autoAssign");
const QString kPropAspectRatioClass = QStringLiteral("aspectRatioClass");
} // namespace

void LayoutAdaptor::setLayoutHidden(const QString& layoutId, bool hidden)
{
    auto* layout = getValidatedLayout(layoutId, QStringLiteral("set layout hidden"));
    if (!layout) {
        return;
    }

    // Value-equality guard: skip the mutation + cache invalidation +
    // signal fan-out when the incoming value already matches the current
    // one. Mirrors the Phase 1.1 guard in SettingsAdaptor::setSetting so
    // a settled checkbox cannot spam subscribers with no-op reloads.
    if (layout->hiddenFromSelector() == hidden) {
        return;
    }

    layout->setHiddenFromSelector(hidden);
    // Note: saveLayouts() is triggered automatically via layoutModified signal

    // Invalidate the cached JSON for this layout so a later getLayout()
    // re-serializes with the new value. Subscribers that want the full
    // shape after a property mutation pull via getLayout — the signal is
    // deliberately narrow.
    invalidateLayoutJsonCacheFor(layout->id());

    qCInfo(lcDbusLayout) << "Set layout" << layoutId << "hidden:" << hidden;
    // Phase 4 of refactor/dbus-performance: emit the compact property
    // signal (3 strings + 1 bool over the wire) instead of layoutChanged
    // with the full 5–20 KB JSON payload. layoutListChanged is likewise
    // not emitted — the list didn't change.
    Q_EMIT layoutPropertyChanged(layoutId, kPropHidden, QDBusVariant(hidden));
}

void LayoutAdaptor::setLayoutAutoAssign(const QString& layoutId, bool enabled)
{
    auto* layout = getValidatedLayout(layoutId, QStringLiteral("set layout auto-assign"));
    if (!layout) {
        return;
    }

    if (layout->autoAssign() == enabled) {
        return;
    }

    layout->setAutoAssign(enabled);
    // Note: saveLayouts() is triggered automatically via layoutModified signal

    invalidateLayoutJsonCacheFor(layout->id());

    qCInfo(lcDbusLayout) << "Set layout" << layoutId << "autoAssign:" << enabled;
    Q_EMIT layoutPropertyChanged(layoutId, kPropAutoAssign, QDBusVariant(enabled));
}

void LayoutAdaptor::setLayoutAspectRatioClass(const QString& layoutId, int aspectRatioClass)
{
    auto* layout = getValidatedLayout(layoutId, QStringLiteral("set layout aspect ratio"));
    if (!layout) {
        return;
    }

    if (static_cast<int>(layout->aspectRatioClass()) == aspectRatioClass) {
        return;
    }

    layout->setAspectRatioClassInt(aspectRatioClass);

    invalidateLayoutJsonCacheFor(layout->id());

    qCInfo(lcDbusLayout) << "Set layout" << layoutId << "aspectRatioClass:" << aspectRatioClass;
    Q_EMIT layoutPropertyChanged(layoutId, kPropAspectRatioClass, QDBusVariant(aspectRatioClass));
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Layout Management
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutAdaptor::setActiveLayout(const QString& id)
{
    auto* layout = getValidatedLayout(id, QStringLiteral("set active layout"));
    if (!layout) {
        return;
    }

    m_layoutManager->setActiveLayoutById(layout->id());
}

void LayoutAdaptor::applyQuickLayout(int number, const QString& screenId)
{
    m_layoutManager->applyQuickLayout(number, Utils::screenIdForName(screenId));
}

QString LayoutAdaptor::createLayout(const QString& name, const QString& type)
{
    if (!validateNonEmpty(name, QStringLiteral("name"), QStringLiteral("create layout"))) {
        return QString();
    }

    PhosphorZones::Layout* layout = LayoutFactory::create(type, m_layoutManager);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Failed to create layout of type:" << type;
        return QString();
    }

    layout->setName(name);

    // Auto-detect aspect ratio class from the primary screen (virtual-screen-aware)
    QScreen* screen = Utils::primaryScreen();
    if (screen) {
        const QString primaryId = Utils::screenIdentifier(screen);
        auto* mgr = ScreenManager::instance();
        QRect geo =
            (mgr && mgr->screenGeometry(primaryId).isValid()) ? mgr->screenGeometry(primaryId) : screen->geometry();
        layout->setAspectRatioClass(PhosphorLayout::ScreenClassification::classify(geo.width(), geo.height()));
    }

    m_layoutManager->addLayout(layout);

    qCInfo(lcDbusLayout) << "Created layout" << name << "of type" << type;
    // Addition-specific companion to layoutListChanged (emitted by the
    // LayoutManager::layoutsChanged fan-out). Subscribers that only care
    // about new layouts — e.g. the settings list auto-select path — can
    // react without diffing the full list.
    const QString newId = layout->id().toString();
    Q_EMIT layoutCreated(newId);
    return newId;
}

void LayoutAdaptor::deleteLayout(const QString& id)
{
    auto* layout = getValidatedLayout(id, QStringLiteral("delete layout"));
    if (!layout) {
        return;
    }

    if (layout->isSystemLayout()) {
        qCWarning(lcDbusLayout) << "Cannot delete system layout:" << id;
        return;
    }

    QUuid uuid = layout->id();
    const QString deletedId = uuid.toString();
    m_layoutManager->removeLayoutById(uuid);

    m_cachedLayoutJson.remove(uuid);
    if (m_cachedActiveLayoutId == uuid) {
        m_cachedActiveLayoutId = QUuid();
        m_cachedActiveLayoutJson.clear();
    }
    qCInfo(lcDbusLayout) << "Deleted layout" << id;
    // Deletion-specific companion to layoutListChanged — lets subscribers
    // evict per-layout state keyed by UUID before the list refresh lands.
    Q_EMIT layoutDeleted(deletedId);
}

QString LayoutAdaptor::duplicateLayout(const QString& id)
{
    auto* source = getValidatedLayout(id, QStringLiteral("duplicate layout"));
    if (!source) {
        return QString();
    }

    auto* duplicate = m_layoutManager->duplicateLayout(source);
    if (!duplicate) {
        qCWarning(lcDbusLayout) << "Failed to duplicate layout:" << id;
        return QString();
    }

    qCInfo(lcDbusLayout) << "Duplicated layout" << id << "to" << duplicate->id();
    const QString dupId = duplicate->id().toString();
    Q_EMIT layoutCreated(dupId);
    return dupId;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Quick PhosphorZones::Layout Slots
// ═══════════════════════════════════════════════════════════════════════════════

QString LayoutAdaptor::getQuickLayoutSlot(int slotNumber)
{
    if (slotNumber < 1 || slotNumber > 9) {
        qCWarning(lcDbusLayout) << "Invalid quick layout slot number:" << slotNumber << "(must be 1-9)";
        return QString();
    }

    // Return raw assignment ID (UUID string or autotile ID)
    auto slots = m_layoutManager->quickLayoutSlots();
    return slots.value(slotNumber);
}

void LayoutAdaptor::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9) {
        qCWarning(lcDbusLayout) << "Invalid quick layout slot number:" << slotNumber << "(must be 1-9)";
        return;
    }

    // Validate UUID format for manual layouts (skip for autotile IDs)
    if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("set quick layout slot"));
        if (!uuidOpt) {
            return;
        }
    }

    m_layoutManager->setQuickLayoutSlot(slotNumber, layoutId);
    qCInfo(lcDbusLayout) << "Set quick layout slot" << slotNumber << "to" << layoutId;
    Q_EMIT quickLayoutSlotsChanged();
}

void LayoutAdaptor::setAllQuickLayoutSlots(const QVariantMap& slots)
{
    QHash<int, QString> parsedSlots;

    for (auto it = slots.begin(); it != slots.end(); ++it) {
        bool ok;
        int slotNumber = it.key().toInt(&ok);
        if (!ok || slotNumber < 1 || slotNumber > 9) {
            qCWarning(lcDbusLayout) << "Invalid slot key:" << it.key();
            continue;
        }

        QString layoutId = it.value().toString();
        if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId)) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch quick layout slot"));
            if (!uuidOpt) {
                continue;
            }
        }
        parsedSlots[slotNumber] = layoutId;
    }

    m_layoutManager->setAllQuickLayoutSlots(parsedSlots);
    qCInfo(lcDbusLayout) << "Batch set" << parsedSlots.size() << "quick layout slots";
    Q_EMIT quickLayoutSlotsChanged();
}

QVariantMap LayoutAdaptor::getAllQuickLayoutSlots()
{
    QVariantMap result;
    auto slots = m_layoutManager->quickLayoutSlots();
    for (auto it = slots.begin(); it != slots.end(); ++it) {
        result[QString::number(it.key())] = it.value();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual Desktop Info
// ═══════════════════════════════════════════════════════════════════════════════

int LayoutAdaptor::getVirtualDesktopCount()
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->desktopCount() : 1;
}

QStringList LayoutAdaptor::getVirtualDesktopNames()
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->desktopNames() : QStringList{QStringLiteral("Desktop 1")};
}

// ═══════════════════════════════════════════════════════════════════════════════
// Settings Access
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutAdaptor::setSettings(ISettings* settings)
{
    m_settings = settings;
}

void LayoutAdaptor::setLayoutSource(PhosphorLayout::ILayoutSource* source)
{
    // The adaptor is a QDBusAbstractAdaptor child of the service-owning
    // QObject — signal plumbing and the coalesce QTimer live on this
    // thread only. Catch cross-thread setters at dev time before they
    // cause subtle double-emit races.
    Q_ASSERT(QThread::currentThread() == thread());
    // The daemon/editor/settings each call setLayoutSource exactly once
    // during init(); allow idempotent re-assignment with the same pointer
    // but trip on a silent swap that would leave stale connections wired.
    Q_ASSERT(!m_layoutSource || m_layoutSource == source);

    if (m_layoutSource == source)
        return;

    // Drop any prior connection into the coalesce timer before rebinding.
    // Disconnect-by-receiver: the timer is the only sink we ever wire
    // contentsChanged into, so the narrow target keeps unrelated signal
    // fan-outs on @p source intact.
    if (m_layoutSource) {
        disconnect(m_layoutSource, nullptr, &m_layoutSourceCoalesce, nullptr);
    }

    m_layoutSource = source;

    if (m_layoutSource) {
        // Coalesce bursts of contentsChanged (AlgorithmRegistry churn on
        // startup, scripted-algorithm hot-reload) into one layoutListChanged
        // D-Bus emission — see m_layoutSourceCoalesce member comment.
        connect(m_layoutSource, &PhosphorLayout::ILayoutSource::contentsChanged, &m_layoutSourceCoalesce,
                QOverload<>::of(&QTimer::start));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Source-agnostic PhosphorZones::Layout Preview (PhosphorLayout::ILayoutSource bridge)
// ═══════════════════════════════════════════════════════════════════════════════

QString LayoutAdaptor::getLayoutPreviewList()
{
    if (!m_layoutSource) {
        return QStringLiteral("[]");
    }
    QJsonArray array;
    const auto previews = m_layoutSource->availableLayouts();
    for (const auto& preview : previews) {
        array.append(PlasmaZones::toJson(preview));
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString LayoutAdaptor::getLayoutPreview(const QString& id, int windowCount)
{
    if (!m_layoutSource || id.isEmpty()) {
        return QStringLiteral("{}");
    }
    const auto preview = m_layoutSource->previewAt(id, windowCount);
    if (preview.id.isEmpty()) {
        // ILayoutSource contract: empty id signals "unknown to this source".
        return QStringLiteral("{}");
    }
    return QString::fromUtf8(QJsonDocument(PlasmaZones::toJson(preview)).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen PhosphorZones::Layout Lock
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutAdaptor::toggleScreenLock(const QString& screenId)
{
    toggleContextLock(screenId, 0, QString());
}

bool LayoutAdaptor::isScreenLocked(const QString& screenId)
{
    return isContextLocked(screenId, 0, QString());
}

void LayoutAdaptor::toggleContextLock(const QString& screenId, int virtualDesktop, const QString& activity)
{
    if (!m_settings)
        return;
    bool locked = m_settings->isContextLocked(screenId, virtualDesktop, activity);
    m_settings->setContextLocked(screenId, virtualDesktop, activity, !locked);
    m_settings->save();
}

bool LayoutAdaptor::isContextLocked(const QString& screenId, int virtualDesktop, const QString& activity)
{
    if (!m_settings)
        return false;
    return m_settings->isContextLocked(screenId, virtualDesktop, activity);
}

} // namespace PlasmaZones
