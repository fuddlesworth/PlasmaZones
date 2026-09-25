// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Surface decoration for the daemon's own surfaces: resolving a surface path
// through the decoration tree and composing the resulting pack chain into the
// stage list QML's SurfaceDecoration consumes.
//
// Split out of osd.cpp, which was holding two unrelated concerns and had reached
// the file-size ceiling. The OSD show and dismiss lifecycle stayed there. This
// half is reached from every decorated daemon surface (the zone selector, snap
// assist, the layout picker, the cheatsheet and the OSD), so it was never really
// OSD code to begin with.

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "core/interfaces/isettings.h"
#include "qml_property_names.h"

#include <PhosphorShaders/ShaderPresetRegistry.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>
#include <PhosphorSurface/SurfaceChainCompose.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>
#include <PhosphorSurface/SurfaceThemeResolve.h>

#include <QGuiApplication>
#include <QImage>
#include <QLatin1String>
#include <QPalette>
#include <QQuickItem>
#include <QVariant>

namespace PlasmaZones {

namespace {
/// Forces the re-bake an in-place source edit cannot get. See qml_property_names.h.
int s_decorationReloadGeneration = 0;

} // namespace

void OverlayService::setSurfaceShaderRegistry(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry)
{
    if (m_surfaceShaderRegistry == registry) {
        return;
    }
    // Disconnect from the outgoing registry before the borrow is overwritten,
    // or a re-set would leave a second connection behind. Daemon::stop() nulls
    // this borrow before resetting the registry, so the old pointer is still
    // alive here.
    if (m_surfaceShaderRegistry) {
        disconnect(m_surfaceShaderRegistry, nullptr, this, nullptr);
    }
    m_surfaceShaderRegistry = registry;
    // Re-arm the refusal warnings: the pack set has changed, so a pack that
    // was reported missing may now be present (or newly broken). effectsChanged
    // fires only on a real content or discovery change, never on a plain
    // rescan, so this cannot put the warnings back to once-per-show.
    m_warnedDecorationPacks.clear();
    if (m_surfaceShaderRegistry) {
        connect(m_surfaceShaderRegistry, &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this,
                [this]() {
                    m_warnedDecorationPacks.clear();
                    ++s_decorationReloadGeneration; // before the re-resolve, so it is written
                    // And RE-RESOLVE what is on screen. A pack installed, removed or
                    // edited on disk changes what a visible popup's chain composes to,
                    // and nothing else on this path pushes that: the chain is resolved
                    // at show time, so a popup already up kept the old composition until
                    // it was dismissed. The shell's twin does the same thing through
                    // bump() for its own chrome.
                    reapplyVisiblePopupDecorations();
                });
    }
}

void OverlayService::reapplyVisiblePopupDecorations()
{
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        const auto& state = it.value();
        // The zone selector is genuinely MULTI-screen: showZoneSelector loops every
        // eligible screen, so its flag alone is the right gate and there is no
        // companion screen id to compare against.
        if (m_zoneSelectorVisible) {
            applyDecoration(state.zoneSelectorSlot(), PhosphorSurfaceShaders::decorationPopupZoneSelectorPath());
        }
        // The next three are singletons across all screens (see their declarations),
        // so each carries a screen id beside its flag and only that screen hosts it.
        // Gating on the flag alone re-resolved and recomposed the chain on EVERY
        // screen's slot, and each of those costs a full decoration-tree parse, since
        // the settings getter carries no parse cache.
        //
        // Deliberately NOT gated on the slot's own visibility, which is what the OSD
        // arm below uses: for the zone selector that predicate is documented as wrong
        // (a slot hidden under a modal is still logically up, and the restore path
        // re-shows it without re-decorating), and using it here would invite the same
        // mistake by symmetry.
        if (m_snapAssistVisible && it.key() == m_snapAssistScreenId) {
            applyDecoration(state.snapAssistSlot(), PhosphorSurfaceShaders::decorationPopupSnapAssistPath());
        }
        if (m_layoutPickerVisible && it.key() == m_layoutPickerScreenId) {
            applyDecoration(state.layoutPickerSlot(), PhosphorSurfaceShaders::decorationPopupLayoutPickerPath());
        }
        if (m_cheatsheetVisible && it.key() == m_cheatsheetScreenId) {
            applyDecoration(state.cheatsheetSlot(), PhosphorSurfaceShaders::decorationPopupCheatsheetPath());
        }
        // THE OSD IS A DECORATED SURFACE TOO and had no arm here, so a pack
        // installed, removed or enabled while one was on screen re-resolved
        // every popup except it. Every OSD show path already calls
        // applyDecoration(osdSlot, "osd"), so this is the same call the show
        // paths make, on the same path string.
        //
        // Keyed on the ITEM's own visibility rather than a service flag, because
        // the OSD has no flag: the show paths call setVisible(true) on the slot
        // directly and the dismiss timer hides it, so the item is the authority.
        // The four above have flags because their visibility is service state.
        //
        // The window in which this matters is short, since an OSD is transient,
        // and an in-place pack EDIT re-resolves to an identical chain anyway.
        // It bites on an install, an uninstall or an enable change.
        if (QQuickItem* const osd = state.osdSlot(); osd && osd->isVisible()) {
            applyDecoration(osd, PhosphorSurfaceShaders::decorationOsdPath());
        }
    }
}

void OverlayService::applyDecoration(QObject* slot, const QString& surfacePath)
{
    if (!slot) {
        return;
    }

    // Helper to leave the slot undecorated: clear the chain so the QML
    // SurfaceDecoration stays inert and the card draws its native chrome.
    const auto clearDecoration = [this, slot]() {
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::DecorationChain), QVariant::fromValue(QVariantList()));
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::DecorationOuterPadding), 0.0);
        // Drop the backdrop with the chain: an undecorated slot has nothing to
        // sample it, and holding the image would keep a wallpaper-sized texture
        // uploaded for a surface that draws none of it.
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::BackdropTexture), QVariant());
        // No decoration -> no audio need on this slot; let CAVA wind down if it
        // was only kept alive for an audio decoration here.
        if (auto* item = qobject_cast<QQuickItem*>(slot)) {
            item->setProperty(OverlayQmlPropertyNames::WantsAudioDecoration.data(), false);
            // Symmetric with applyDecoration's UniqueConnection: an undecorated
            // slot no longer needs the show/hide hook (applyDecoration re-adds
            // it if the slot is decorated again).
            disconnect(item, &QQuickItem::visibleChanged, this, &OverlayService::syncCavaState);
        }
        syncCavaState();
    };

    if (!m_settings || !m_surfaceShaderRegistry) {
        clearDecoration();
        return;
    }

    // Resolve @p surfacePath through the decoration tree. resolve() walks
    // baseline → category → leaf and returns a DecorationProfile carrying an
    // effective CHAIN (ordered pack ids) plus a per-pack parameters map.
    const PhosphorSurfaceShaders::DecorationProfileTree tree = m_settings->decorationProfileTree();
    // Flatten each layer's preset reference into its parameters, after the
    // walk-up rather than before it — see withPresetsResolved for why the order
    // matters. With no preset registry injected this is the resolved profile
    // unchanged.
    const PhosphorSurfaceShaders::DecorationProfile profile = m_presetRegistry
        ? PhosphorSurfaceShaders::withPresetsResolved(tree.resolve(surfacePath), *m_presetRegistry,
                                                      PhosphorShaders::ShaderFamily::Surface)
        : tree.resolve(surfacePath);
    // enabledChain(): a pack the user toggled off must not render here either.
    const QStringList chain = profile.enabledChain();
    if (chain.isEmpty()) {
        // No decoration packs configured for this surface — render it plainly.
        clearDecoration();
        return;
    }

    // The daemon composes the FULL chain: the QML SurfaceDecoration host runs
    // one SurfaceShaderItem per stage, each sampling the previous stage's
    // output through an interposed ShaderEffectSource — the QML analogue of
    // the compositor's composite ping-pong (renderSurfaceChainComposite), so
    // a border + glow chain renders both packs here too. Buffer passes
    // (multipass packs like the blur family) run here as well — each stage
    // forwards its pack's declared buffer set below. needsBackdrop packs have
    // no scene to sample on the daemon, so the desktop wallpaper is bound as a
    // stand-in below and they take their uHasBackdrop = 0 fallback only when
    // that cannot be resolved.
    //
    // Per-pack parameter overrides come from the resolved profile (shape
    // { packId -> { paramId -> value } }). p_useSystemAccent is a
    // host-consumed flag; the overlay path passes the pack's declared colour
    // params through translateSurfaceParams unchanged (system-accent colour
    // resolution is performed by the daemon's colour pipeline, not
    // synthesised here). Each stage's vertexSource satisfies the warm-bake
    // HOST-WIRING PRECONDITION (daemon.cpp): a pack declaring its own vertex
    // stage keys the same vert here as the warm bake; the empty-URL case
    // (every current pack) falls through to the item's shared-surface.vert
    // resolution.
    const QVariantMap allPackParams = profile.effectiveParameters();
    QVariantList stages;
    double outerPadding = 0.0;
    bool chainWantsAudio = false;
    bool chainWantsBackdrop = false;
    // Theme colours for the pack flag resolver, read once for the whole chain.
    const QPalette pal = QGuiApplication::palette();
    // The blur-quality tier the composer folds into every declared buffer scale.
    // m_settings is non-null here: this function early-returns above when it is.
    const qreal blurScale = m_settings->decorationBlurScaleMultiplier();
    // The chain's shared bottom-corner answer, resolved ONCE for the whole chain
    // and injected into every stage below. The pane's outline is one shape, so a
    // backdrop pack squaring its bottom corners has to take the border and the
    // halo with it rather than leaving them tracing a corner the pane gave up.
    // See chainRoundBottomCorners for the resolution order.
    const QVariant chainBottomCorners =
        PhosphorSurfaceShaders::chainRoundBottomCorners(*m_surfaceShaderRegistry, chain, allPackParams);
    for (const QString& packId : chain) {
        if (!m_surfaceShaderRegistry->hasEffect(packId)) {
            // One warning per pack id per REASON, not one per show: a profile
            // naming a pack the user uninstalled is a standing condition, and
            // this runs on every OSD show. parseEffect's texture drops are
            // one-shot for the same reason, though only per registry parse.
            // (translateSurfaceParams' overflow summaries are NOT — they are
            // one summary per call, and composeStageMap calls it for every
            // pack on every show.)
            //
            // Keyed per reason rather than per pack: the missing and invalid
            // branches are mutually exclusive within one iteration but not
            // over time, so a bare pack id would let "uninstalled" swallow the
            // later, different "reinstalled but broken" warning for good.
            const QString missingKey = packId + QLatin1String("|missing");
            if (!m_warnedDecorationPacks.contains(missingKey)) {
                m_warnedDecorationPacks.insert(missingKey);
                qCWarning(lcOverlay) << "Surface decoration (" << surfacePath << "): resolved pack id" << packId
                                     << "is not present in the surface-shader registry — skipping this chain stage";
            }
            continue;
        }
        const PhosphorSurfaceShaders::SurfaceShaderEffect effect = m_surfaceShaderRegistry->effect(packId);
        // isValid() already requires a non-empty fragmentShaderPath.
        if (!effect.isValid()) {
            // Standing condition too, and on the same every-show path: an
            // installed pack whose fragment shader will not resolve stays
            // broken until the user reinstalls it. Same per-reason keying as
            // the missing branch above.
            const QString invalidKey = packId + QLatin1String("|invalid");
            if (!m_warnedDecorationPacks.contains(invalidKey)) {
                m_warnedDecorationPacks.insert(invalidKey);
                qCWarning(lcOverlay) << "Surface decoration (" << surfacePath << "): pack" << packId
                                     << "has no valid fragment shader — skipping this chain stage";
            }
            continue;
        }
        // Audio-reactive pack in the chain -> this decoration slot wants the
        // live CAVA spectrum (gated below so a plain border never starts audio).
        chainWantsAudio = chainWantsAudio || effect.audio;
        // A pack that samples the scene behind the surface gets the desktop
        // wallpaper as a stand-in for it (see the backdrop write below).
        chainWantsBackdrop = chainWantsBackdrop || effect.needsBackdrop;
        const QVariantMap friendlyParams = allPackParams.value(packId).toMap();

        // Outer-margin request (the pack's declared paddingParam, e.g. glow's
        // glowSize): the per-surface override wins, else the param's declared
        // default — the same resolution the compositor's updateWindowDecoration
        // applies, with the chain's LARGEST request padding the shared canvas.
        // The QML host inflates the capture + shader items by this logical-px
        // margin so an outer effect gets real transparent room; 0 (a
        // margin-less chain) keeps the classic 1:1 geometry.
        outerPadding = qMax(outerPadding, PhosphorSurfaceShaders::paddingRequest(effect, friendlyParams));

        // Theme colour resolution: packs that opt into theme-derived colours
        // (border useThemeNeutral/useSystemAccent, glow/shadow useThemeTint) have
        // them synthesised into their friendly params here, before translation —
        // the flags are host-consumed and never reach the shader. Shared with the
        // KWin window-decoration path via resolveThemeParamColors so both resolve
        // identically. The daemon sources its theme colours from the live palette
        // (background / foreground) plus its accent settings; resolved on every
        // show, so a colour-scheme change is picked up on the next OSD.
        // m_settings is guaranteed non-null here — applyDecoration early-returns
        // above when it (or the registry) is null.
        QVariantMap resolvedParams = friendlyParams;
        PhosphorSurfaceShaders::resolveThemeParamColors(effect, resolvedParams,
                                                        {m_settings->highlightColor(), m_settings->inactiveColor(),
                                                         pal.color(QPalette::Active, QPalette::Window),
                                                         pal.color(QPalette::Active, QPalette::WindowText)});

        // Card corner radius: the popup slot publishes its card's design radius
        // (cardCornerRadius, a Kirigami-derived logical-px value). The decoration
        // rounds to the CARD, not a per-pack value, so every pack that declares a
        // cornerRadius (border, shadow, glow) is injected the same radius here and
        // their corners coincide. translateSurfaceParams only emits a lane for
        // packs whose metadata declares cornerRadius, so this is a no-op for any
        // pack without it. Slots that publish no cardCornerRadius (or a non-card
        // surface) fall back to the pack's own default.
        const QVariant cardRadius = slot->property(OverlayQmlPropertyNames::CardCornerRadius.data());
        if (cardRadius.isValid() && cardRadius.toReal() > 0.0) {
            resolvedParams.insert(QStringLiteral("cornerRadius"), cardRadius.toReal());
        }

        // The chain's silhouette, injected on the same terms as the radius above:
        // unconditionally, because translateSurfaceParams drops the key for a pack
        // that does not declare it. Invalid means no pack in the chain declared it.
        if (chainBottomCorners.isValid()) {
            resolvedParams.insert(PhosphorSurfaceShaders::roundBottomCornersParamId(), chainBottomCorners);
        }

        // Through the shared builder, so this host, the settings app's decoration
        // preview and the shell cannot describe a stage differently. A preview that
        // composed its own would stop predicting what the daemon draws.
        stages.append(PhosphorSurfaceShaders::composeStageMap(effect, resolvedParams, blurScale));
    }
    if (stages.isEmpty()) {
        clearDecoration();
        return;
    }
    // Same defensive cap as the compositor's wb.outerPadding, shared so the two
    // decoration composers cannot drift.
    outerPadding = qBound(0.0, outerPadding, static_cast<double>(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx));

    // Padding BEFORE the chain: the chain write is the load trigger, and the
    // single list write hands every stage's source + params to QML atomically,
    // so no stage ever bakes against a half-written sibling (the old
    // per-property protocol needed a clear-first + source-last dance for the
    // same guarantee).
    writeQmlProperty(slot, QString(OverlayQmlPropertyNames::DecorationOuterPadding), outerPadding);
    // Backdrop BEFORE the chain, for the same reason as the padding: the chain
    // write is the load trigger, so everything a stage reads on its first bake
    // has to be in place first.
    //
    // A daemon surface has no live scene behind it, so a needsBackdrop pack
    // (the glass / blur family) is handed the desktop wallpaper as a stand-in.
    // It is an approximation — it shows the wallpaper, not the windows actually
    // under the card — but it is the difference between a frosted OSD reading
    // as frosted glass and reading as a flat tint. Only resolved for a chain
    // that actually samples it; every other chain writes a null image, leaves
    // uHasBackdrop at 0, and behaves exactly as it did before.
    //
    // Loaded once into a local so an unresolvable wallpaper writes the SAME
    // invalid QVariant the no-backdrop arm and every hide/clear path write. A
    // valid QVariant holding a null QImage is not the same thing to the QML
    // side, which gates on the property being null or undefined, so it would
    // flip useWallpaper true with no pixels behind it.
    const QImage backdrop = chainWantsBackdrop ? PhosphorShaders::ShaderRegistry::loadWallpaperImage() : QImage();
    writeQmlProperty(slot, QString(OverlayQmlPropertyNames::BackdropTexture),
                     backdrop.isNull() ? QVariant() : QVariant::fromValue(backdrop));
    writeQmlProperty(slot, QString(OverlayQmlPropertyNames::DecorationChain), QVariant::fromValue(stages));
    // Every apply, so a slot decorated after a commit starts at the current value.
    writeQmlProperty(slot, QString(OverlayQmlPropertyNames::DecorationReloadGeneration), s_decorationReloadGeneration);

    // Record whether this slot now carries an audio-reactive pack, then reconcile
    // CAVA: a newly-decorated audio surface may need audio capture started, or a
    // change from audio to non-audio may let it wind down.
    // Every OSD slot is a QQuickItem (the show paths hand one down), so a
    // failed cast here means the caller passed something this function cannot
    // decorate at all. Say so rather than silently leaving the slot carrying
    // whatever audio flag a previous show set, which syncCavaState would then
    // act on.
    if (auto* item = qobject_cast<QQuickItem*>(slot)) {
        item->setProperty(OverlayQmlPropertyNames::WantsAudioDecoration.data(), chainWantsAudio);
        // Decoration is often applied while the slot is still hidden (popups
        // apply-then-show), so re-run syncCavaState whenever it shows/hides —
        // that starts CAVA once an audio surface becomes visible and stops it on
        // hide. UniqueConnection keeps re-decoration from stacking duplicates.
        connect(item, &QQuickItem::visibleChanged, this, &OverlayService::syncCavaState, Qt::UniqueConnection);
    } else {
        qCWarning(lcOverlay) << "Surface decoration (" << surfacePath
                             << "): slot is not a QQuickItem — its audio-reactive flag cannot be updated";
    }
    syncCavaState();
}

} // namespace PlasmaZones
