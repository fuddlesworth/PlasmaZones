// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../layoutadaptor.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutFactory.h>
#include <PhosphorZones/Zone.h>
#include "core/types/constants.h"
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <array>
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

    // The path comes off the bus, which any session process can reach, and the
    // registry opens it verbatim. Sanitise here: this is the only chokepoint all
    // callers share (the settings app sanitises too, but the editor client and
    // direct bus calls do not).
    const QString safePath = Utils::sanitizeIOPath(filePath);
    if (safePath.isEmpty()) {
        qCWarning(lcDbusLayout) << "import layout: refusing unsafe path" << filePath;
        return QString();
    }

    // The registry hands back the imported layout itself, so the new ID comes
    // straight from it. Deriving it from layouts().last() encoded an
    // append-order assumption nothing in the registry enforces.
    PhosphorZones::Layout* newLayout = m_layoutManager->importLayout(safePath);
    if (!newLayout) {
        qCWarning(lcDbusLayout) << "Failed to import layout from" << safePath;
        return QString();
    }

    // Imported files bypass the editor UI, so apply the same D-Bus boundary
    // name clamp the other entry points enforce. The setters only emit when
    // the value actually changes, so re-setting an already-short name is free.
    newLayout->setName(clampName(newLayout->name()));
    const auto zones = newLayout->zones();
    for (PhosphorZones::Zone* zone : zones) {
        zone->setName(clampName(zone->name()));
    }

    qCInfo(lcDbusLayout) << "Imported layout from" << safePath << "with ID" << newLayout->id();
    const QString newId = newLayout->id().toString();
    Q_EMIT layoutCreated(newId);
    return newId;
}

bool LayoutAdaptor::exportLayout(const QString& layoutId, const QString& filePath)
{
    if (!validateNonEmpty(filePath, QStringLiteral("file path"), QStringLiteral("export layout"))) {
        return false;
    }

    // Export WRITES the caller's path, so an unsanitised value off the bus
    // clobbers whatever it names. Same chokepoint reasoning as importLayout.
    const QString safePath = Utils::sanitizeIOPath(filePath);
    if (safePath.isEmpty()) {
        qCWarning(lcDbusLayout) << "export layout: refusing unsafe path" << filePath;
        return false;
    }

    auto* layout = getValidatedLayout(layoutId, QStringLiteral("export layout"));
    if (!layout) {
        return false;
    }

    // Report what the write actually did. The previous form logged success
    // unconditionally, so the journal said "Exported layout" for an export that
    // never reached the disk.
    if (!m_layoutManager->exportLayout(layout, safePath)) {
        qCWarning(lcDbusLayout) << "Failed to export layout" << layoutId << "to" << safePath;
        return false;
    }
    qCInfo(lcDbusLayout) << "Exported layout" << layoutId << "to" << safePath;
    return true;
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
    if (PhosphorLayout::LayoutId::isAutotile(idStr)) {
        QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(idStr);
        // D-Bus boundary: an id of exactly "autotile:" passes isAutotile but
        // yields an empty algorithm key — saving overrides under it would
        // create an unreachable settings group.
        if (algoId.isEmpty()) {
            qCWarning(lcDbusLayout) << "updateLayout: autotile id with empty algorithm id rejected:" << idStr;
            return false;
        }
        // Start from the stored entry so keys this editor doesn't manage
        // (notably hiddenFromSelector, written via setLayoutHidden) survive a
        // gaps/shader/allow-list save — autotile entries now share the unified
        // layout-settings.json sidecar. Editor-managed keys are set when present
        // in the incoming object and cleared when absent (reset-to-default).
        QJsonObject overrides = m_layoutManager->loadAutotileOverrides(algoId);
        // CTAD deduces the array size from the initializer so the element count
        // stays in sync automatically (no hand-maintained size). hiddenFromSelector
        // is intentionally absent — it's owned by setLayoutHidden, not the editor.
        const std::array editorKeys{
            ::PhosphorZones::ZoneJsonKeys::ZonePadding,       ::PhosphorZones::ZoneJsonKeys::OuterGap,
            ::PhosphorZones::ZoneJsonKeys::AllowedScreens,    ::PhosphorZones::ZoneJsonKeys::AllowedDesktops,
            ::PhosphorZones::ZoneJsonKeys::AllowedActivities, ::PhosphorZones::ZoneJsonKeys::ShaderId,
            ::PhosphorZones::ZoneJsonKeys::ShaderParams,      ::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode,
        };
        for (const QLatin1String key : editorKeys) {
            if (obj.contains(key)) {
                overrides[key] = obj.value(key);
            } else {
                overrides.remove(key);
            }
        }
        m_layoutManager->saveAutotileOverrides(algoId, overrides);
        qCInfo(lcDbusLayout) << "Saved autotile overrides for algorithm:" << algoId;
        // Autotile override save mutates a single autotile preview entry, not
        // the layout list itself. Emit layoutChanged only; layoutListChanged
        // stays reserved for add/delete operations. SettingsController wires
        // both signals to the same reload slot so no visible behavior changes.
        Q_EMIT layoutChanged(layoutJson);
        return true;
    }

    // Past the autotile branch this is the full-layout replace path, and it is
    // as bus-reachable as createLayoutFromJson: the zone loop below reads the
    // incoming geometry straight into setRelativeGeometry, so it takes the same
    // schema gate for the same reason. It sits after the autotile branch on
    // purpose — an autotile payload names an algorithm rather than a layout and
    // only ever contributes an allow-listed set of sidecar keys, so the
    // full-layout schema (zones required, and none supplied) is the wrong shape
    // for it. That branch reaches no zone geometry to validate.
    if (!PhosphorZones::LayoutRegistry::isLayoutJsonValid(obj, QStringLiteral("D-Bus updateLayout"))) {
        return false;
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

    // Update basic properties (name clamped at the D-Bus boundary)
    layout->setName(clampName(obj[::PhosphorZones::ZoneJsonKeys::Name].toString()));

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

    // Update aspect ratio classification. fromJsonValue accepts both serialized
    // forms and maps a missing key to Any, which is the reset this branch wants.
    layout->setAspectRatioClassInt(static_cast<int>(
        PhosphorLayout::ScreenClassification::fromJsonValue(obj[::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey])));

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

    // Clear existing zones and add new ones
    layout->clearZones();

    const auto zonesArray = obj[::PhosphorZones::ZoneJsonKeys::Zones].toArray();
    for (const auto& zoneVal : zonesArray) {
        QJsonObject zoneObj = zoneVal.toObject();
        auto* zone = new PhosphorZones::Zone(layout);

        zone->setName(clampName(zoneObj[::PhosphorZones::ZoneJsonKeys::Name].toString()));
        zone->setZoneNumber(zoneObj[::PhosphorZones::ZoneJsonKeys::ZoneNumber].toInt());

        QJsonObject relGeo = zoneObj[::PhosphorZones::ZoneJsonKeys::RelativeGeometry].toObject();
        zone->setRelativeGeometry(QRectF(relGeo[::PhosphorZones::ZoneJsonKeys::X].toDouble(),
                                         relGeo[::PhosphorZones::ZoneJsonKeys::Y].toDouble(),
                                         relGeo[::PhosphorZones::ZoneJsonKeys::Width].toDouble(),
                                         relGeo[::PhosphorZones::ZoneJsonKeys::Height].toDouble()));

        // Per-zone geometry mode
        zone->setGeometryModeInt(zoneObj[::PhosphorZones::ZoneJsonKeys::GeometryMode].toInt(0));
        // fixedGeometry is the one geometry key the schema gate above does not
        // cover — the schema describes relativeGeometry and never mentions it.
        // This is also the more exposed of the two ingresses that accept it
        // (any session process can reach the bus, where a file has to be
        // imported), so it takes the same normalization Zone::fromJson applies
        // to the file side. Without it a negative width or a NaN lands verbatim
        // and the daemon then persists a layout it will refuse to load.
        if (zoneObj.contains(::PhosphorZones::ZoneJsonKeys::FixedGeometry)) {
            QJsonObject fixedGeo = zoneObj[::PhosphorZones::ZoneJsonKeys::FixedGeometry].toObject();
            zone->setFixedGeometry(PhosphorZones::Zone::sanitizeFixedGeometry(
                QRectF(fixedGeo[::PhosphorZones::ZoneJsonKeys::X].toDouble(),
                       fixedGeo[::PhosphorZones::ZoneJsonKeys::Y].toDouble(),
                       fixedGeo[::PhosphorZones::ZoneJsonKeys::Width].toDouble(),
                       fixedGeo[::PhosphorZones::ZoneJsonKeys::Height].toDouble())));
        }
        // Same fallback the file ingress applies: a Fixed zone with no usable
        // pixel payload renders from its authored relativeGeometry instead of
        // becoming an invisible snap-target sink.
        if (zone->isFixedGeometry() && zone->fixedGeometry().isEmpty()) {
            qCWarning(lcDbusLayout) << "updateLayout: zone" << zone->name()
                                    << "declares Fixed geometry with no usable fixedGeometry payload"
                                    << "— downgrading to Relative";
            zone->setGeometryMode(PhosphorZones::ZoneGeometryMode::Relative);
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

    // Clamp layout and zone names before handing the JSON to fromJson
    // (same D-Bus boundary rule as updateLayout).
    QJsonObject obj = *objOpt;
    if (obj.contains(::PhosphorZones::ZoneJsonKeys::Name)) {
        obj[::PhosphorZones::ZoneJsonKeys::Name] = clampName(obj[::PhosphorZones::ZoneJsonKeys::Name].toString());
    }
    QJsonArray zones = obj[::PhosphorZones::ZoneJsonKeys::Zones].toArray();
    for (auto zoneRef : zones) {
        QJsonObject zoneObj = zoneRef.toObject();
        if (zoneObj.contains(::PhosphorZones::ZoneJsonKeys::Name)) {
            zoneObj[::PhosphorZones::ZoneJsonKeys::Name] =
                clampName(zoneObj[::PhosphorZones::ZoneJsonKeys::Name].toString());
            zoneRef = zoneObj;
        }
    }
    if (obj.contains(::PhosphorZones::ZoneJsonKeys::Zones)) {
        obj[::PhosphorZones::ZoneJsonKeys::Zones] = zones;
    }

    // D-Bus is an untrusted boundary, so it takes the same schema gate the file
    // ingresses do (directory scan / import / system restore). The structural
    // invariants are the point: zone geometry lands in the layout verbatim, so
    // without this gate a zero-width zone reaches the zone list and persists as
    // an unselectable dead region the user cannot see or fix from the editor.
    //
    // Note the gate does not have to police every key's serialized form. Keys
    // whose reader normalizes on the way in (aspectRatioClass, read through
    // fromJsonValue, which takes both wire forms) are re-emitted canonically by
    // Layout::toJson when the registry persists them, so the on-disk file is
    // well-formed regardless of which form arrived here. Those keys are widened
    // in the schema to match the contract rather than narrowed to one form.
    if (!PhosphorZones::LayoutRegistry::isLayoutJsonValid(obj, QStringLiteral("D-Bus createLayoutFromJson"))) {
        return QString();
    }

    auto* layout = PhosphorZones::Layout::fromJson(obj, m_layoutManager);
    if (!layout) {
        qCWarning(lcDbusLayout) << "Failed to create layout from JSON";
        return QString();
    }

    m_layoutManager->addLayout(layout);

    qCInfo(lcDbusLayout) << "Created layout from JSON:" << layout->id();
    const QString newId = layout->id().toString();
    Q_EMIT layoutCreated(newId);
    return newId;
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

void LayoutAdaptor::openEditorForLayoutOnScreen(const QString& layoutId, const QString& screenId)
{
    QStringList args{QStringLiteral("--layout"), layoutId};
    if (!screenId.isEmpty()) {
        args << QStringLiteral("--screen") << screenId;
    }
    launchEditor(args, QStringLiteral("for layout: %1 on screen: %2").arg(layoutId, screenId));
}

} // namespace PlasmaZones
