// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayspagecontroller.h"

#include "core/interfaces/isettings.h"
#include "core/interfaces/shaderregistry.h"
#include "core/platform/logging.h"
#include "core/types/overlayshadertree.h"
#include "phosphor_i18n.h"
#include "settings/services/shaderpackinstaller.h"
#include "shaderpreview/shaderpreviewcontroller.h"

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>

#include <QScopedValueRollback>
#include <QSet>

#include <algorithm>

namespace PlasmaZones {

namespace {

QVariantMap profileToMap(const OverlayShaderProfile& profile)
{
    QVariantMap map;
    map.insert(QLatin1String("shaderId"), profile.shaderId);
    map.insert(QLatin1String("parameters"), profile.parameters);
    return map;
}

} // namespace

OverlaysPageController::OverlaysPageController(PlasmaZones::ShaderRegistry* shaderRegistry,
                                               PhosphorZones::IZoneLayoutRegistry* layoutRegistry, ISettings* settings,
                                               ShaderPreviewController* previewController, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("overlays-shaders"), parent)
    , m_shaderRegistry(shaderRegistry)
    , m_layoutRegistry(layoutRegistry)
    , m_settings(settings)
    , m_previewController(previewController)
{
    if (m_shaderRegistry) {
        connect(m_shaderRegistry, &PhosphorShaders::ShaderRegistry::shadersChanged, this,
                &OverlaysPageController::shaderEffectsChanged);
    }
    if (m_settings) {
        // The assignment store is the config tree; every mutation (local
        // setter, D-Bus write, profile apply, page reset) funnels through this
        // one NOTIFY, which does not say which node moved. When the write came
        // from one of this controller's own setters, m_writingPath does say,
        // and the cards use it to skip a refresh they do not need. Any other
        // writer gets the honest whole-tree answer.
        connect(m_settings, &ISettings::overlayShaderTreeChanged, this, [this]() {
            if (m_writingPath.has_value())
                Q_EMIT shaderProfileChanged(*m_writingPath, /*wholeTree=*/false);
            else
                Q_EMIT shaderProfileChanged(QString(), /*wholeTree=*/true);
        });
    }
    if (m_layoutRegistry) {
        // Layout add / remove / rename changes the assignment card list
        // and the labels the usage chips render.
        connect(m_layoutRegistry, &PhosphorLayout::ILayoutSourceRegistry::contentsChanged, this, [this]() {
            Q_EMIT shaderProfileChanged(QString(), /*wholeTree=*/true);
        });
    }
    // Last: the store's closures capture `this` and read the members above.
    initSetsStore();
}

OverlaysPageController::~OverlaysPageController() = default;

QObject* OverlaysPageController::previewController() const
{
    return m_previewController;
}

QString OverlaysPageController::layoutNameFor(const QString& layoutId) const
{
    if (!m_layoutRegistry)
        return {};
    const QVector<PhosphorZones::Layout*> layouts = m_layoutRegistry->layouts();
    for (PhosphorZones::Layout* layout : layouts) {
        if (layout && layout->id().toString() == layoutId)
            return layout->name();
    }
    return {};
}

QVariantList OverlaysPageController::assignableLayouts() const
{
    if (!m_layoutRegistry)
        return {};
    QVariantList out;
    QSet<QString> knownIds;
    const QVector<PhosphorZones::Layout*> layouts = m_layoutRegistry->layouts();
    for (PhosphorZones::Layout* layout : layouts) {
        if (!layout)
            continue;
        const QString id = layout->id().toString();
        knownIds.insert(id);
        QVariantMap entry;
        entry.insert(QLatin1String("id"), id);
        entry.insert(QLatin1String("name"), layout->name());
        // Present on EVERY row, including the live ones. The page feeds these
        // into a ListModel, whose roles are fixed by the first row appended, so
        // a live row missing the key would leave the flag undefined for the
        // orphan rows that follow it.
        entry.insert(QLatin1String("missing"), false);
        out.append(entry);
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QLatin1String("name")).toString().toLower()
            < b.toMap().value(QLatin1String("name")).toString().toLower();
    });
    // Overrides for layouts that no longer exist would otherwise be
    // invisible on the assignments page (and un-clearable from any UI).
    // Append them after the live rows so the user can still clear them.
    if (m_settings) {
        const QStringList overridden = m_settings->overlayShaderTree().overriddenLayouts();
        for (const QString& layoutId : overridden) {
            if (knownIds.contains(layoutId))
                continue;
            QVariantMap entry;
            entry.insert(QLatin1String("id"), layoutId);
            entry.insert(QLatin1String("name"), QString());
            entry.insert(QLatin1String("missing"), true);
            out.append(entry);
        }
    }
    return out;
}

bool OverlaysPageController::hasOverride(const QString& path) const
{
    if (!m_settings || path.isEmpty())
        return false;
    return m_settings->overlayShaderTree().hasOverride(path);
}

QVariantMap OverlaysPageController::rawShaderProfile(const QString& path) const
{
    if (!m_settings)
        return profileToMap({});
    const OverlayShaderTree tree = m_settings->overlayShaderTree();
    return profileToMap(path.isEmpty() ? tree.baseline() : tree.directOverride(path));
}

QVariantMap OverlaysPageController::resolvedShaderProfile(const QString& path) const
{
    if (!m_settings)
        return profileToMap({});
    const OverlayShaderTree tree = m_settings->overlayShaderTree();
    return profileToMap(path.isEmpty() ? tree.baseline() : tree.resolve(path));
}

QVariantMap OverlaysPageController::nodeState(const QString& path) const
{
    QVariantMap out;
    if (!m_settings) {
        out.insert(QLatin1String("hasOverride"), false);
        out.insert(QLatin1String("raw"), profileToMap({}));
        out.insert(QLatin1String("resolved"), profileToMap({}));
        return out;
    }
    // One read, one parse. The three separate getters each re-read the config
    // key and rebuild the whole tree, and a card wants all three every time.
    const OverlayShaderTree tree = m_settings->overlayShaderTree();
    const bool isBaseline = path.isEmpty();
    out.insert(QLatin1String("hasOverride"), !isBaseline && tree.hasOverride(path));
    out.insert(QLatin1String("raw"), profileToMap(isBaseline ? tree.baseline() : tree.directOverride(path)));
    out.insert(QLatin1String("resolved"), profileToMap(isBaseline ? tree.baseline() : tree.resolve(path)));
    return out;
}

void OverlaysPageController::writeTreeAnnouncing(const OverlayShaderTree& tree, const QString& path)
{
    // Parked across the write only. setOverlayShaderTree emits its NOTIFY
    // synchronously from inside the write, so the handler above reads this
    // while it is set; the rollback restores the empty state before anything
    // else can write, which is what keeps a foreign write from borrowing this
    // path and announcing itself as a single-node change.
    QScopedValueRollback<std::optional<QString>> announcing(m_writingPath, path);
    m_settings->setOverlayShaderTree(tree);
}

bool OverlaysPageController::acceptableShaderEffectId(const QString& effectId) const
{
    // An empty id is the "None" sentinel and a real stored value: it suppresses
    // the baseline for this layout, so it must pass.
    if (effectId.isEmpty())
        return true;
    // The schema validator already bounds the length, but it deliberately does
    // not judge the shape or the membership, so both are checked here — the
    // same gate the animations sibling applies for the same reason.
    if (effectId.size() > 256 || effectId.contains(QLatin1Char('/')) || effectId.contains(QLatin1Char('\\'))
        || effectId.contains(QLatin1String("..")) || effectId.contains(QLatin1Char('\0'))) {
        return false;
    }
    // Skipped while the registry is still empty (startup, tests), or a pack
    // that is mid-scan would have its assignment eaten.
    if (m_shaderRegistry && !m_shaderRegistry->availableShaders().isEmpty()
        && m_shaderRegistry->shaderInfo(effectId).isEmpty()) {
        return false;
    }
    return true;
}

void OverlaysPageController::setShaderOverride(const QString& path, const QString& effectId, const QVariantMap& params)
{
    if (!m_settings)
        return;
    if (!acceptableShaderEffectId(effectId)) {
        qCWarning(lcConfig) << "OverlaysPageController: refusing overlay shader id" << effectId << "for path" << path;
        return;
    }
    OverlayShaderTree tree = m_settings->overlayShaderTree();
    const OverlayShaderProfile node{effectId, params};
    if (path.isEmpty())
        tree.setBaseline(node);
    else
        tree.setOverride(path, node);
    writeTreeAnnouncing(tree, path);
}

bool OverlaysPageController::clearOverride(const QString& path)
{
    if (!m_settings || path.isEmpty())
        return false;
    OverlayShaderTree tree = m_settings->overlayShaderTree();
    if (!tree.clearOverride(path))
        return false;
    writeTreeAnnouncing(tree, path);
    return true;
}

QVariantList OverlaysPageController::shaderParameters(const QString& effectId) const
{
    if (!m_shaderRegistry || effectId.isEmpty())
        return {};
    // shaderInfo() is the keyed lookup and returns the same row shape as the
    // flattened list, so it carries the pack's ParameterInfo maps too. The
    // list form rebuilds every installed pack's row from scratch on each call,
    // which is a whole-registry walk to read one pack's parameters — and a
    // card calls this on every refresh.
    return m_shaderRegistry->shaderInfo(effectId).value(QLatin1String("parameters")).toList();
}

QString OverlaysPageController::userShaderDirectoryPath() const
{
    if (!m_shaderRegistry)
        return {};
    return m_shaderRegistry->userShaderDirectory();
}

QVariantList OverlaysPageController::availableShaderEffects() const
{
    if (!m_shaderRegistry)
        return {};
    // Registry returns its native shape with `isUserShader`; rename to
    // `isUserEffect` so the pack-agnostic ShaderBrowserPage / Card /
    // Dialog can read both registries through the same key. The rest of
    // the keys (id, name, description, author, version, category,
    // parameters) already match.
    QVariantList effects = m_shaderRegistry->availableShadersVariant();
    for (QVariant& v : effects) {
        QVariantMap m = v.toMap();
        if (m.contains(QLatin1String("isUserShader"))) {
            m.insert(QLatin1String("isUserEffect"), m.value(QLatin1String("isUserShader")));
            m.remove(QLatin1String("isUserShader"));
        }
        // Dropped like the animation/decoration bridges: the browser
        // previews live shaders and no QML reads the key any more.
        m.remove(QLatin1String("previewPath"));
        v = m;
    }
    return effects;
}

void OverlaysPageController::openUserShaderDirectory()
{
    if (!m_shaderRegistry)
        return;
    // Forward to the registry's create-and-open primitive — keeps the
    // mkpath / openUrl pair in one place so `installShaderPack` and the
    // "Open Folder" button can never drift apart on what counts as the
    // user shader directory.
    m_shaderRegistry->openUserShaderDirectory();
}

bool OverlaysPageController::installShaderPack(const QString& sourceUrl)
{
    // All validation + copy lives in the shared ShaderPackInstaller
    // helper. Same logic as the animations-shader page (DRY) and the
    // security-sensitive bits (symlink rejection, metadata.json
    // verification, rollback) only need an audit in one place.
    const auto result = ShaderPackInstaller::install(sourceUrl, userShaderDirectoryPath());
    if (result != ShaderPackInstaller::Result::Success) {
        const QString message = ShaderPackInstaller::errorMessage(result);
        qCWarning(lcConfig) << "installShaderPack (overlay):" << message << "— source:" << sourceUrl;
        // Surface the reason via the chrome toast — the InlineMessage
        // in the drop zone is generic; the underlying failure reason
        // (DestinationExists, MissingMetadata, PackTooLarge…) gives
        // the user a concrete next step.
        Q_EMIT toastRequested(message);
        return false;
    }
    // The registry's filewatcher rescans on its own — `shadersChanged`
    // fires automatically and reaches QML through this controller's
    // forwarded `shaderEffectsChanged` signal.
    return true;
}

QVariantList OverlaysPageController::shaderEffectUsages(const QString& effectId) const
{
    if (!m_settings || effectId.isEmpty())
        return {};
    const OverlayShaderTree tree = m_settings->overlayShaderTree();
    QVariantList out;
    if (tree.baseline().shaderId == effectId) {
        QVariantMap entry;
        entry.insert(QLatin1String("path"), QString());
        entry.insert(QLatin1String("label"), PhosphorI18n::tr("Global default"));
        out.append(entry);
    }
    QVariantList layoutRows;
    const QStringList overridden = tree.overriddenLayouts();
    for (const QString& layoutId : overridden) {
        if (tree.directOverride(layoutId).shaderId != effectId)
            continue;
        QVariantMap entry;
        // `path` is the layout's UUID-with-braces (matches the rest of
        // the codebase's QUuid::toString convention); `label` is the
        // user-facing name. A layout this machine does not have gets the
        // shared absent-layout wording rather than an empty label: the
        // browser falls back to `path` when the label is empty, and that
        // printed a raw 36-character UUID where the assignments page and the
        // coverage chip both said something readable about the same state.
        const QString name = layoutNameFor(layoutId);
        entry.insert(QLatin1String("path"), layoutId);
        entry.insert(QLatin1String("label"), name.isEmpty() ? absentLayoutLabel(layoutId) : name);
        layoutRows.append(entry);
    }
    std::sort(layoutRows.begin(), layoutRows.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QLatin1String("label")).toString().toLower()
            < b.toMap().value(QLatin1String("label")).toString().toLower();
    });
    out.append(layoutRows);
    return out;
}

} // namespace PlasmaZones
