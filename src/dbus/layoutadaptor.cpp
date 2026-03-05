// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutadaptor.h"
#include "../core/interfaces.h"
#include "../core/layout.h"
#include "../core/layoutfactory.h"
#include "../core/zone.h"
#include "../core/constants.h"
#include "../core/layoututils.h"
#include "../autotile/AlgorithmRegistry.h"
#include "../autotile/TilingAlgorithm.h"
#include "../core/virtualdesktopmanager.h"
#include "../core/activitymanager.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include "../core/utils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScreen>

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
}

LayoutAdaptor::LayoutAdaptor(LayoutManager* manager, VirtualDesktopManager* vdm, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_layoutManager(manager)
    , m_virtualDesktopManager(vdm)
{
    Q_ASSERT(manager);
    connectLayoutManagerSignals();
    connectVirtualDesktopSignals();
}

void LayoutAdaptor::connectLayoutManagerSignals()
{
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &LayoutAdaptor::onActiveLayoutChanged);
    connect(m_layoutManager, &LayoutManager::layoutsChanged, this, &LayoutAdaptor::onLayoutsChanged);
    connect(m_layoutManager, &LayoutManager::layoutAssigned, this, &LayoutAdaptor::onLayoutAssigned);
}

void LayoutAdaptor::onActiveLayoutChanged(Layout* layout)
{
    m_cachedActiveLayoutId = QUuid();
    m_cachedActiveLayoutJson.clear();
    if (layout) {
        Q_EMIT layoutChanged(QString::fromUtf8(QJsonDocument(layout->toJson()).toJson()));
        qCInfo(lcDbusLayout) << "Emitting activeLayoutIdChanged D-Bus signal for:" << layout->id().toString();
        Q_EMIT activeLayoutIdChanged(layout->id().toString());
    }
}

void LayoutAdaptor::onLayoutsChanged()
{
    invalidateCache();
    Q_EMIT layoutListChanged();
}

void LayoutAdaptor::onLayoutAssigned(const QString& screen, Layout* layout)
{
    if (layout) {
        Q_EMIT screenLayoutChanged(screen, layout->id().toString());
    } else {
        // layout is nullptr for autotile assignments or cleared assignments
        // Try to retrieve the raw assignment ID for the signal
        int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        QString activity = m_activityManager ? m_activityManager->currentActivity() : QString();
        QString assignmentId = m_layoutManager->assignmentIdForScreen(screen, desktop, activity);
        Q_EMIT screenLayoutChanged(screen, assignmentId);
    }
}

void LayoutAdaptor::setVirtualDesktopManager(VirtualDesktopManager* vdm)
{
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
    if (id.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot" << operation << "- empty ID";
        return std::nullopt;
    }

    auto uuidOpt = Utils::parseUuid(id);
    if (!uuidOpt) {
        qCWarning(lcDbusLayout) << "Invalid UUID format for" << operation << ":" << id;
        return std::nullopt;
    }

    return uuidOpt;
}

Layout* LayoutAdaptor::getValidatedLayout(const QString& id, const QString& operation)
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
    if (value.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot" << operation << "- empty" << paramName;
        return false;
    }
    return true;
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
// Layout Queries
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

    const auto entries = LayoutUtils::buildUnifiedLayoutList(m_layoutManager, /*includeAutotile=*/true);
    for (const auto& entry : entries) {
        QJsonObject json = LayoutUtils::toJson(entry);

        auto uuidOpt = Utils::parseUuid(entry.id);
        if (uuidOpt) {
            Layout* layout = m_layoutManager->layoutById(*uuidOpt);
            if (layout) {
                json[JsonKeys::IsSystem] = layout->isSystemLayout();
                json[JsonKeys::Type] = static_cast<int>(layout->type());
                json[JsonKeys::HiddenFromSelector] = layout->hiddenFromSelector();
                if (layout->defaultOrder() != 999) {
                    json[JsonKeys::DefaultOrder] = layout->defaultOrder();
                }

                // Include allow-lists so KCM can show the filter badge
                LayoutUtils::serializeAllowLists(json, layout->allowedScreens(), layout->allowedDesktops(),
                                                 layout->allowedActivities());
            }
        }

        result.append(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
    }

    return result;
}

QString LayoutAdaptor::getLayout(const QString& id)
{
    // Handle autotile algorithm preview layouts
    if (LayoutId::isAutotile(id)) {
        QString algoId = LayoutId::extractAlgorithmId(id);
        auto* registry = AlgorithmRegistry::instance();
        auto* algo = registry ? registry->algorithm(algoId) : nullptr;
        if (!algo) {
            qCWarning(lcDbusLayout) << "Autotile algorithm not found:" << algoId;
            return QString();
        }
        UnifiedLayoutEntry entry;
        entry.id = id;
        entry.name = algo->name();
        entry.description = algo->description();
        entry.isAutotile = true;
        entry.previewZones = AlgorithmRegistry::generatePreviewZones(algo);
        entry.zones = entry.previewZones;
        entry.zoneCount = AlgorithmRegistry::effectiveMaxWindows(algo);
        QJsonObject json = LayoutUtils::toJson(entry);
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
        qCWarning(lcDbusLayout) << "Layout not found:" << id;
        return QString();
    }

    QString json = QString::fromUtf8(QJsonDocument(layout->toJson()).toJson());
    m_cachedLayoutJson[uuid] = json;
    return json;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Visibility Filtering
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutAdaptor::setLayoutHidden(const QString& layoutId, bool hidden)
{
    auto* layout = getValidatedLayout(layoutId, QStringLiteral("set layout hidden"));
    if (!layout) {
        return;
    }

    layout->setHiddenFromSelector(hidden);
    // Note: saveLayouts() is triggered automatically via layoutModified signal

    qCInfo(lcDbusLayout) << "Set layout" << layoutId << "hidden:" << hidden;
    Q_EMIT layoutChanged(QString::fromUtf8(QJsonDocument(layout->toJson()).toJson()));
    Q_EMIT layoutListChanged();
}

void LayoutAdaptor::setLayoutAutoAssign(const QString& layoutId, bool enabled)
{
    auto* layout = getValidatedLayout(layoutId, QStringLiteral("set layout auto-assign"));
    if (!layout) {
        return;
    }

    layout->setAutoAssign(enabled);
    // Note: saveLayouts() is triggered automatically via layoutModified signal

    qCInfo(lcDbusLayout) << "Set layout" << layoutId << "autoAssign:" << enabled;
    Q_EMIT layoutChanged(QString::fromUtf8(QJsonDocument(layout->toJson()).toJson()));
    Q_EMIT layoutListChanged();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Layout Management
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutAdaptor::setActiveLayout(const QString& id)
{
    auto* layout = getValidatedLayout(id, QStringLiteral("set active layout"));
    if (!layout) {
        return;
    }

    m_layoutManager->setActiveLayoutById(layout->id());
}

void LayoutAdaptor::applyQuickLayout(int number, const QString& screenName)
{
    m_layoutManager->applyQuickLayout(number, Utils::screenIdForName(screenName));
}

QString LayoutAdaptor::createLayout(const QString& name, const QString& type)
{
    if (!validateNonEmpty(name, QStringLiteral("name"), QStringLiteral("create layout"))) {
        return QString();
    }

    Layout* layout = LayoutFactory::create(type, m_layoutManager);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Failed to create layout of type:" << type;
        return QString();
    }

    layout->setName(name);
    m_layoutManager->addLayout(layout);

    qCInfo(lcDbusLayout) << "Created layout" << name << "of type" << type;
    return layout->id().toString();
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
    m_layoutManager->removeLayoutById(uuid);

    m_cachedLayoutJson.remove(uuid);
    if (m_cachedActiveLayoutId == uuid) {
        m_cachedActiveLayoutId = QUuid();
        m_cachedActiveLayoutJson.clear();
    }
    qCInfo(lcDbusLayout) << "Deleted layout" << id;
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
    return duplicate->id().toString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Quick Layout Slots
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
    if (!layoutId.isEmpty() && !LayoutId::isAutotile(layoutId)) {
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
        if (!layoutId.isEmpty() && !LayoutId::isAutotile(layoutId)) {
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

} // namespace PlasmaZones
