// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../layoutadaptor.h"
#include "../../core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include "../../core/layoutfactory.h"
#include <PhosphorZones/Zone.h>
#include "../../core/constants.h"
#include <PhosphorZones/LayoutUtils.h>
#include "../../core/layoutmanager.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QScopeGuard>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Editor Launch Helper
// ═══════════════════════════════════════════════════════════════════════════════

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

    qCInfo(lcDbusLayout) << "Launching editor" << description;
    if (!QProcess::startDetached(editor, args)) {
        qCWarning(lcDbusLayout) << "Failed to launch editor" << description;
    }
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
        PhosphorZones::Layout* newLayout = layouts.last();
        qCInfo(lcDbusLayout) << "Imported layout from" << filePath << "with ID" << newLayout->id();
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
    qCInfo(lcDbusLayout) << "Exported layout" << layoutId << "to" << filePath;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Layout Update (Editor Support)
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
    QString idStr = obj[::PhosphorZones::ZoneJsonKeys::Id].toString();

    // Handle autotile layout settings updates (gaps, visibility, shader only)
    if (LayoutId::isAutotile(idStr)) {
        QString algoId = LayoutId::extractAlgorithmId(idStr);
        QJsonObject overrides;
        if (obj.contains(::PhosphorZones::ZoneJsonKeys::ZonePadding))
            overrides[::PhosphorZones::ZoneJsonKeys::ZonePadding] = obj[::PhosphorZones::ZoneJsonKeys::ZonePadding];
        if (obj.contains(::PhosphorZones::ZoneJsonKeys::OuterGap))
            overrides[::PhosphorZones::ZoneJsonKeys::OuterGap] = obj[::PhosphorZones::ZoneJsonKeys::OuterGap];
        if (obj.contains(::PhosphorZones::ZoneJsonKeys::AllowedScreens))
            overrides[::PhosphorZones::ZoneJsonKeys::AllowedScreens] =
                obj[::PhosphorZones::ZoneJsonKeys::AllowedScreens];
        if (obj.contains(::PhosphorZones::ZoneJsonKeys::AllowedDesktops))
            overrides[::PhosphorZones::ZoneJsonKeys::AllowedDesktops] =
                obj[::PhosphorZones::ZoneJsonKeys::AllowedDesktops];
        if (obj.contains(::PhosphorZones::ZoneJsonKeys::AllowedActivities))
            overrides[::PhosphorZones::ZoneJsonKeys::AllowedActivities] =
                obj[::PhosphorZones::ZoneJsonKeys::AllowedActivities];
        if (obj.contains(::PhosphorZones::ZoneJsonKeys::ShaderId))
            overrides[::PhosphorZones::ZoneJsonKeys::ShaderId] = obj[::PhosphorZones::ZoneJsonKeys::ShaderId];
        if (obj.contains(::PhosphorZones::ZoneJsonKeys::ShaderParams))
            overrides[::PhosphorZones::ZoneJsonKeys::ShaderParams] = obj[::PhosphorZones::ZoneJsonKeys::ShaderParams];
        if (obj.contains(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode))
            overrides[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode] =
                obj[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode];
        m_layoutManager->saveAutotileOverrides(algoId, overrides);
        qCInfo(lcDbusLayout) << "Saved autotile overrides for algorithm:" << algoId;
        // Autotile override save mutates a single autotile preview entry, not
        // the layout list itself. Emit layoutChanged only; layoutListChanged
        // stays reserved for add/delete operations. SettingsController wires
        // both signals to the same reload slot so no visible behavior changes.
        Q_EMIT layoutChanged(layoutJson);
        return true;
    }

    auto* layout = getValidatedLayout(idStr, QStringLiteral("update layout"));
    if (!layout) {
        return false;
    }
    QUuid layoutId = layout->id();

    layout->beginBatchModify();
    auto batchGuard = qScopeGuard([layout]() {
        layout->endBatchModify();
    });

    // Update basic properties
    layout->setName(obj[::PhosphorZones::ZoneJsonKeys::Name].toString());

    // Update per-layout gap overrides (-1 = use global setting)
    if (obj.contains(::PhosphorZones::ZoneJsonKeys::ZonePadding)) {
        layout->setZonePadding(obj[::PhosphorZones::ZoneJsonKeys::ZonePadding].toInt(-1));
    } else {
        layout->clearZonePaddingOverride();
    }
    // Set each gap field explicitly — avoid clearOuterGapOverride() which clobbers
    // per-side state before per-side keys are processed
    layout->setOuterGap(obj.contains(::PhosphorZones::ZoneJsonKeys::OuterGap)
                            ? obj[::PhosphorZones::ZoneJsonKeys::OuterGap].toInt(-1)
                            : -1);
    layout->setUsePerSideOuterGap(obj[::PhosphorZones::ZoneJsonKeys::UsePerSideOuterGap].toBool(false));
    layout->setOuterGapTop(obj.contains(::PhosphorZones::ZoneJsonKeys::OuterGapTop)
                               ? obj[::PhosphorZones::ZoneJsonKeys::OuterGapTop].toInt(-1)
                               : -1);
    layout->setOuterGapBottom(obj.contains(::PhosphorZones::ZoneJsonKeys::OuterGapBottom)
                                  ? obj[::PhosphorZones::ZoneJsonKeys::OuterGapBottom].toInt(-1)
                                  : -1);
    layout->setOuterGapLeft(obj.contains(::PhosphorZones::ZoneJsonKeys::OuterGapLeft)
                                ? obj[::PhosphorZones::ZoneJsonKeys::OuterGapLeft].toInt(-1)
                                : -1);
    layout->setOuterGapRight(obj.contains(::PhosphorZones::ZoneJsonKeys::OuterGapRight)
                                 ? obj[::PhosphorZones::ZoneJsonKeys::OuterGapRight].toInt(-1)
                                 : -1);

    // Update per-layout overlay display mode override
    if (obj.contains(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode)) {
        layout->setOverlayDisplayMode(obj[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode].toInt(-1));
    } else {
        layout->clearOverlayDisplayModeOverride();
    }

    // Update full screen geometry mode
    layout->setUseFullScreenGeometry(obj[::PhosphorZones::ZoneJsonKeys::UseFullScreenGeometry].toBool(false));

    // Update aspect ratio classification
    if (obj.contains(::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey)) {
        layout->setAspectRatioClassInt(obj[::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey].toInt(0));
    } else {
        layout->setAspectRatioClassInt(0);
    }

    // Update shader settings
    layout->setShaderId(obj[::PhosphorZones::ZoneJsonKeys::ShaderId].toString());
    if (obj.contains(::PhosphorZones::ZoneJsonKeys::ShaderParams)) {
        layout->setShaderParams(obj[::PhosphorZones::ZoneJsonKeys::ShaderParams].toObject().toVariantMap());
    } else {
        layout->setShaderParams(QVariantMap());
    }

    // Update visibility allow-lists
    {
        QStringList screens;
        QList<int> desktops;
        QStringList activities;
        PhosphorZones::LayoutUtils::deserializeAllowLists(obj, screens, desktops, activities);
        layout->setAllowedScreens(screens);
        layout->setAllowedDesktops(desktops);
        layout->setAllowedActivities(activities);
    }

    // Update app-to-zone rules
    if (obj.contains(::PhosphorZones::ZoneJsonKeys::AppRules)) {
        layout->setAppRules(
            PhosphorZones::AppRule::fromJsonArray(obj[::PhosphorZones::ZoneJsonKeys::AppRules].toArray()));
    }

    // Clear existing zones and add new ones
    layout->clearZones();

    const auto zonesArray = obj[::PhosphorZones::ZoneJsonKeys::Zones].toArray();
    for (const auto& zoneVal : zonesArray) {
        QJsonObject zoneObj = zoneVal.toObject();
        auto* zone = new PhosphorZones::Zone(layout);

        zone->setName(zoneObj[::PhosphorZones::ZoneJsonKeys::Name].toString());
        zone->setZoneNumber(zoneObj[::PhosphorZones::ZoneJsonKeys::ZoneNumber].toInt());

        QJsonObject relGeo = zoneObj[::PhosphorZones::ZoneJsonKeys::RelativeGeometry].toObject();
        zone->setRelativeGeometry(QRectF(relGeo[::PhosphorZones::ZoneJsonKeys::X].toDouble(),
                                         relGeo[::PhosphorZones::ZoneJsonKeys::Y].toDouble(),
                                         relGeo[::PhosphorZones::ZoneJsonKeys::Width].toDouble(),
                                         relGeo[::PhosphorZones::ZoneJsonKeys::Height].toDouble()));

        // Per-zone geometry mode
        zone->setGeometryModeInt(zoneObj[::PhosphorZones::ZoneJsonKeys::GeometryMode].toInt(0));
        if (zoneObj.contains(::PhosphorZones::ZoneJsonKeys::FixedGeometry)) {
            QJsonObject fixedGeo = zoneObj[::PhosphorZones::ZoneJsonKeys::FixedGeometry].toObject();
            zone->setFixedGeometry(QRectF(fixedGeo[::PhosphorZones::ZoneJsonKeys::X].toDouble(),
                                          fixedGeo[::PhosphorZones::ZoneJsonKeys::Y].toDouble(),
                                          fixedGeo[::PhosphorZones::ZoneJsonKeys::Width].toDouble(),
                                          fixedGeo[::PhosphorZones::ZoneJsonKeys::Height].toDouble()));
        }

        QJsonObject appearance = zoneObj[::PhosphorZones::ZoneJsonKeys::Appearance].toObject();
        if (!appearance.isEmpty()) {
            zone->setHighlightColor(QColor(appearance[::PhosphorZones::ZoneJsonKeys::HighlightColor].toString()));
            zone->setInactiveColor(QColor(appearance[::PhosphorZones::ZoneJsonKeys::InactiveColor].toString()));
            zone->setBorderColor(QColor(appearance[::PhosphorZones::ZoneJsonKeys::BorderColor].toString()));

            if (appearance.contains(::PhosphorZones::ZoneJsonKeys::ActiveOpacity)) {
                zone->setActiveOpacity(appearance[::PhosphorZones::ZoneJsonKeys::ActiveOpacity].toDouble());
            }
            if (appearance.contains(::PhosphorZones::ZoneJsonKeys::InactiveOpacity)) {
                zone->setInactiveOpacity(appearance[::PhosphorZones::ZoneJsonKeys::InactiveOpacity].toDouble());
            }
            if (appearance.contains(::PhosphorZones::ZoneJsonKeys::BorderWidth)) {
                zone->setBorderWidth(appearance[::PhosphorZones::ZoneJsonKeys::BorderWidth].toInt());
            }
            if (appearance.contains(::PhosphorZones::ZoneJsonKeys::BorderRadius)) {
                zone->setBorderRadius(appearance[::PhosphorZones::ZoneJsonKeys::BorderRadius].toInt());
            }
            if (appearance.contains(::PhosphorZones::ZoneJsonKeys::UseCustomColors)) {
                zone->setUseCustomColors(appearance[::PhosphorZones::ZoneJsonKeys::UseCustomColors].toBool());
            }
            if (appearance.contains(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode)) {
                zone->setOverlayDisplayMode(appearance[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode].toInt(-1));
            }
        }

        layout->addZone(zone);
    }

    // endBatchModify() is called by batchGuard (RAII) when the function returns

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

    auto* layout = PhosphorZones::Layout::fromJson(*objOpt, m_layoutManager);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Failed to create layout from JSON";
        return QString();
    }

    m_layoutManager->addLayout(layout);

    qCInfo(lcDbusLayout) << "Created layout from JSON:" << layout->id();
    return layout->id().toString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Editor Launch
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutAdaptor::openEditor()
{
    launchEditor({}, QString());
}

void LayoutAdaptor::openEditorForScreen(const QString& screenId)
{
    // Intentionally passes the screen ID — the editor process
    // uses it for QScreen::name() matching and geometry lookup.
    launchEditor({QStringLiteral("--screen"), screenId}, QStringLiteral("for screen: %1").arg(screenId));
}

void LayoutAdaptor::openEditorForLayout(const QString& layoutId)
{
    launchEditor({QStringLiteral("--layout"), layoutId}, QStringLiteral("for layout: %1").arg(layoutId));
}

void LayoutAdaptor::openEditorForLayoutOnScreen(const QString& layoutId, const QString& screenId)
{
    QStringList args{QStringLiteral("--layout"), layoutId};
    if (!screenId.isEmpty()) {
        args << QStringLiteral("--screen") << screenId;
    }
    launchEditor(args, QStringLiteral("for layout: %1 on screen: %2").arg(layoutId, screenId));
}

} // namespace PlasmaZones
