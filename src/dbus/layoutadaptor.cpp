// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutadaptor.h"
#include "../core/interfaces.h"
#include "../core/layout.h"
#include "../core/zone.h"
#include "../core/constants.h"
#include "../core/virtualdesktopmanager.h"
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
    // Connect to LayoutManager signals (concrete type - ILayoutManager has no signals)
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &LayoutAdaptor::onActiveLayoutChanged);

    connect(m_layoutManager, &LayoutManager::layoutsChanged, this, &LayoutAdaptor::onLayoutsChanged);

    connect(m_layoutManager, &LayoutManager::layoutAssigned, this, &LayoutAdaptor::onLayoutAssigned);
}

void LayoutAdaptor::onActiveLayoutChanged(Layout* layout)
{
    // Invalidate active layout cache when it changes
    m_cachedActiveLayoutId = QUuid();
    m_cachedActiveLayoutJson.clear();
    if (layout) {
        Q_EMIT layoutChanged(QString::fromUtf8(QJsonDocument(layout->toJson()).toJson()));
        // Emit activeLayoutIdChanged for KCM UI synchronization
        // This allows settings panel to update selection when layout changes via hotkey
        qCDebug(lcDbusLayout) << "Emitting activeLayoutIdChanged D-Bus signal for:" << layout->id().toString();
        Q_EMIT activeLayoutIdChanged(layout->id().toString());
    }
}

void LayoutAdaptor::onLayoutsChanged()
{
    invalidateCache(); // Invalidate cache when layouts change
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

QString LayoutAdaptor::getActiveLayout()
{
    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return QString();
    }

    // Use cache if layout hasn't changed
    if (m_cachedActiveLayoutId == layout->id() && !m_cachedActiveLayoutJson.isEmpty()) {
        return m_cachedActiveLayoutJson;
    }

    // Serialize and cache
    m_cachedActiveLayoutJson = QString::fromUtf8(QJsonDocument(layout->toJson()).toJson());
    m_cachedActiveLayoutId = layout->id();
    return m_cachedActiveLayoutJson;
}

QStringList LayoutAdaptor::getLayoutList()
{
    QStringList result;
    for (const auto* layout : m_layoutManager->layouts()) {
        QJsonObject info;
        info[JsonKeys::Id] = layout->id().toString();
        info[JsonKeys::Name] = layout->name();
        info[JsonKeys::ZoneCount] = layout->zoneCount();
        info[JsonKeys::IsSystem] = layout->isSystemLayout(); // Determined by source path
        info[JsonKeys::Type] = static_cast<int>(layout->type());

        // Include zone relative geometries for preview rendering
        QJsonArray zonesArray;
        for (const auto* zone : layout->zones()) {
            QJsonObject zoneInfo;
            zoneInfo[JsonKeys::ZoneNumber] = zone->zoneNumber();
            // Include relative geometry (0-1 normalized) for resolution-independent preview
            QJsonObject relGeo;
            relGeo[JsonKeys::X] = zone->relativeGeometry().x();
            relGeo[JsonKeys::Y] = zone->relativeGeometry().y();
            relGeo[JsonKeys::Width] = zone->relativeGeometry().width();
            relGeo[JsonKeys::Height] = zone->relativeGeometry().height();
            zoneInfo[JsonKeys::RelativeGeometry] = relGeo;
            zonesArray.append(zoneInfo);
        }
        info[JsonKeys::Zones] = zonesArray;

        result.append(QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Compact)));
    }
    return result;
}

QString LayoutAdaptor::getLayout(const QString& id)
{
    if (id.isEmpty()) {
        qCWarning(lcDbusLayout) << "Empty layout ID requested";
        return QString();
    }

    auto uuidOpt = Utils::parseUuid(id);
    if (!uuidOpt) {
        qCWarning(lcDbusLayout) << "Invalid UUID format:" << id;
        return QString();
    }
    QUuid uuid = *uuidOpt;

    // Check cache first
    if (m_cachedLayoutJson.contains(uuid)) {
        return m_cachedLayoutJson[uuid];
    }

    auto* layout = m_layoutManager->layoutById(uuid);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Layout not found:" << id;
        return QString();
    }

    // Serialize and cache
    QString json = QString::fromUtf8(QJsonDocument(layout->toJson()).toJson());
    m_cachedLayoutJson[uuid] = json;
    return json;
}

void LayoutAdaptor::setActiveLayout(const QString& id)
{
    if (id.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot set active layout - empty ID";
        return;
    }

    auto uuidOpt = Utils::parseUuid(id);
    if (!uuidOpt) {
        qCWarning(lcDbusLayout) << "Invalid UUID format for setActiveLayout:" << id;
        return;
    }

    auto* layout = m_layoutManager->layoutById(*uuidOpt);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Cannot set active layout - layout not found:" << id;
        return;
    }

    m_layoutManager->setActiveLayoutById(*uuidOpt);
}

void LayoutAdaptor::applyQuickLayout(int number, const QString& screenName)
{
    m_layoutManager->applyQuickLayout(number, screenName);
}

QString LayoutAdaptor::createLayout(const QString& name, const QString& type)
{
    if (name.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot create layout - empty name";
        return QString();
    }

    Layout* layout = nullptr;

    if (type == QStringLiteral("columns")) {
        layout = Layout::createColumnsLayout(3, m_layoutManager);
    } else if (type == QStringLiteral("rows")) {
        layout = Layout::createRowsLayout(3, m_layoutManager);
    } else if (type == QStringLiteral("grid")) {
        layout = Layout::createGridLayout(2, 2, m_layoutManager);
    } else if (type == QStringLiteral("priority")) {
        layout = Layout::createPriorityGridLayout(m_layoutManager);
    } else if (type == QStringLiteral("focus")) {
        layout = Layout::createFocusLayout(m_layoutManager);
    } else {
        // Custom layout with no zones
        layout = new Layout(name, LayoutType::Custom, m_layoutManager);
    }

    if (!layout) {
        qCWarning(lcDbusLayout) << "Failed to create layout of type:" << type;
        return QString();
    }

    layout->setName(name);
    // Note: New layouts have no sourcePath, making them user layouts (not system)
    m_layoutManager->addLayout(layout);
    m_layoutManager->saveLayouts();

    qCDebug(lcDbusLayout) << "Created layout" << name << "of type" << type;
    return layout->id().toString();
}

void LayoutAdaptor::deleteLayout(const QString& id)
{
    if (id.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot delete layout - empty ID";
        return;
    }

    auto uuidOpt = Utils::parseUuid(id);
    if (!uuidOpt) {
        qCWarning(lcDbusLayout) << "Invalid UUID format for deleteLayout:" << id;
        return;
    }
    QUuid uuid = *uuidOpt;

    auto* layout = m_layoutManager->layoutById(uuid);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Cannot delete layout - not found:" << id;
        return;
    }

    if (layout->isSystemLayout()) {
        qCWarning(lcDbusLayout) << "Cannot delete system layout:" << id;
        return;
    }

    m_layoutManager->removeLayoutById(uuid);
    // Remove from cache
    m_cachedLayoutJson.remove(uuid);
    if (m_cachedActiveLayoutId == uuid) {
        m_cachedActiveLayoutId = QUuid();
        m_cachedActiveLayoutJson.clear();
    }
    qCDebug(lcDbusLayout) << "Deleted layout" << id;
}

QString LayoutAdaptor::duplicateLayout(const QString& id)
{
    if (id.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot duplicate layout - empty ID";
        return QString();
    }

    auto uuidOpt = Utils::parseUuid(id);
    if (!uuidOpt) {
        qCWarning(lcDbusLayout) << "Invalid UUID format for duplicateLayout:" << id;
        return QString();
    }

    auto* source = m_layoutManager->layoutById(*uuidOpt);
    if (!source) {
        qCWarning(lcDbusLayout) << "Cannot duplicate layout - not found:" << id;
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

QString LayoutAdaptor::importLayout(const QString& filePath)
{
    if (filePath.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot import layout - empty file path";
        return QString();
    }

    // Store layout count before import to find the new one
    int layoutCountBefore = m_layoutManager->layouts().size();

    m_layoutManager->importLayout(filePath);

    // Find the newly imported layout (should be the last one added)
    const auto layouts = m_layoutManager->layouts();
    if (layouts.size() > layoutCountBefore) {
        // The new layout is the last one in the list
        Layout* newLayout = layouts.last();
        qCDebug(lcDbusLayout) << "Imported layout from" << filePath << "with ID" << newLayout->id();
        return newLayout->id().toString();
    }

    qCWarning(lcDbusLayout) << "Failed to import layout from" << filePath;
    return QString();
}

void LayoutAdaptor::exportLayout(const QString& layoutId, const QString& filePath)
{
    if (layoutId.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot export layout - empty layout ID";
        return;
    }

    if (filePath.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot export layout - empty file path";
        return;
    }

    auto uuidOpt = Utils::parseUuid(layoutId);
    if (!uuidOpt) {
        qCWarning(lcDbusLayout) << "Invalid UUID format for exportLayout:" << layoutId;
        return;
    }

    auto* layout = m_layoutManager->layoutById(*uuidOpt);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Cannot export layout - not found:" << layoutId;
        return;
    }

    m_layoutManager->exportLayout(layout, filePath);
    qCDebug(lcDbusLayout) << "Exported layout" << layoutId << "to" << filePath;
}

QString LayoutAdaptor::getLayoutForScreen(const QString& screenName)
{
    auto* layout = m_layoutManager->layoutForScreen(screenName);
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    if (screenName.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot assign layout - empty screen name";
        return;
    }

    if (layoutId.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot assign layout - empty layout ID";
        return;
    }

    auto uuidOpt = Utils::parseUuid(layoutId);
    if (!uuidOpt) {
        qCWarning(lcDbusLayout) << "Invalid UUID format for assignLayoutToScreen:" << layoutId;
        return;
    }

    auto* layout = m_layoutManager->layoutById(*uuidOpt);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Cannot assign layout - not found:" << layoutId;
        return;
    }

    m_layoutManager->assignLayoutById(screenName, 0, QString(), *uuidOpt);
    m_layoutManager->saveAssignments(); // Persist to disk
    qCDebug(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenName;
}

void LayoutAdaptor::clearAssignment(const QString& screenName)
{
    m_layoutManager->clearAssignment(screenName);
    m_layoutManager->saveAssignments(); // Persist to disk
}

bool LayoutAdaptor::updateLayout(const QString& layoutJson)
{
    if (layoutJson.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot update layout - empty JSON";
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcDbusLayout) << "Invalid layout JSON for update - parse error:" << parseError.errorString()
                                << "at offset" << parseError.offset;
        return false;
    }

    if (!doc.isObject()) {
        qCWarning(lcDbusLayout) << "Layout JSON is not an object";
        return false;
    }

    QJsonObject obj = doc.object();
    QString idStr = obj[JsonKeys::Id].toString();
    if (idStr.isEmpty()) {
        qCWarning(lcDbusLayout) << "Layout JSON missing ID";
        return false;
    }

    auto idOpt = Utils::parseUuid(idStr);
    if (!idOpt) {
        qCWarning(lcDbusLayout) << "Invalid UUID in layout JSON:" << idStr;
        return false;
    }
    QUuid id = *idOpt;

    auto* layout = m_layoutManager->layoutById(id);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Layout not found for update:" << id;
        return false;
    }

    // System layouts can be edited - changes will be saved to user directory
    // The layout's sourcePath will be updated when saved

    // Update basic properties
    layout->setName(obj[JsonKeys::Name].toString());

    // Update shader settings
    layout->setShaderId(obj[JsonKeys::ShaderId].toString());
    if (obj.contains(JsonKeys::ShaderParams)) {
        layout->setShaderParams(obj[JsonKeys::ShaderParams].toObject().toVariantMap());
    } else {
        layout->setShaderParams(QVariantMap());
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

        // Appearance - load ALL appearance properties, not just colors
        QJsonObject appearance = zoneObj[JsonKeys::Appearance].toObject();
        if (!appearance.isEmpty()) {
            zone->setHighlightColor(QColor(appearance[JsonKeys::HighlightColor].toString()));
            zone->setInactiveColor(QColor(appearance[JsonKeys::InactiveColor].toString()));
            zone->setBorderColor(QColor(appearance[JsonKeys::BorderColor].toString()));

            // Load optional appearance properties with defaults if missing
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
            // Load useCustomColors (was missing in an earlier version).
            if (appearance.contains(JsonKeys::UseCustomColors)) {
                zone->setUseCustomColors(appearance[JsonKeys::UseCustomColors].toBool());
            }
        }

        layout->addZone(zone);
    }

    m_layoutManager->saveLayouts();

    // Invalidate cache for this layout
    m_cachedLayoutJson.remove(id);
    if (m_cachedActiveLayoutId == id) {
        m_cachedActiveLayoutId = QUuid();
        m_cachedActiveLayoutJson.clear();
    }

    Q_EMIT layoutChanged(QString::fromUtf8(QJsonDocument(layout->toJson()).toJson()));

    return true;
}

QString LayoutAdaptor::createLayoutFromJson(const QString& layoutJson)
{
    if (layoutJson.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot create layout - empty JSON";
        return QString();
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcDbusLayout) << "Invalid layout JSON for creation - parse error:" << parseError.errorString()
                                << "at offset" << parseError.offset;
        return QString();
    }

    if (!doc.isObject()) {
        qCWarning(lcDbusLayout) << "Layout JSON is not an object";
        return QString();
    }

    auto* layout = Layout::fromJson(doc.object(), m_layoutManager);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Failed to create layout from JSON";
        return QString();
    }

    // Note: New layouts have no sourcePath, making them user layouts
    m_layoutManager->addLayout(layout);
    m_layoutManager->saveLayouts();

    qCDebug(lcDbusLayout) << "Created layout from JSON:" << layout->id();
    // Use default toString() format (with braces) for consistency with other D-Bus methods
    return layout->id().toString();
}

static QString findEditorExecutable()
{
    // First try system PATH
    QString editor = QStandardPaths::findExecutable(QStringLiteral("plasmazones-editor"));
    if (!editor.isEmpty()) {
        return editor;
    }

    // Try relative to the daemon executable
    QString appDir = QCoreApplication::applicationDirPath();
    QString localEditor = appDir + QStringLiteral("/plasmazones-editor");
    if (QFile::exists(localEditor)) {
        return localEditor;
    }

    // Fallback to hoping it's in PATH
    return QStringLiteral("plasmazones-editor");
}

void LayoutAdaptor::openEditor()
{
    QString editor = findEditorExecutable();
    qCDebug(lcDbusLayout) << "Launching editor:" << editor;
    if (!QProcess::startDetached(editor, QStringList())) {
        qCWarning(lcDbusLayout) << "Failed to launch editor:" << editor;
    }
}

void LayoutAdaptor::openEditorForScreen(const QString& screenName)
{
    QString editor = findEditorExecutable();
    qCDebug(lcDbusLayout) << "Launching editor for screen:" << screenName;
    if (!QProcess::startDetached(editor, QStringList{QStringLiteral("--screen"), screenName})) {
        qCWarning(lcDbusLayout) << "Failed to launch editor for screen:" << screenName;
    }
}

void LayoutAdaptor::openEditorForLayout(const QString& layoutId)
{
    QString editor = findEditorExecutable();
    qCDebug(lcDbusLayout) << "Launching editor for layout:" << layoutId;
    if (!QProcess::startDetached(editor, QStringList{QStringLiteral("--layout"), layoutId})) {
        qCWarning(lcDbusLayout) << "Failed to launch editor for layout:" << layoutId;
    }
}

QString LayoutAdaptor::getQuickLayoutSlot(int slotNumber)
{
    if (slotNumber < 1 || slotNumber > 9) {
        qCWarning(lcDbusLayout) << "Invalid quick layout slot number:" << slotNumber << "(must be 1-9)";
        return QString();
    }

    auto* layout = m_layoutManager->layoutForShortcut(slotNumber);
    if (layout) {
        return layout->id().toString();
    }
    return QString();
}

void LayoutAdaptor::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9) {
        qCWarning(lcDbusLayout) << "Invalid quick layout slot number:" << slotNumber << "(must be 1-9)";
        return;
    }

    QUuid uuid;
    if (!layoutId.isEmpty()) {
        auto uuidOpt = Utils::parseUuid(layoutId);
        if (!uuidOpt) {
            qCWarning(lcDbusLayout) << "Invalid UUID format:" << layoutId;
            return;
        }
        uuid = *uuidOpt;
    }

    m_layoutManager->setQuickLayoutSlot(slotNumber, uuid);
    qCDebug(lcDbusLayout) << "Set quick layout slot" << slotNumber << "to" << layoutId;

    // Notify listeners (e.g., KCM) that quick layout slots have changed
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

// Per-virtual-desktop screen assignments
QString LayoutAdaptor::getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop)
{
    auto* layout = m_layoutManager->layoutForScreen(screenName, virtualDesktop, QString());
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId)
{
    if (screenName.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot assign layout - empty screen name";
        return;
    }

    if (layoutId.isEmpty()) {
        qCWarning(lcDbusLayout) << "Cannot assign layout - empty layout ID";
        return;
    }

    auto uuidOpt = Utils::parseUuid(layoutId);
    if (!uuidOpt) {
        qCWarning(lcDbusLayout) << "Invalid UUID format for assignLayoutToScreenDesktop:" << layoutId;
        return;
    }

    auto* layout = m_layoutManager->layoutById(*uuidOpt);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Cannot assign layout - not found:" << layoutId;
        return;
    }

    m_layoutManager->assignLayoutById(screenName, virtualDesktop, QString(), *uuidOpt);
    m_layoutManager->saveAssignments(); // Persist to disk
    qCDebug(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenName << "on desktop"
                          << virtualDesktop;

    // Trigger active layout update if this affects the current desktop
    if (m_virtualDesktopManager) {
        int currentDesktop = m_virtualDesktopManager->currentDesktop();
        if (virtualDesktop == 0 || virtualDesktop == currentDesktop) {
            // Assignment affects current desktop - update active layout
            QMetaObject::invokeMethod(m_virtualDesktopManager, "updateActiveLayout", Qt::QueuedConnection);
        }
    }
}

void LayoutAdaptor::clearAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop)
{
    m_layoutManager->clearAssignment(screenName, virtualDesktop, QString());
    m_layoutManager->saveAssignments(); // Persist to disk
    qCDebug(lcDbusLayout) << "Cleared assignment for screen" << screenName << "on desktop" << virtualDesktop;
}

bool LayoutAdaptor::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop)
{
    return m_layoutManager->hasExplicitAssignment(screenName, virtualDesktop, QString());
}

int LayoutAdaptor::getVirtualDesktopCount()
{
    if (m_virtualDesktopManager) {
        return m_virtualDesktopManager->desktopCount();
    }
    // Graceful fallback if VirtualDesktopManager not available
    return 1;
}

QStringList LayoutAdaptor::getVirtualDesktopNames()
{
    if (m_virtualDesktopManager) {
        return m_virtualDesktopManager->desktopNames();
    }
    // Graceful fallback
    return {QStringLiteral("Desktop 1")};
}

QString LayoutAdaptor::getAllScreenAssignments()
{
    // Get assignments from the LayoutManager's internal storage
    // Cast to LayoutManager to access the assignments hash
    auto* layoutManager = qobject_cast<LayoutManager*>(m_layoutManager);
    if (!layoutManager) {
        qCWarning(lcDbusLayout) << "Cannot get screen assignments - LayoutManager cast failed";
        return QStringLiteral("{}");
    }

    QJsonObject root;

    // Build JSON with screen -> desktop -> layoutId structure
    // We need to iterate through all known screens and desktops
    const int desktopCount = getVirtualDesktopCount();

    // Get all screens from Qt (best effort since we don't have direct access)
    for (QScreen* screen : Utils::allScreens()) {
        QString screenName = screen->name();
        QJsonObject screenObj;

        // Check each specific desktop (0 = all desktops default, 1+ = specific desktops)
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

} // namespace PlasmaZones
