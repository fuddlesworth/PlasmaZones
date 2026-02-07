// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutadaptor.h"
#include "../core/interfaces.h"
#include "../core/layout.h"
#include "../core/layoutfactory.h"
#include "../core/zone.h"
#include "../core/constants.h"
#include "../core/layoututils.h"
#include "../core/virtualdesktopmanager.h"
#include "../core/activitymanager.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include "../core/utils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>

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
        qCDebug(lcDbusLayout) << "Emitting activeLayoutIdChanged D-Bus signal for:" << layout->id().toString();
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
    Q_EMIT screenLayoutChanged(screen, layout ? layout->id().toString() : QString());
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

void LayoutAdaptor::launchEditor(const QStringList& args, const QString& description)
{
    static const QString editor = []() {
        QString found = QStandardPaths::findExecutable(QStringLiteral("plasmazones-editor"));
        if (!found.isEmpty()) {
            return found;
        }

        QString appDir = QCoreApplication::applicationDirPath();
        QString localEditor = appDir + QStringLiteral("/plasmazones-editor");
        if (QFile::exists(localEditor)) {
            return localEditor;
        }

        return QStringLiteral("plasmazones-editor");
    }();

    qCDebug(lcDbusLayout) << "Launching editor" << description;
    if (!QProcess::startDetached(editor, args)) {
        qCWarning(lcDbusLayout) << "Failed to launch editor" << description;
    }
}

QJsonObject LayoutAdaptor::buildActivityInfoJson(const QString& activityId) const
{
    QJsonObject info;
    info[QLatin1String("id")] = activityId;
    if (m_activityManager) {
        info[QLatin1String("name")] = m_activityManager->activityName(activityId);
        info[QLatin1String("icon")] = m_activityManager->activityIcon(activityId);
    }
    return info;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Layout Queries
// ═══════════════════════════════════════════════════════════════════════════════

QString LayoutAdaptor::getActiveLayout()
{
    auto* layout = m_layoutManager->activeLayout();
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

    const auto entries = LayoutUtils::buildUnifiedLayoutList(m_layoutManager);
    for (const auto& entry : entries) {
        QJsonObject json = LayoutUtils::toJson(entry);

        auto uuidOpt = Utils::parseUuid(entry.id);
        if (uuidOpt) {
            Layout* layout = m_layoutManager->layoutById(*uuidOpt);
            if (layout) {
                json[JsonKeys::IsSystem] = layout->isSystemLayout();
                json[JsonKeys::Type] = static_cast<int>(layout->type());
                json[JsonKeys::HiddenFromSelector] = layout->hiddenFromSelector();

                // Include allow-lists so KCM can show the filter badge
                LayoutUtils::serializeAllowLists(json, layout->allowedScreens(),
                                                  layout->allowedDesktops(), layout->allowedActivities());
            }
        }

        result.append(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
    }

    return result;
}

QString LayoutAdaptor::getLayout(const QString& id)
{
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

    qCDebug(lcDbusLayout) << "Set layout" << layoutId << "hidden:" << hidden;
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
    m_layoutManager->applyQuickLayout(number, screenName);
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
    m_layoutManager->saveLayouts();

    qCDebug(lcDbusLayout) << "Created layout" << name << "of type" << type;
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
    qCDebug(lcDbusLayout) << "Deleted layout" << id;
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

    qCDebug(lcDbusLayout) << "Duplicated layout" << id << "to" << duplicate->id();
    return duplicate->id().toString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Import/Export
// ═══════════════════════════════════════════════════════════════════════════════

QString LayoutAdaptor::importLayout(const QString& filePath)
{
    if (!validateNonEmpty(filePath, QStringLiteral("file path"), QStringLiteral("import layout"))) {
        return QString();
    }

    int layoutCountBefore = m_layoutManager->layouts().size();
    m_layoutManager->importLayout(filePath);

    const auto layouts = m_layoutManager->layouts();
    if (layouts.size() > layoutCountBefore) {
        Layout* newLayout = layouts.last();
        qCDebug(lcDbusLayout) << "Imported layout from" << filePath << "with ID" << newLayout->id();
        return newLayout->id().toString();
    }

    qCWarning(lcDbusLayout) << "Failed to import layout from" << filePath;
    return QString();
}

void LayoutAdaptor::exportLayout(const QString& layoutId, const QString& filePath)
{
    if (!validateNonEmpty(filePath, QStringLiteral("file path"), QStringLiteral("export layout"))) {
        return;
    }

    auto* layout = getValidatedLayout(layoutId, QStringLiteral("export layout"));
    if (!layout) {
        return;
    }

    m_layoutManager->exportLayout(layout, filePath);
    qCDebug(lcDbusLayout) << "Exported layout" << layoutId << "to" << filePath;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Editor Support
// ═══════════════════════════════════════════════════════════════════════════════

bool LayoutAdaptor::updateLayout(const QString& layoutJson)
{
    if (!validateNonEmpty(layoutJson, QStringLiteral("JSON"), QStringLiteral("update layout"))) {
        return false;
    }

    auto objOpt = parseJsonObject(layoutJson, QStringLiteral("update layout"));
    if (!objOpt) {
        return false;
    }
    QJsonObject obj = *objOpt;
    QString idStr = obj[JsonKeys::Id].toString();

    auto* layout = getValidatedLayout(idStr, QStringLiteral("update layout"));
    if (!layout) {
        return false;
    }
    QUuid layoutId = layout->id();

    // Update basic properties
    layout->setName(obj[JsonKeys::Name].toString());

    // Update shader settings
    layout->setShaderId(obj[JsonKeys::ShaderId].toString());
    if (obj.contains(JsonKeys::ShaderParams)) {
        layout->setShaderParams(obj[JsonKeys::ShaderParams].toObject().toVariantMap());
    } else {
        layout->setShaderParams(QVariantMap());
    }

    // Update visibility allow-lists
    {
        QStringList screens;
        QList<int> desktops;
        QStringList activities;
        LayoutUtils::deserializeAllowLists(obj, screens, desktops, activities);
        layout->setAllowedScreens(screens);
        layout->setAllowedDesktops(desktops);
        layout->setAllowedActivities(activities);
    }

    // Update app-to-zone rules
    if (obj.contains(JsonKeys::AppRules)) {
        layout->setAppRules(AppRule::fromJsonArray(obj[JsonKeys::AppRules].toArray()));
    }

    // Clear existing zones and add new ones
    layout->clearZones();

    const auto zonesArray = obj[JsonKeys::Zones].toArray();
    for (const auto& zoneVal : zonesArray) {
        QJsonObject zoneObj = zoneVal.toObject();
        auto* zone = new Zone(layout);

        zone->setName(zoneObj[JsonKeys::Name].toString());
        zone->setZoneNumber(zoneObj[JsonKeys::ZoneNumber].toInt());

        QJsonObject relGeo = zoneObj[JsonKeys::RelativeGeometry].toObject();
        zone->setRelativeGeometry(QRectF(relGeo[JsonKeys::X].toDouble(), relGeo[JsonKeys::Y].toDouble(),
                                         relGeo[JsonKeys::Width].toDouble(), relGeo[JsonKeys::Height].toDouble()));

        QJsonObject appearance = zoneObj[JsonKeys::Appearance].toObject();
        if (!appearance.isEmpty()) {
            zone->setHighlightColor(QColor(appearance[JsonKeys::HighlightColor].toString()));
            zone->setInactiveColor(QColor(appearance[JsonKeys::InactiveColor].toString()));
            zone->setBorderColor(QColor(appearance[JsonKeys::BorderColor].toString()));

            if (appearance.contains(JsonKeys::ActiveOpacity)) {
                zone->setActiveOpacity(appearance[JsonKeys::ActiveOpacity].toDouble());
            }
            if (appearance.contains(JsonKeys::InactiveOpacity)) {
                zone->setInactiveOpacity(appearance[JsonKeys::InactiveOpacity].toDouble());
            }
            if (appearance.contains(JsonKeys::BorderWidth)) {
                zone->setBorderWidth(appearance[JsonKeys::BorderWidth].toInt());
            }
            if (appearance.contains(JsonKeys::BorderRadius)) {
                zone->setBorderRadius(appearance[JsonKeys::BorderRadius].toInt());
            }
            if (appearance.contains(JsonKeys::UseCustomColors)) {
                zone->setUseCustomColors(appearance[JsonKeys::UseCustomColors].toBool());
            }
        }

        layout->addZone(zone);
    }

    m_layoutManager->saveLayouts();

    m_cachedLayoutJson.remove(layoutId);
    if (m_cachedActiveLayoutId == layoutId) {
        m_cachedActiveLayoutId = QUuid();
        m_cachedActiveLayoutJson.clear();
    }

    Q_EMIT layoutChanged(QString::fromUtf8(QJsonDocument(layout->toJson()).toJson()));
    return true;
}

QString LayoutAdaptor::createLayoutFromJson(const QString& layoutJson)
{
    if (!validateNonEmpty(layoutJson, QStringLiteral("JSON"), QStringLiteral("create layout from JSON"))) {
        return QString();
    }

    auto objOpt = parseJsonObject(layoutJson, QStringLiteral("create layout from JSON"));
    if (!objOpt) {
        return QString();
    }

    auto* layout = Layout::fromJson(*objOpt, m_layoutManager);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Failed to create layout from JSON";
        return QString();
    }

    m_layoutManager->addLayout(layout);
    m_layoutManager->saveLayouts();

    qCDebug(lcDbusLayout) << "Created layout from JSON:" << layout->id();
    return layout->id().toString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Editor Launch
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutAdaptor::openEditor()
{
    launchEditor({}, QString());
}

void LayoutAdaptor::openEditorForScreen(const QString& screenName)
{
    launchEditor({QStringLiteral("--screen"), screenName}, QStringLiteral("for screen: %1").arg(screenName));
}

void LayoutAdaptor::openEditorForLayout(const QString& layoutId)
{
    launchEditor({QStringLiteral("--layout"), layoutId}, QStringLiteral("for layout: %1").arg(layoutId));
}

void LayoutAdaptor::openEditorForLayoutOnScreen(const QString& layoutId, const QString& screenName)
{
    QStringList args{QStringLiteral("--layout"), layoutId};
    if (!screenName.isEmpty()) {
        args << QStringLiteral("--screen") << screenName;
    }
    launchEditor(args, QStringLiteral("for layout: %1 on screen: %2").arg(layoutId, screenName));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen Assignments
// ═══════════════════════════════════════════════════════════════════════════════

QString LayoutAdaptor::getLayoutForScreen(const QString& screenName)
{
    int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    QString activity = m_activityManager ? m_activityManager->currentActivity() : QString();
    auto* layout = m_layoutManager->layoutForScreen(screenName, desktop, activity);
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    if (!validateNonEmpty(screenName, QStringLiteral("screen name"), QStringLiteral("assign layout"))) {
        return;
    }

    auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen"));
    if (!layout) {
        return;
    }

    m_layoutManager->assignLayoutById(screenName, 0, QString(), layout->id());
    m_layoutManager->saveAssignments();
    qCDebug(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenName;
}

void LayoutAdaptor::clearAssignment(const QString& screenName)
{
    m_layoutManager->clearAssignment(screenName);
    m_layoutManager->saveAssignments();
}

void LayoutAdaptor::setAllScreenAssignments(const QVariantMap& assignments)
{
    QHash<QString, QUuid> parsedAssignments;

    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenName = it.key();
        QString layoutId = it.value().toString();

        QUuid uuid;
        if (!layoutId.isEmpty()) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch screen assignment"));
            if (!uuidOpt) {
                continue;
            }
            uuid = *uuidOpt;
        }
        parsedAssignments[screenName] = uuid;
    }

    m_layoutManager->setAllScreenAssignments(parsedAssignments);
    qCDebug(lcDbusLayout) << "Batch set" << parsedAssignments.size() << "screen assignments";
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

    auto* layout = m_layoutManager->layoutForShortcut(slotNumber);
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9) {
        qCWarning(lcDbusLayout) << "Invalid quick layout slot number:" << slotNumber << "(must be 1-9)";
        return;
    }

    QUuid uuid;
    if (!layoutId.isEmpty()) {
        auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("set quick layout slot"));
        if (!uuidOpt) {
            return;
        }
        uuid = *uuidOpt;
    }

    m_layoutManager->setQuickLayoutSlot(slotNumber, uuid);
    qCDebug(lcDbusLayout) << "Set quick layout slot" << slotNumber << "to" << layoutId;
    Q_EMIT quickLayoutSlotsChanged();
}

void LayoutAdaptor::setAllQuickLayoutSlots(const QVariantMap& slots)
{
    QHash<int, QUuid> parsedSlots;

    for (auto it = slots.begin(); it != slots.end(); ++it) {
        bool ok;
        int slotNumber = it.key().toInt(&ok);
        if (!ok || slotNumber < 1 || slotNumber > 9) {
            qCWarning(lcDbusLayout) << "Invalid slot key:" << it.key();
            continue;
        }

        QString layoutId = it.value().toString();
        QUuid uuid;
        if (!layoutId.isEmpty()) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch quick layout slot"));
            if (!uuidOpt) {
                continue;
            }
            uuid = *uuidOpt;
        }
        parsedSlots[slotNumber] = uuid;
    }

    m_layoutManager->setAllQuickLayoutSlots(parsedSlots);
    qCDebug(lcDbusLayout) << "Batch set" << parsedSlots.size() << "quick layout slots";
    Q_EMIT quickLayoutSlotsChanged();
}

QVariantMap LayoutAdaptor::getAllQuickLayoutSlots()
{
    QVariantMap result;
    auto slots = m_layoutManager->quickLayoutSlots();
    for (auto it = slots.begin(); it != slots.end(); ++it) {
        result[QString::number(it.key())] = it.value().toString();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-Virtual-Desktop Assignments
// ═══════════════════════════════════════════════════════════════════════════════

QString LayoutAdaptor::getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop)
{
    auto* layout = m_layoutManager->layoutForScreen(screenName, virtualDesktop, QString());
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId)
{
    if (!validateNonEmpty(screenName, QStringLiteral("screen name"), QStringLiteral("assign layout to desktop"))) {
        return;
    }

    auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen desktop"));
    if (!layout) {
        return;
    }

    m_layoutManager->assignLayoutById(screenName, virtualDesktop, QString(), layout->id());
    m_layoutManager->saveAssignments();
    qCDebug(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenName << "on desktop" << virtualDesktop;

    if (m_virtualDesktopManager) {
        int currentDesktop = m_virtualDesktopManager->currentDesktop();
        if (virtualDesktop == 0 || virtualDesktop == currentDesktop) {
            QMetaObject::invokeMethod(m_virtualDesktopManager, "updateActiveLayout", Qt::QueuedConnection);
        }
    }
}

void LayoutAdaptor::clearAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop)
{
    m_layoutManager->clearAssignment(screenName, virtualDesktop, QString());
    m_layoutManager->saveAssignments();
    qCDebug(lcDbusLayout) << "Cleared assignment for screen" << screenName << "on desktop" << virtualDesktop;
}

bool LayoutAdaptor::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop)
{
    return m_layoutManager->hasExplicitAssignment(screenName, virtualDesktop, QString());
}

void LayoutAdaptor::setAllDesktopAssignments(const QVariantMap& assignments)
{
    QHash<QPair<QString, int>, QUuid> parsedAssignments;

    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() != 2) {
            qCWarning(lcDbusLayout) << "Invalid desktop assignment key format:" << it.key();
            continue;
        }

        QString screenName = parts[0];
        bool ok;
        int virtualDesktop = parts[1].toInt(&ok);
        if (!ok || virtualDesktop < 1) {
            qCWarning(lcDbusLayout) << "Invalid virtual desktop number:" << parts[1];
            continue;
        }

        QString layoutId = it.value().toString();
        QUuid uuid;
        if (!layoutId.isEmpty()) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch desktop assignment"));
            if (!uuidOpt) {
                continue;
            }
            uuid = *uuidOpt;
        }
        parsedAssignments[qMakePair(screenName, virtualDesktop)] = uuid;
    }

    m_layoutManager->setAllDesktopAssignments(parsedAssignments);
    qCDebug(lcDbusLayout) << "Batch set" << parsedAssignments.size() << "desktop assignments";
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

QString LayoutAdaptor::getAllScreenAssignments()
{
    QJsonObject root;
    const int desktopCount = getVirtualDesktopCount();

    for (QScreen* screen : Utils::allScreens()) {
        QString screenName = screen->name();
        QJsonObject screenObj;

        for (int desktop = 0; desktop <= desktopCount; ++desktop) {
            auto* layout = m_layoutManager->layoutForScreen(screenName, desktop, QString());
            if (layout) {
                QString key = (desktop == 0) ? QStringLiteral("default") : QString::number(desktop);
                screenObj[key] = layout->id().toString();
            }
        }

        if (!screenObj.isEmpty()) {
            root[screenName] = screenObj;
        }
    }

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QVariantMap LayoutAdaptor::getAllDesktopAssignments()
{
    QVariantMap result;

    const auto assignments = m_layoutManager->desktopAssignments();
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        QString key = QStringLiteral("%1:%2").arg(it.key().first).arg(it.key().second);
        result[key] = it.value().toString();
    }

    return result;
}

QVariantMap LayoutAdaptor::getAllActivityAssignments()
{
    QVariantMap result;

    const auto assignments = m_layoutManager->activityAssignments();
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        QString key = QStringLiteral("%1:%2").arg(it.key().first, it.key().second);
        result[key] = it.value().toString();
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// KDE Activities Support
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutAdaptor::setActivityManager(ActivityManager* am)
{
    if (m_activityManager) {
        disconnect(m_activityManager, nullptr, this, nullptr);
    }

    m_activityManager = am;
    connectActivitySignals();
}

void LayoutAdaptor::connectActivitySignals()
{
    if (m_activityManager) {
        connect(m_activityManager, &ActivityManager::currentActivityChanged, this,
                &LayoutAdaptor::currentActivityChanged);
        connect(m_activityManager, &ActivityManager::activitiesChanged, this,
                &LayoutAdaptor::activitiesChanged);
    }
}

bool LayoutAdaptor::isActivitiesAvailable()
{
    return ActivityManager::isAvailable();
}

QStringList LayoutAdaptor::getActivities()
{
    return m_activityManager ? m_activityManager->activities() : QStringList{};
}

QString LayoutAdaptor::getCurrentActivity()
{
    return m_activityManager ? m_activityManager->currentActivity() : QString();
}

QString LayoutAdaptor::getActivityInfo(const QString& activityId)
{
    if (!m_activityManager || activityId.isEmpty()) {
        return QStringLiteral("{}");
    }

    return QString::fromUtf8(QJsonDocument(buildActivityInfoJson(activityId)).toJson(QJsonDocument::Compact));
}

QString LayoutAdaptor::getAllActivitiesInfo()
{
    QJsonArray array;

    if (m_activityManager) {
        const auto activityIds = m_activityManager->activities();
        for (const QString& activityId : activityIds) {
            array.append(buildActivityInfoJson(activityId));
        }
    }

    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-Activity Assignments
// ═══════════════════════════════════════════════════════════════════════════════

QString LayoutAdaptor::getLayoutForScreenActivity(const QString& screenName, const QString& activityId)
{
    auto* layout = m_layoutManager->layoutForScreen(screenName, 0, activityId);
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreenActivity(const QString& screenName, const QString& activityId, const QString& layoutId)
{
    if (!validateNonEmpty(screenName, QStringLiteral("screen name"), QStringLiteral("assign layout to activity"))) {
        return;
    }
    if (!validateNonEmpty(activityId, QStringLiteral("activity ID"), QStringLiteral("assign layout to activity"))) {
        return;
    }

    auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen activity"));
    if (!layout) {
        return;
    }

    m_layoutManager->assignLayoutById(screenName, 0, activityId, layout->id());
    m_layoutManager->saveAssignments();

    qCDebug(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenName << "for activity" << activityId;

    if (m_activityManager && m_activityManager->currentActivity() == activityId) {
        QMetaObject::invokeMethod(m_activityManager, &ActivityManager::updateActiveLayout, Qt::QueuedConnection);
    }
}

void LayoutAdaptor::clearAssignmentForScreenActivity(const QString& screenName, const QString& activityId)
{
    m_layoutManager->clearAssignment(screenName, 0, activityId);
    m_layoutManager->saveAssignments();
    qCDebug(lcDbusLayout) << "Cleared assignment for screen" << screenName << "activity" << activityId;
}

bool LayoutAdaptor::hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId)
{
    return m_layoutManager->hasExplicitAssignment(screenName, 0, activityId);
}

void LayoutAdaptor::setAllActivityAssignments(const QVariantMap& assignments)
{
    QHash<QPair<QString, QString>, QUuid> parsedAssignments;

    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() != 2) {
            qCWarning(lcDbusLayout) << "Invalid activity assignment key format:" << it.key();
            continue;
        }

        QString screenName = parts[0];
        QString activityId = parts[1];
        if (screenName.isEmpty() || activityId.isEmpty()) {
            qCWarning(lcDbusLayout) << "Empty screen or activity in assignment key:" << it.key();
            continue;
        }

        QString layoutId = it.value().toString();
        QUuid uuid;
        if (!layoutId.isEmpty()) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch activity assignment"));
            if (!uuidOpt) {
                continue;
            }
            uuid = *uuidOpt;
        }
        parsedAssignments[qMakePair(screenName, activityId)] = uuid;
    }

    m_layoutManager->setAllActivityAssignments(parsedAssignments);
    qCDebug(lcDbusLayout) << "Batch set" << parsedAssignments.size() << "activity assignments";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Full Assignments (Screen + Desktop + Activity)
// ═══════════════════════════════════════════════════════════════════════════════

QString LayoutAdaptor::getLayoutForScreenDesktopActivity(const QString& screenName, int virtualDesktop, const QString& activityId)
{
    auto* layout = m_layoutManager->layoutForScreen(screenName, virtualDesktop, activityId);
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreenDesktopActivity(const QString& screenName, int virtualDesktop,
                                                         const QString& activityId, const QString& layoutId)
{
    if (!validateNonEmpty(screenName, QStringLiteral("screen name"), QStringLiteral("assign layout"))) {
        return;
    }

    auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen desktop activity"));
    if (!layout) {
        return;
    }

    m_layoutManager->assignLayoutById(screenName, virtualDesktop, activityId, layout->id());
    m_layoutManager->saveAssignments();

    qCDebug(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenName
                          << "desktop" << virtualDesktop << "activity" << activityId;

    bool affectsCurrentDesktop = (virtualDesktop == 0);
    bool affectsCurrentActivity = activityId.isEmpty();

    if (m_virtualDesktopManager) {
        affectsCurrentDesktop = affectsCurrentDesktop || (virtualDesktop == m_virtualDesktopManager->currentDesktop());
    }
    if (m_activityManager) {
        affectsCurrentActivity = affectsCurrentActivity || (activityId == m_activityManager->currentActivity());
    }

    if (affectsCurrentDesktop && affectsCurrentActivity) {
        if (m_virtualDesktopManager) {
            QMetaObject::invokeMethod(m_virtualDesktopManager, "updateActiveLayout", Qt::QueuedConnection);
        }
    }
}

void LayoutAdaptor::clearAssignmentForScreenDesktopActivity(const QString& screenName, int virtualDesktop,
                                                             const QString& activityId)
{
    m_layoutManager->clearAssignment(screenName, virtualDesktop, activityId);
    m_layoutManager->saveAssignments();
    qCDebug(lcDbusLayout) << "Cleared assignment for screen" << screenName
                          << "desktop" << virtualDesktop << "activity" << activityId;
}

} // namespace PlasmaZones
