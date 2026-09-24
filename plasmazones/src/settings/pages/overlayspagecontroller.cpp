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

/// An overlay preset id is a UUID or a pack-declared preset name. Bounded like
/// every other string this controller lets reach disk; the schema sanitizer
/// bounds it again on the way in, and an id naming no preset resolves to the
/// node own parameters, so dropping one degrades rather than breaks.
constexpr int kMaxOverlayPresetIdChars = 1024;

QVariantMap profileToMap(const OverlayShaderProfile& profile)
{
    QVariantMap map;
    map.insert(QLatin1String("shaderId"), profile.shaderId);
    map.insert(QLatin1String("parameters"), profile.parameters);
    // The preset reference, without which the whole overlay preset row was
    // inert: the card derives its selected preset from this map, so picking one
    // persisted correctly and then snapped straight back to "None", with Revert,
    // Update, Rename and Delete all hidden because the row believed no preset
    // was set. Emitted unconditionally, like `parameters`.
    map.insert(QLatin1String("presetId"), profile.presetId);
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
    // What the path RESOLVES to, not its direct override. For an inheriting layout the
    // direct override is a default-constructed profile, so the carry below could never
    // fire and promoting such a layout destroyed the preset it was resolving with —
    // which the card reaches on every slider edit AND from its Revert control, the one
    // documented as "every value then resolves from the preset again". This tree is
    // whole-node and one step, so resolve() is the baseline for an un-overridden path,
    // exactly as setShaderPreset already seeds from it.
    const OverlayShaderProfile stored = path.isEmpty() ? tree.baseline() : tree.resolve(path);
    OverlayShaderProfile node{effectId, params};
    // Carry the preset reference across, but ONLY while the pack is unchanged.
    // This writer is how a parameter edit lands as well as how a pack is
    // picked, so rebuilding the node without this would silently drop the
    // preset the moment a slider moved. When the pack DOES change the
    // reference has to go: presets are keyed by pack, so one belonging to the
    // old pack would resolve to nothing against the new one.
    if (stored.shaderId == effectId)
        node.presetId = stored.presetId;
    if (path.isEmpty())
        tree.setBaseline(node);
    else
        tree.setOverride(path, node);
    writeTreeAnnouncing(tree, path);
}

void OverlaysPageController::setShaderPreset(const QString& path, const QString& presetId)
{
    if (!m_settings)
        return;
    if (presetId.size() > kMaxOverlayPresetIdChars) {
        qCWarning(lcConfig) << "OverlaysPageController: refusing an over-long preset id for path" << path;
        return;
    }
    OverlayShaderTree tree = m_settings->overlayShaderTree();
    // Seed from what the layout RESOLVES to, not from its direct override, when it
    // has none. `directOverride` answers a default-constructed profile for an
    // unoverridden layout, so engaging only `presetId` on it stored
    // `{shaderId: "", presetId: X}` — and an empty shaderId is the "None" sentinel
    // that SUPPRESSES the baseline shader for that layout (see
    // acceptableShaderEffectId). Picking a preset on a card that was showing the
    // inherited shader therefore turned that layout's overlay off. The card offers
    // the preset row whenever a shader resolves, inherited or not, so this was
    // reachable in one click.
    //
    // `resolve()` is one step on this tree — an override or the baseline — so this
    // carries the pack and its parameters down from the baseline exactly as the card
    // was already displaying them, which is what the user is tuning.
    OverlayShaderProfile node = path.isEmpty() ? tree.baseline() : tree.resolve(path);
    // Nothing to do when the id already matches, whether the layout is overridden or
    // not. An `|| tree.hasOverride(path)` conjunct used to gate this, which made the
    // early return unreachable for an INHERITING layout: the write then engaged an override
    // carrying the baseline's pack and parameters, pinning a layout that had been
    // following the baseline, for a call that changed nothing. The card's
    // `onPresetDeleted` reaches here with an empty id unconditionally, so deleting a
    // preset while viewing an inheriting card was enough to pin it.
    if (node.presetId == presetId)
        return;
    // The pack and the parameter edits are left exactly as stored: this call
    // carries a preset and nothing else, and the parameters become deltas on
    // top of it rather than being replaced by it.
    node.presetId = presetId;
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
