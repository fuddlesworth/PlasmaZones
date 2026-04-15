// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../layoutadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/layout.h"
#include "../../core/layoutfactory.h"
#include "../../core/zone.h"
#include "../../core/constants.h"
#include "../../core/layoututils.h"
#include "../../core/layoutmanager.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../autotile/AlgorithmRegistry.h"
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
        Layout* newLayout = layouts.last();
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
// Layout Update (Editor Support)
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

    // Handle autotile layout settings updates (gaps, visibility, shader only)
    if (LayoutId::isAutotile(idStr)) {
        QString algoId = LayoutId::extractAlgorithmId(idStr);
        QJsonObject overrides;
        if (obj.contains(JsonKeys::ZonePadding))
            overrides[JsonKeys::ZonePadding] = obj[JsonKeys::ZonePadding];
        if (obj.contains(JsonKeys::OuterGap))
            overrides[JsonKeys::OuterGap] = obj[JsonKeys::OuterGap];
        if (obj.contains(JsonKeys::AllowedScreens))
            overrides[JsonKeys::AllowedScreens] = obj[JsonKeys::AllowedScreens];
        if (obj.contains(JsonKeys::AllowedDesktops))
            overrides[JsonKeys::AllowedDesktops] = obj[JsonKeys::AllowedDesktops];
        if (obj.contains(JsonKeys::AllowedActivities))
            overrides[JsonKeys::AllowedActivities] = obj[JsonKeys::AllowedActivities];
        if (obj.contains(JsonKeys::ShaderId))
            overrides[JsonKeys::ShaderId] = obj[JsonKeys::ShaderId];
        if (obj.contains(JsonKeys::ShaderParams))
            overrides[JsonKeys::ShaderParams] = obj[JsonKeys::ShaderParams];
        if (obj.contains(JsonKeys::OverlayDisplayMode))
            overrides[JsonKeys::OverlayDisplayMode] = obj[JsonKeys::OverlayDisplayMode];
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
    layout->setName(obj[JsonKeys::Name].toString());

    // Update per-layout gap overrides (-1 = use global setting)
    if (obj.contains(JsonKeys::ZonePadding)) {
        layout->setZonePadding(obj[JsonKeys::ZonePadding].toInt(-1));
    } else {
        layout->clearZonePaddingOverride();
    }
    // Set each gap field explicitly — avoid clearOuterGapOverride() which clobbers
    // per-side state before per-side keys are processed
    layout->setOuterGap(obj.contains(JsonKeys::OuterGap) ? obj[JsonKeys::OuterGap].toInt(-1) : -1);
    layout->setUsePerSideOuterGap(obj[JsonKeys::UsePerSideOuterGap].toBool(false));
    layout->setOuterGapTop(obj.contains(JsonKeys::OuterGapTop) ? obj[JsonKeys::OuterGapTop].toInt(-1) : -1);
    layout->setOuterGapBottom(obj.contains(JsonKeys::OuterGapBottom) ? obj[JsonKeys::OuterGapBottom].toInt(-1) : -1);
    layout->setOuterGapLeft(obj.contains(JsonKeys::OuterGapLeft) ? obj[JsonKeys::OuterGapLeft].toInt(-1) : -1);
    layout->setOuterGapRight(obj.contains(JsonKeys::OuterGapRight) ? obj[JsonKeys::OuterGapRight].toInt(-1) : -1);

    // Update per-layout overlay display mode override
    if (obj.contains(JsonKeys::OverlayDisplayMode)) {
        layout->setOverlayDisplayMode(obj[JsonKeys::OverlayDisplayMode].toInt(-1));
    } else {
        layout->clearOverlayDisplayModeOverride();
    }

    // Update full screen geometry mode
    layout->setUseFullScreenGeometry(obj[JsonKeys::UseFullScreenGeometry].toBool(false));

    // Update aspect ratio classification
    if (obj.contains(JsonKeys::AspectRatioClassKey)) {
        layout->setAspectRatioClassInt(obj[JsonKeys::AspectRatioClassKey].toInt(0));
    } else {
        layout->setAspectRatioClassInt(0);
    }

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

        // Per-zone geometry mode
        zone->setGeometryModeInt(zoneObj[JsonKeys::GeometryMode].toInt(0));
        if (zoneObj.contains(JsonKeys::FixedGeometry)) {
            QJsonObject fixedGeo = zoneObj[JsonKeys::FixedGeometry].toObject();
            zone->setFixedGeometry(QRectF(fixedGeo[JsonKeys::X].toDouble(), fixedGeo[JsonKeys::Y].toDouble(),
                                          fixedGeo[JsonKeys::Width].toDouble(), fixedGeo[JsonKeys::Height].toDouble()));
        }

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
            if (appearance.contains(JsonKeys::OverlayDisplayMode)) {
                zone->setOverlayDisplayMode(appearance[JsonKeys::OverlayDisplayMode].toInt(-1));
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

    auto* layout = Layout::fromJson(*objOpt, m_layoutManager);
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
