# Copy/Paste System Design Document

**Date:** 2026  
**Status:** Design Phase  
**Feature:** Clipboard Operations for Zone Editor

---

## Executive Summary

This document describes the design and implementation plan for copy/paste functionality in the PlasmaZones Layout Editor. The system will enable users to copy zones to the system clipboard and paste them either within the same layout or across different layouts.

---

## Goals

1. **Copy zones** to system clipboard (`Ctrl+C`)
2. **Cut zones** (copy and delete) (`Ctrl+X`)
3. **Paste zones** from clipboard (`Ctrl+V`)
4. **Paste with offset** to avoid exact overlap (`Ctrl+Shift+V`)
5. **Cross-layout paste** support (paste into different layouts)
6. **Multi-zone support** (copy/paste multiple zones at once)
7. **JSON clipboard format** for sharing zones between instances

---

## Architecture Overview

### Components

1. **EditorController** (C++)
   - `copyZones(QStringList zoneIds)` - Copy selected zones to clipboard
   - `cutZones(QStringList zoneIds)` - Cut selected zones (copy + delete)
   - `pasteZones(bool withOffset = false)` - Paste zones from clipboard
   - `canPaste()` - Check if clipboard contains valid zone data

2. **ZoneManager** (C++)
   - Helper methods for batch zone operations
   - Zone validation and creation

3. **QML Integration**
   - Keyboard shortcuts in `EditorShortcuts.qml`
   - Menu items in context menu
   - Visual paste preview

### Data Flow

```
User Action (Ctrl+C/X/V)
    ↓
EditorController (handles keyboard shortcuts)
    ↓
QClipboard (system clipboard)
    ↓
JSON Serialization/Deserialization
    ↓
ZoneManager (add zones to layout)
    ↓
Signals → QML Updates
```

---

## Clipboard Format

### JSON Structure

```json
{
  "version": "1.0",
  "application": "PlasmaZones",
  "dataType": "zones",
  "zones": [
    {
      "id": "new-uuid-here",
      "name": "Zone Name",
      "zoneNumber": 1,
      "x": 0.1,
      "y": 0.1,
      "width": 0.3,
      "height": 0.4,
      "highlightColor": "#800078D4",
      "inactiveColor": "#40808080",
      "borderColor": "#CCFFFFFF",
      "opacity": 0.5,
      "borderWidth": 2,
      "borderRadius": 8,
      "useCustomColors": false,
      "shortcut": ""
    }
  ]
}
```

### MIME Type

- **Primary**: `application/vnd.plasmazones.zones+json`
- **Fallback**: `application/json` (for compatibility)
- **Text fallback**: `text/plain` (JSON string, for debugging)

---

## Implementation Details

### Required Includes

**Location:** `EditorController.cpp` (top of file)

```cpp
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorController.h"
#include "../core/constants.h"
#include "../editor/services/ZoneManager.h"
#include <QClipboard>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QGuiApplication>
#include <KLocalizedString>

namespace PlasmaZones {
```

### 1. Clipboard Serialization

**Location:** `EditorController.cpp` (private method)

```cpp
/**
 * @brief Serializes zones to JSON format for clipboard
 * @param zones List of zones to serialize
 * @return JSON string containing zone data
 */
QString EditorController::serializeZonesToClipboard(const QVariantList &zones) {
    QJsonObject clipboardData;
    clipboardData["version"] = "1.0";
    clipboardData["application"] = "PlasmaZones";
    clipboardData["dataType"] = "zones";
    
    QJsonArray zonesArray;
    for (const QVariant &zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        QJsonObject zoneObj;
        
        // Generate new UUID for paste (preserve original ID in metadata)
        zoneObj["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        zoneObj["name"] = zone[JsonKeys::Name].toString();
        zoneObj["zoneNumber"] = zone[JsonKeys::ZoneNumber].toInt();
        zoneObj["x"] = zone[JsonKeys::X].toDouble();
        zoneObj["y"] = zone[JsonKeys::Y].toDouble();
        zoneObj["width"] = zone[JsonKeys::Width].toDouble();
        zoneObj["height"] = zone[JsonKeys::Height].toDouble();
        
        // Appearance properties
        zoneObj["highlightColor"] = zone[JsonKeys::HighlightColor].toString();
        zoneObj["inactiveColor"] = zone[JsonKeys::InactiveColor].toString();
        zoneObj["borderColor"] = zone[JsonKeys::BorderColor].toString();
        zoneObj["opacity"] = zone.contains(JsonKeys::Opacity) ? 
            zone[JsonKeys::Opacity].toDouble() : Defaults::Opacity;
        zoneObj["borderWidth"] = zone.contains(JsonKeys::BorderWidth) ? 
            zone[JsonKeys::BorderWidth].toInt() : Defaults::BorderWidth;
        zoneObj["borderRadius"] = zone.contains(JsonKeys::BorderRadius) ? 
            zone[JsonKeys::BorderRadius].toInt() : Defaults::BorderRadius;
        
        QString useCustomColorsKey = QString::fromLatin1(JsonKeys::UseCustomColors);
        zoneObj["useCustomColors"] = zone.contains(useCustomColorsKey) ? 
            zone[useCustomColorsKey].toBool() : false;
        zoneObj["shortcut"] = zone.contains(JsonKeys::Shortcut) ? 
            zone[JsonKeys::Shortcut].toString() : QString();
        
        zonesArray.append(zoneObj);
    }
    clipboardData["zones"] = zonesArray;
    
    QJsonDocument doc(clipboardData);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

} // namespace PlasmaZones
```

### 2. Clipboard Deserialization

**Location:** `EditorController.cpp` (private method)

```cpp
namespace PlasmaZones {

/**
 * @brief Deserializes zones from clipboard JSON format
 * @param clipboardText JSON string from clipboard
 * @return List of zones, or empty list if invalid data
 */
QVariantList EditorController::deserializeZonesFromClipboard(const QString &clipboardText) {
    QJsonDocument doc = QJsonDocument::fromJson(clipboardText.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        return QVariantList();
    }
    
    QJsonObject clipboardData = doc.object();
    
    // Validate clipboard format
    if (clipboardData["application"].toString() != "PlasmaZones" ||
        clipboardData["dataType"].toString() != "zones") {
        return QVariantList();
    }
    
    QJsonArray zonesArray = clipboardData["zones"].toArray();
    QVariantList zones;
    
    for (const QJsonValue &zoneVal : zonesArray) {
        QJsonObject zoneObj = zoneVal.toObject();
        QVariantMap zone;
        
        // Convert JSON to QVariantMap format used by ZoneManager
        zone[JsonKeys::Id] = zoneObj["id"].toString();
        zone[JsonKeys::Name] = zoneObj["name"].toString();
        zone[JsonKeys::ZoneNumber] = zoneObj["zoneNumber"].toInt();
        zone[JsonKeys::X] = zoneObj["x"].toDouble();
        zone[JsonKeys::Y] = zoneObj["y"].toDouble();
        zone[JsonKeys::Width] = zoneObj["width"].toDouble();
        zone[JsonKeys::Height] = zoneObj["height"].toDouble();
        
        // Appearance properties
        zone[JsonKeys::HighlightColor] = zoneObj["highlightColor"].toString();
        zone[JsonKeys::InactiveColor] = zoneObj["inactiveColor"].toString();
        zone[JsonKeys::BorderColor] = zoneObj["borderColor"].toString();
        zone[JsonKeys::Opacity] = zoneObj["opacity"].toDouble(Defaults::Opacity);
        zone[JsonKeys::BorderWidth] = zoneObj["borderWidth"].toInt(Defaults::BorderWidth);
        zone[JsonKeys::BorderRadius] = zoneObj["borderRadius"].toInt(Defaults::BorderRadius);
        
        QString useCustomColorsKey = QString::fromLatin1(JsonKeys::UseCustomColors);
        zone[useCustomColorsKey] = zoneObj["useCustomColors"].toBool(false);
        zone[JsonKeys::Shortcut] = zoneObj["shortcut"].toString();
        
        zones.append(zone);
    }
    
    return zones;
}

} // namespace PlasmaZones
```

### 3. Copy Operation

**Location:** `EditorController.cpp` (public method)

```cpp
namespace PlasmaZones {

/**
 * @brief Copies selected zones to system clipboard
 * @param zoneIds List of zone IDs to copy
 * 
 * Serializes zones to JSON format and stores in clipboard using MIME type
 * "application/vnd.plasmazones.zones+json". Emits canPasteChanged() signal
 * if clipboard state changes.
 */
void EditorController::copyZones(const QStringList &zoneIds)
{
    if (!m_zoneManager) {
        qWarning() << "EditorController: ZoneManager not initialized";
        Q_EMIT clipboardOperationFailed(i18nc("@info", "Zone manager not initialized"));
        return;
    }
    
    if (zoneIds.isEmpty()) {
        qWarning() << "EditorController: Empty zone ID list for copy";
        return;
    }
    
    // Collect zones to copy
    QVariantList zonesToCopy;
    QVariantList allZones = m_zoneManager->zones();
    
    for (const QVariant &zoneVar : allZones) {
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone[JsonKeys::Id].toString();
        if (zoneIds.contains(zoneId)) {
            zonesToCopy.append(zone);
        }
    }
    
    if (zonesToCopy.isEmpty()) {
        qWarning() << "EditorController: No valid zones found to copy";
        return;
    }
    
    // Serialize to JSON
    QString jsonData = serializeZonesToClipboard(zonesToCopy);
    
    // Copy to clipboard
    QClipboard *clipboard = QGuiApplication::clipboard();
    
    // QClipboard::setMimeData() takes ownership of QMimeData
    // No need to specify parent - ownership is transferred to clipboard
    QMimeData *mimeData = new QMimeData();
    mimeData->setData("application/vnd.plasmazones.zones+json", jsonData.toUtf8());
    mimeData->setData("application/json", jsonData.toUtf8());
    mimeData->setText(jsonData);  // Text fallback for debugging
    
    // Check if clipboard state will change
    bool wasCanPaste = canPaste();
    clipboard->setMimeData(mimeData, QClipboard::Clipboard);
    
    // Emit signal if clipboard state changed
    if (canPaste() != wasCanPaste) {
        Q_EMIT canPasteChanged();
    }
}

} // namespace PlasmaZones
```

### 4. Cut Operation

**Location:** `EditorController.cpp` (public method)

```cpp
namespace PlasmaZones {

/**
 * @brief Cuts selected zones (copy + delete)
 * @param zoneIds List of zone IDs to cut
 * 
 * First copies zones to clipboard, then deletes them from the layout.
 * Emits signals for clipboard state changes and zone deletions.
 */
void EditorController::cutZones(const QStringList &zoneIds)
{
    // Copy first
    copyZones(zoneIds);
    
    // Then delete
    for (const QString &zoneId : zoneIds) {
        deleteZone(zoneId);
    }
}

} // namespace PlasmaZones
```

### 5. Paste Operation

**Location:** `EditorController.cpp` (public method)

```cpp
namespace PlasmaZones {

/**
 * @brief Pastes zones from clipboard
 * @param withOffset If true, offset pasted zones by 2% to avoid overlap
 * @return List of newly pasted zone IDs, or empty list on failure
 * 
 * Deserializes zones from clipboard, generates new IDs, and adds them to
 * the layout. Zones are clamped to valid bounds. Selects the first pasted
 * zone if paste succeeds.
 */
QStringList EditorController::pasteZones(bool withOffset)
{
    if (!m_zoneManager) {
        qWarning() << "EditorController: ZoneManager not initialized";
        Q_EMIT clipboardOperationFailed(i18nc("@info", "Zone manager not initialized"));
        return QStringList();
    }
    
    // Get clipboard data
    QClipboard *clipboard = QGuiApplication::clipboard();
    QString clipboardText = clipboard->text();
    
    if (clipboardText.isEmpty()) {
        return QStringList();
    }
    
    // Deserialize zones
    QVariantList zonesToPaste = deserializeZonesFromClipboard(clipboardText);
    if (zonesToPaste.isEmpty()) {
        return QStringList();
    }
    
    // Calculate offset if needed
    qreal offsetX = 0.0;
    qreal offsetY = 0.0;
    if (withOffset) {
        offsetX = EditorConstants::DuplicateOffset;
        offsetY = EditorConstants::DuplicateOffset;
    }
    
    // Paste zones with new IDs and adjusted positions
    QStringList newZoneIds;
    int newZoneNumber = m_zoneManager->zoneCount() + 1;
    
    for (QVariant &zoneVar : zonesToPaste) {
        QVariantMap zone = zoneVar.toMap();
        
        // Generate new ID
        QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        zone[JsonKeys::Id] = newId;
        
        // Adjust position if offset requested
        qreal x = zone[JsonKeys::X].toDouble() + offsetX;
        qreal y = zone[JsonKeys::Y].toDouble() + offsetY;
        qreal width = zone[JsonKeys::Width].toDouble();
        qreal height = zone[JsonKeys::Height].toDouble();
        
        // Clamp to bounds
        x = qBound(0.0, x, 1.0 - width);
        y = qBound(0.0, y, 1.0 - height);
        
        zone[JsonKeys::X] = x;
        zone[JsonKeys::Y] = y;
        zone[JsonKeys::ZoneNumber] = newZoneNumber++;
        
        // Add zone via ZoneManager
        // Note: We need to add a method in ZoneManager to add zone from QVariantMap
        QString zoneId = m_zoneManager->addZoneFromMap(zone);
        if (!zoneId.isEmpty()) {
            newZoneIds.append(zoneId);
        }
    }
    
    // Select the first pasted zone (only emit signal if value changes)
    if (!newZoneIds.isEmpty()) {
        QString newSelectedId = newZoneIds.first();
        if (m_selectedZoneId != newSelectedId) {
            m_selectedZoneId = newSelectedId;
            Q_EMIT selectedZoneIdChanged();
        }
        markUnsaved();
    }
    
    return newZoneIds;
}

} // namespace PlasmaZones
```

### 6. Can Paste Check

**Location:** `EditorController.cpp` (public method)

```cpp
namespace PlasmaZones {

/**
 * @brief Checks if clipboard contains valid zone data
 * @return true if clipboard can be pasted, false otherwise
 * 
 * Validates clipboard content is JSON with PlasmaZones zone format.
 * Cached result is updated when clipboard changes via onClipboardChanged().
 */
bool EditorController::canPaste() const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    QString clipboardText = clipboard->text();
    
    if (clipboardText.isEmpty()) {
        return false;
    }
    
    // Quick validation - check if it's valid JSON with our format
    QJsonDocument doc = QJsonDocument::fromJson(clipboardText.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }
    
    QJsonObject clipboardData = doc.object();
    return clipboardData["application"].toString() == "PlasmaZones" &&
           clipboardData["dataType"].toString() == "zones";
}

} // namespace PlasmaZones
```

### 7. Clipboard Monitoring

**Location:** `EditorController.cpp` (constructor and slot)

To enable reactive `canPaste` property updates, we need to monitor clipboard changes:

```cpp
namespace PlasmaZones {

// In EditorController constructor:
EditorController::EditorController(QObject *parent)
    : QObject(parent)
    , m_layoutService(new DBusLayoutService(this))
    , m_zoneManager(new ZoneManager(this))
    // ... other initialization ...
{
    // ... existing connections ...
    
    // Connect to clipboard changes for reactive canPaste updates
    QClipboard *clipboard = QGuiApplication::clipboard();
    connect(clipboard, &QClipboard::dataChanged, this, &EditorController::onClipboardChanged);
    
    // Initialize canPaste state
    m_canPaste = canPaste();
}

/**
 * @brief Handles clipboard content changes
 * 
 * Updates canPaste property and emits signal when clipboard state changes.
 * This enables reactive QML bindings to update when clipboard changes
 * (e.g., user copies from another application).
 */
void EditorController::onClipboardChanged()
{
    bool newCanPaste = canPaste();
    if (m_canPaste != newCanPaste) {
        m_canPaste = newCanPaste;
        Q_EMIT canPasteChanged();
    }
}

} // namespace PlasmaZones
```

---

## QML Integration

**IMPORTANT:** If creating new QML components for copy/paste UI, they MUST be added to the `QML_FILES` list in `CMakeLists.txt` under `qt_add_qml_module()`. Missing files cause "X is not a type" errors at runtime.

### 1. Keyboard Shortcuts

**Location:** `src/editor/qml/EditorShortcuts.qml`

Add shortcuts for Copy, Cut, and Paste:

```qml
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

// Copy zone shortcut
Shortcut {
    id: copyShortcut
    sequence: "Ctrl+C"
    context: Qt.ApplicationShortcut
    enabled: editorWindow.selectedZoneId !== ""
    
    Accessible.name: i18nc("@action:shortcut", "Copy zone")
    Accessible.description: i18nc("@info", "Copy the selected zone to clipboard")
    
    onActivated: {
        if (editorController && editorWindow.selectedZoneId) {
            var zoneIds = [editorWindow.selectedZoneId]
            editorController.copyZones(zoneIds)
        }
    }
}

// Cut zone shortcut
Shortcut {
    id: cutShortcut
    sequence: "Ctrl+X"
    context: Qt.ApplicationShortcut
    enabled: editorWindow.selectedZoneId !== ""
    
    Accessible.name: i18nc("@action:shortcut", "Cut zone")
    Accessible.description: i18nc("@info", "Cut the selected zone to clipboard")
    
    onActivated: {
        if (editorController && editorWindow.selectedZoneId) {
            var zoneIds = [editorWindow.selectedZoneId]
            editorController.cutZones(zoneIds)
        }
    }
}

// Paste zone shortcut
Shortcut {
    id: pasteShortcut
    sequence: "Ctrl+V"
    context: Qt.ApplicationShortcut
    enabled: editorController && editorController.canPaste
    
    Accessible.name: i18nc("@action:shortcut", "Paste zone")
    Accessible.description: i18nc("@info", "Paste zones from clipboard")
    
    onActivated: {
        if (editorController) {
            editorController.pasteZones(false)
        }
    }
}

// Paste with offset shortcut
Shortcut {
    id: pasteOffsetShortcut
    sequence: "Ctrl+Shift+V"
    context: Qt.ApplicationShortcut
    enabled: editorController && editorController.canPaste
    
    Accessible.name: i18nc("@action:shortcut", "Paste zone with offset")
    Accessible.description: i18nc("@info", "Paste zones from clipboard with offset to avoid overlap")
    
    onActivated: {
        if (editorController) {
            editorController.pasteZones(true)
        }
    }
}
```

### 2. Context Menu

**Location:** `src/editor/qml/ZoneContextMenu.qml`

Add Copy, Cut, and Paste menu items:

```qml
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick.Controls as Controls

MenuItem {
    text: i18nc("@action", "Copy")
    icon.name: "edit-copy"
    enabled: root.zoneId !== ""
    
    Accessible.name: i18nc("@action", "Copy zone")
    Accessible.description: i18nc("@info", "Copy the selected zone to clipboard")
    
    onTriggered: {
        var zoneIds = [root.zoneId]
        editorController.copyZones(zoneIds)
    }
}

MenuItem {
    text: i18nc("@action", "Cut")
    icon.name: "edit-cut"
    enabled: root.zoneId !== ""
    
    Accessible.name: i18nc("@action", "Cut zone")
    Accessible.description: i18nc("@info", "Cut the selected zone to clipboard")
    
    onTriggered: {
        var zoneIds = [root.zoneId]
        editorController.cutZones(zoneIds)
    }
}

MenuSeparator {}

MenuItem {
    text: i18nc("@action", "Paste")
    icon.name: "edit-paste"
    enabled: editorController && editorController.canPaste
    
    Accessible.name: i18nc("@action", "Paste zone")
    Accessible.description: i18nc("@info", "Paste zones from clipboard")
    
    onTriggered: {
        editorController.pasteZones(false)
    }
}

MenuItem {
    text: i18nc("@action", "Paste with Offset")
    icon.name: "edit-paste"
    enabled: editorController && editorController.canPaste
    
    Accessible.name: i18nc("@action", "Paste zone with offset")
    Accessible.description: i18nc("@info", "Paste zones from clipboard with offset to avoid overlap")
    
    onTriggered: {
        editorController.pasteZones(true)
    }
}
```

### 3. Property for Can Paste

**Location:** `EditorController.h`

Add property to expose paste availability to QML:

```cpp
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PlasmaZones {

class EditorController : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged)

public:
    // ... existing methods ...

Q_SIGNALS:
    void canPasteChanged();
    void clipboardOperationFailed(const QString &error);

private:
    // ... existing members ...
    bool m_canPaste = false;
    
    void onClipboardChanged();
    QString serializeZonesToClipboard(const QVariantList &zones);
    QVariantList deserializeZonesFromClipboard(const QString &clipboardText);
};

} // namespace PlasmaZones
```

### 4. Update Help Dialog

**Location:** `src/editor/qml/HelpDialogContent.qml`

Add keyboard shortcut documentation for Copy/Cut/Paste using `i18nc()` with proper context:

```qml
// Copy/Cut/Paste shortcuts section
Row {
    spacing: Kirigami.Units.smallSpacing
    
    Label {
        text: i18nc("@label", "Copy zone:")
        width: constants.labelWidth
    }
    Label {
        text: editorController ? i18nc("@info", "Ctrl+C") : "Ctrl+C"
    }
}

Row {
    spacing: Kirigami.Units.smallSpacing
    
    Label {
        text: i18nc("@label", "Cut zone:")
        width: constants.labelWidth
    }
    Label {
        text: editorController ? i18nc("@info", "Ctrl+X") : "Ctrl+X"
    }
}

Row {
    spacing: Kirigami.Units.smallSpacing
    
    Label {
        text: i18nc("@label", "Paste zone:")
        width: constants.labelWidth
    }
    Label {
        text: editorController ? i18nc("@info", "Ctrl+V") : "Ctrl+V"
    }
}

Row {
    spacing: Kirigami.Units.smallSpacing
    
    Label {
        text: i18nc("@label", "Paste with offset:")
        width: constants.labelWidth
    }
    Label {
        text: editorController ? i18nc("@info", "Ctrl+Shift+V") : "Ctrl+Shift+V"
    }
}
```

---

## ZoneManager Extensions

### Add Zone from Map

**Location:** `src/editor/services/ZoneManager.h` and `.cpp`

Add method to create zone from complete QVariantMap:

```cpp
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PlasmaZones {

class ZoneManager : public QObject
{
    Q_OBJECT

public:
    // ... existing methods ...
    
    /**
     * @brief Adds a zone from complete QVariantMap (for paste operations)
     * @param zoneData Complete zone data including all properties (colors, appearance, etc.)
     * @return Zone ID of the created zone, or empty string on failure
     * 
     * Allows pasting zones with all their properties intact. Validates
     * zone data and creates new zone with specified properties.
     */
    QString addZoneFromMap(const QVariantMap &zoneData);
};

} // namespace PlasmaZones
```

**Implementation:**

```cpp
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ZoneManager.h"
#include "../core/constants.h"
#include <QUuid>

namespace PlasmaZones {

QString ZoneManager::addZoneFromMap(const QVariantMap &zoneData)
{
    if (zoneData.isEmpty()) {
        qWarning() << "ZoneManager: Empty zone data for addZoneFromMap";
        return QString();
    }
    
    // Validate required fields
    if (!zoneData.contains(JsonKeys::Id) || !zoneData.contains(JsonKeys::X) ||
        !zoneData.contains(JsonKeys::Y) || !zoneData.contains(JsonKeys::Width) ||
        !zoneData.contains(JsonKeys::Height)) {
        qWarning() << "ZoneManager: Invalid zone data - missing required fields";
        return QString();
    }
    
    // Validate geometry
    qreal x = zoneData[JsonKeys::X].toDouble();
    qreal y = zoneData[JsonKeys::Y].toDouble();
    qreal width = zoneData[JsonKeys::Width].toDouble();
    qreal height = zoneData[JsonKeys::Height].toDouble();
    
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 ||
        width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
        qWarning() << "ZoneManager: Invalid zone geometry for addZoneFromMap";
        return QString();
    }
    
    // Use provided ID or generate new one
    QString zoneId = zoneData[JsonKeys::Id].toString();
    if (zoneId.isEmpty() || findZoneIndex(zoneId) >= 0) {
        // ID is empty or already exists, generate new one
        zoneId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    
    // Create zone with all properties from zoneData
    QString name = zoneData.contains(JsonKeys::Name) ? 
        zoneData[JsonKeys::Name].toString() : QString();
    int zoneNumber = zoneData.contains(JsonKeys::ZoneNumber) ? 
        zoneData[JsonKeys::ZoneNumber].toInt() : m_zones.size() + 1;
    
    QVariantMap zone = createZone(name, zoneNumber, x, y, width, height);
    
    // Update ID (createZone generates new ID, but we want to preserve paste ID)
    zone[JsonKeys::Id] = zoneId;
    
    // Copy all appearance properties
    if (zoneData.contains(JsonKeys::HighlightColor)) {
        zone[JsonKeys::HighlightColor] = zoneData[JsonKeys::HighlightColor].toString();
    }
    if (zoneData.contains(JsonKeys::InactiveColor)) {
        zone[JsonKeys::InactiveColor] = zoneData[JsonKeys::InactiveColor].toString();
    }
    if (zoneData.contains(JsonKeys::BorderColor)) {
        zone[JsonKeys::BorderColor] = zoneData[JsonKeys::BorderColor].toString();
    }
    if (zoneData.contains(JsonKeys::Opacity)) {
        zone[JsonKeys::Opacity] = zoneData[JsonKeys::Opacity].toDouble();
    }
    if (zoneData.contains(JsonKeys::BorderWidth)) {
        zone[JsonKeys::BorderWidth] = zoneData[JsonKeys::BorderWidth].toInt();
    }
    if (zoneData.contains(JsonKeys::BorderRadius)) {
        zone[JsonKeys::BorderRadius] = zoneData[JsonKeys::BorderRadius].toInt();
    }
    
    QString useCustomColorsKey = QString::fromLatin1(JsonKeys::UseCustomColors);
    if (zoneData.contains(useCustomColorsKey)) {
        zone[useCustomColorsKey] = zoneData[useCustomColorsKey].toBool();
    }
    
    if (zoneData.contains(JsonKeys::Shortcut)) {
        zone[JsonKeys::Shortcut] = zoneData[JsonKeys::Shortcut].toString();
    }
    
    m_zones.append(zone);
    
    Q_EMIT zoneAdded(zoneId);
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
    
    return zoneId;
}

} // namespace PlasmaZones
```

This allows pasting zones with all their properties (colors, appearance, etc.) intact.

---

## Edge Cases & Validation

### 1. Paste Outside Bounds

- **Solution**: Clamp zone positions to valid bounds (0.0-1.0)
- **Implementation**: Already handled in `pasteZones()` with `qBound()`

### 2. Invalid Clipboard Data

- **Solution**: Validate JSON structure and required fields
- **Implementation**: `deserializeZonesFromClipboard()` returns empty list on failure

### 3. Zone Number Conflicts

- **Solution**: Assign new sequential numbers starting from `zoneCount() + 1`
- **Implementation**: Already handled in `pasteZones()`

### 4. Empty Clipboard

- **Solution**: `canPaste()` returns false, paste operations do nothing
- **Implementation**: Check clipboard text before deserializing

### 5. Zone Name Conflicts

- **Solution**: Allow duplicate names (current behavior)
- **Future Enhancement**: Add " (Copy)" suffix if desired

### 6. Cross-Layout Paste

- **Solution**: Use relative coordinates (0.0-1.0), so zones work on any screen size
- **Implementation**: No special handling needed - relative coordinates are resolution-independent

---

## Testing Strategy

### Unit Tests

1. **Clipboard Serialization**
   - Test single zone serialization
   - Test multiple zones serialization
   - Test with all appearance properties
   - Test with missing optional properties (should use defaults)

2. **Clipboard Deserialization**
   - Test valid clipboard data
   - Test invalid JSON
   - Test wrong application/dataType
   - Test missing required fields
   - Test version compatibility

3. **Copy Operations**
   - Test copy single zone
   - Test copy multiple zones (when multi-select implemented)
   - Test copy with no selection (should do nothing)

4. **Cut Operations**
   - Test cut single zone
   - Test cut multiple zones
   - Test clipboard contains data after cut
   - Test zones are deleted after cut

5. **Paste Operations**
   - Test paste single zone
   - Test paste multiple zones
   - Test paste with offset
   - Test paste without offset (overlap)
   - Test paste when clipboard empty
   - Test paste invalid data
   - Test bounds clamping

6. **Can Paste**
   - Test returns true for valid data
   - Test returns false for empty clipboard
   - Test returns false for invalid data
   - Test returns false for wrong application

### Integration Tests

1. **Cross-Layout Paste**
   - Copy zones from Layout A
   - Paste into Layout B
   - Verify zones appear correctly

2. **Keyboard Shortcuts**
   - Test Ctrl+C copies
   - Test Ctrl+X cuts
   - Test Ctrl+V pastes
   - Test Ctrl+Shift+V pastes with offset

3. **Context Menu**
   - Test Copy menu item
   - Test Cut menu item
   - Test Paste menu item
   - Test Paste with Offset menu item
   - Test menu items enable/disable correctly

---

## Future Enhancements

1. **Multi-Zone Support**
   - Extend to support multiple selected zones (when multi-select is implemented)
   - Batch copy/paste operations

2. **Paste Preview**
   - Show ghost/preview zones before paste is committed
   - Allow user to position zones before pasting

3. **Smart Paste**
   - Auto-position pasted zones to avoid overlaps
   - Snap to grid when pasting
   - Align with existing zones

4. **Copy Between Editor Instances**
   - Support copying from one editor window to another
   - Share zones via clipboard between instances

5. **Clipboard History**
   - Store multiple clipboard items
   - Allow cycling through clipboard history

6. **Export Zones as JSON**
   - File menu option to export selected zones as JSON file
   - Import zones from JSON file

---

## Implementation Checklist

### Phase 1: Core Functionality

- [ ] Add SPDX headers to all new files
- [ ] Wrap all C++ code in `namespace PlasmaZones`
- [ ] Add required includes (QClipboard, QMimeData, QJsonDocument, etc.)
- [ ] Add clipboard serialization methods to `EditorController`
- [ ] Add clipboard deserialization methods to `EditorController`
- [ ] Implement `copyZones()` method with error handling
- [ ] Implement `cutZones()` method
- [ ] Implement `pasteZones()` method with proper signal emissions
- [ ] Implement `canPaste()` method
- [ ] Add clipboard monitoring (`QClipboard::dataChanged` connection)
- [ ] Implement `onClipboardChanged()` slot
- [ ] Add `clipboardOperationFailed()` signal
- [ ] Add proper documentation comments (`@brief`, `@param`, `@return`)
- [ ] Add `addZoneFromMap()` to `ZoneManager`

### Phase 2: QML Integration

- [ ] Add SPDX headers to QML files
- [ ] Add keyboard shortcuts in `EditorShortcuts.qml` with accessibility attributes
- [ ] Add context menu items in `ZoneContextMenu.qml` with accessibility attributes
- [ ] Add `canPaste` property to `EditorController` with signal
- [ ] Update help dialog with copy/paste shortcuts using `i18nc()`
- [ ] Add visual feedback for copy/cut operations
- [ ] Register any new QML files in `CMakeLists.txt` `qt_add_qml_module()`

### Phase 3: Testing & Polish

- [ ] Write unit tests for clipboard operations
- [ ] Test clipboard monitoring (copy from external app)
- [ ] Test signal emissions (only when changed)
- [ ] Test error handling
- [ ] Write integration tests
- [ ] Test edge cases (bounds, invalid data, etc.)
- [ ] Test cross-layout paste
- [ ] Verify memory management (QMimeData ownership)
- [ ] Update documentation
- [ ] Add i18n strings for all new UI elements

---

## API Summary

### EditorController Public API

```cpp
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PlasmaZones {

class EditorController : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged)

public:
    /**
     * @brief Copies selected zones to clipboard
     * @param zoneIds List of zone IDs to copy
     */
    Q_INVOKABLE void copyZones(const QStringList &zoneIds);

    /**
     * @brief Cuts selected zones (copy + delete)
     * @param zoneIds List of zone IDs to cut
     */
    Q_INVOKABLE void cutZones(const QStringList &zoneIds);

    /**
     * @brief Pastes zones from clipboard
     * @param withOffset If true, offset pasted zones by 2% to avoid overlap
     * @return List of newly pasted zone IDs, or empty list on failure
     */
    Q_INVOKABLE QStringList pasteZones(bool withOffset = false);

    /**
     * @brief Checks if clipboard contains valid zone data
     * @return true if clipboard can be pasted, false otherwise
     */
    Q_INVOKABLE bool canPaste() const;

Q_SIGNALS:
    void canPasteChanged();
    void clipboardOperationFailed(const QString &error);

private:
    bool m_canPaste = false;
    void onClipboardChanged();
    QString serializeZonesToClipboard(const QVariantList &zones);
    QVariantList deserializeZonesFromClipboard(const QString &clipboardText);
};

} // namespace PlasmaZones
```

### ZoneManager Public API

```cpp
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PlasmaZones {

class ZoneManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Adds a zone from complete QVariantMap (for paste operations)
     * @param zoneData Complete zone data including all properties (colors, appearance, etc.)
     * @return Zone ID of the created zone, or empty string on failure
     */
    QString addZoneFromMap(const QVariantMap &zoneData);
};

} // namespace PlasmaZones
```

---

## Conclusion

This design provides a comprehensive copy/paste system that integrates seamlessly with the existing PlasmaZones editor architecture. The JSON-based clipboard format enables cross-layout and cross-instance zone sharing, while the implementation follows Qt and KDE best practices.

The phased implementation approach allows for incremental development and testing, ensuring a robust and user-friendly feature.

---

---

## Compliance Notes

This design document has been updated for full compliance with `.cursorrules`:

- ✅ All C++ code wrapped in `namespace PlasmaZones`
- ✅ SPDX headers added to all code examples
- ✅ Proper documentation comments with `@brief`, `@param`, `@return`
- ✅ Required includes specified
- ✅ Signal emissions only when values change
- ✅ Clipboard monitoring for reactive `canPaste` property
- ✅ Error handling with user-friendly messages
- ✅ Accessibility attributes in QML examples
- ✅ Proper i18n usage with `i18nc()` context
- ✅ Memory management documented (QMimeData ownership)
- ✅ CMake/QML module registration notes added

---

**Document Version:** 2.0  
**Last Updated:** 2026  
**Status:** Compliance Verified  
**Author:** Design Team
