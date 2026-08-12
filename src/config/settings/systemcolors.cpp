// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/colorimporter.h"
#include "config/configdefaults.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QScopedValueRollback>
#include <QThread>

namespace PlasmaZones {

// ── Appearance: palette-following colours ────────────────────────────────────
// The four zone-colour keys (and the scrolling drop indicator's two, which
// share the machinery) are theme-fallback strings: EMPTY means "follow the
// system palette". Resolution lives here, in the getters' shared helper,
// so a palette change needs no writes, no baseline bookkeeping, and no
// squelch flags — the old useSystemColors machinery that WROTE derived
// colours into config is gone with the bool itself.

// ── Color helpers ────────────────────────────────────────────────────────────

QString Settings::loadColorsFromFile(const QString& filePath)
{
    ColorImportResult result = ColorImporter::importFromFile(filePath);
    if (!result.success) {
        return result.errorMessage;
    }
    // The QColor setters store concrete #AARRGGBB strings, which is exactly
    // what "stop following the palette" means now — no mode bool to flip.
    setHighlightColor(result.highlightColor);
    setInactiveColor(result.inactiveColor);
    setBorderColor(result.borderColor);
    setLabelFontColor(result.labelFontColor);
    return QString(); // Success - no error
}

void Settings::trackSystemPaletteChanges()
{
    // Track system palette changes at runtime so bindings on the resolved
    // colour getters refresh when the desktop colour scheme changes
    // (wallpaper-driven schemes switch often). Qt 6 delivers
    // QEvent::ApplicationPaletteChange to the application object; there is
    // no signal for it, hence the event filter. Guarded: the config
    // library is also used by non-GUI tools where qGuiApp is null.
    //
    // Cost note: the filter is installed on the application object, so it
    // sees EVERY event delivered in the process; the leading guard in
    // eventFilter() keeps the per-event cost to two compares (watched
    // pointer + event type). That per-event tax also scales with instance
    // count — Settings must remain a per-process near-singleton.
    //
    // Thread note: installEventFilter() requires the filter object and the
    // filtered object to live on the same thread, and qGuiApp lives on the
    // main (GUI) thread — so Settings must be constructed on the main thread
    // for palette tracking to engage. Every current composition root does
    // that; the guard below turns a future regression into a loud warning
    // (with palette tracking disabled) instead of a Qt-internal one.
    if (qGuiApp) {
        if (thread() != qGuiApp->thread()) {
            qCWarning(lcConfig) << "Settings constructed off the main thread — system palette tracking disabled "
                                   "(installEventFilter requires same-thread objects)";
            return;
        }
        // Seed the colour-scheme token from the live palette so the first
        // ApplicationPaletteChange only announces a REAL flip, not the
        // startup read.
        m_lastColorSchemeToken = Settings::systemColorSchemeToken();
        qGuiApp->installEventFilter(this);
        // Seed the change gate so the first ApplicationPaletteChange can
        // compare against what the process started with.
        const QColor drop = resolvedSystemColor(SystemColorRole::DropIndicator);
        m_paletteBaseline = {resolvedSystemColor(SystemColorRole::Highlight),
                             resolvedSystemColor(SystemColorRole::Inactive),
                             resolvedSystemColor(SystemColorRole::Border),
                             resolvedSystemColor(SystemColorRole::LabelFont),
                             drop,
                             drop};
    }
}

QString Settings::systemColorSchemeToken()
{
    // Window-background lightness is the scheme discriminator the desktop
    // itself uses (a dark scheme is one whose surfaces are dark); the
    // midpoint split matches how portals classify light vs dark. Empty when
    // there is no GUI application — the match field then stays inert.
    //
    // Off the GUI thread it degrades the same way, and for the same reason
    // resolvedSystemColor below refuses: QGuiApplication::palette() is not
    // documented thread-safe, so reading it here would race the GUI thread.
    // The degraded answer differs from that sibling's on purpose — a colour
    // has a defensible constant to fall back on, a colour SCHEME does not, and
    // empty is already this accessor's honest "cannot classify" answer.
    if (!qGuiApp || QThread::currentThread() != qGuiApp->thread()) {
        return QString();
    }
    const int lightness = QGuiApplication::palette().color(QPalette::Active, QPalette::Window).lightness();
    return lightness < 128 ? QStringLiteral("dark") : QStringLiteral("light");
}

bool Settings::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qGuiApp && event->type() == QEvent::ApplicationPaletteChange) {
        // ColorScheme match-field feed: derive the light/dark token and
        // announce a flip. Runs ahead of the colour fan-out below and outside
        // its value gates — this is not a config value, so it must not touch
        // dirty tracking, and the field must fire even when every zone colour
        // is pinned to a concrete value.
        const QString schemeToken = Settings::systemColorSchemeToken();
        if (schemeToken != m_lastColorSchemeToken) {
            m_lastColorSchemeToken = schemeToken;
            Q_EMIT systemColorSchemeChanged();
        }
        // Re-announce whichever colours FOLLOW the palette (stored sentinel
        // empty) AND actually moved. Qt delivers ApplicationPaletteChange
        // after the palette is already updated, so the "before" values come
        // from the cached baseline rather than a re-read — without the value
        // gate every palette event (a style change, a plasma-integration
        // re-push with identical roles) would run the aggregate consumers
        // (daemon refreshConfigFromSettings, KWin effect reload) for
        // nothing. The resolved getters read the new palette on the next
        // call, so this is purely a NOTIFY fan-out: nothing is written,
        // dirty tracking never engages, and a colour the user pinned to a
        // concrete value stays silent. One aggregate settingsChanged,
        // batched like the setters, so aggregate consumers run once per
        // theme switch rather than six times.
        const QColor newHighlight = resolvedSystemColor(SystemColorRole::Highlight);
        const QColor newInactive = resolvedSystemColor(SystemColorRole::Inactive);
        const QColor newBorder = resolvedSystemColor(SystemColorRole::Border);
        const QColor newLabelFont = resolvedSystemColor(SystemColorRole::LabelFont);
        // The scrolling drop indicator's two keys ride the same fan-out: they
        // are theme-fallback keys resolved in their getters exactly like the
        // quartet, so a theme switch moves their resolved view with nothing
        // written. One resolve for both, since they share the role.
        const QColor newDrop = resolvedSystemColor(SystemColorRole::DropIndicator);
        const bool highlightMoved = highlightColorRaw().isEmpty() && newHighlight != m_paletteBaseline[0];
        const bool inactiveMoved = inactiveColorRaw().isEmpty() && newInactive != m_paletteBaseline[1];
        const bool borderMoved = borderColorRaw().isEmpty() && newBorder != m_paletteBaseline[2];
        const bool labelFontMoved = labelFontColorRaw().isEmpty() && newLabelFont != m_paletteBaseline[3];
        const bool dropMoved = scrollingDropIndicatorColorRaw().isEmpty() && newDrop != m_paletteBaseline[4];
        const bool dropBorderMoved =
            scrollingDropIndicatorBorderColorRaw().isEmpty() && newDrop != m_paletteBaseline[5];
        // Refresh the baseline unconditionally (pinned colours included) so
        // a later un-pin compares against the current palette, not a stale
        // capture.
        m_paletteBaseline = {newHighlight, newInactive, newBorder, newLabelFont, newDrop, newDrop};
        // The NOTIFYs below are palette-driven, not user edits; the flag lets
        // SettingsController::onSettingsPropertyChanged() keep the
        // unsaved-changes footer quiet through a theme switch. RAII so an
        // emission handler that throws cannot leave it stuck up.
        QScopedValueRollback<bool> announcing(m_announcingPaletteChange, true);
        if (highlightMoved)
            Q_EMIT highlightColorChanged();
        if (inactiveMoved)
            Q_EMIT inactiveColorChanged();
        if (borderMoved)
            Q_EMIT borderColorChanged();
        if (labelFontMoved)
            Q_EMIT labelFontColorChanged();
        if (dropMoved)
            Q_EMIT scrollingDropIndicatorColorChanged();
        if (dropBorderMoved)
            Q_EMIT scrollingDropIndicatorBorderColorChanged();
        if (highlightMoved || inactiveMoved || borderMoved || labelFontMoved || dropMoved || dropBorderMoved)
            Q_EMIT settingsChanged();
    }
    return ISettings::eventFilter(watched, event);
}

QColor Settings::resolvedSystemColor(SystemColorRole role)
{
    // Headless config tools have no palette to follow; serve the shipped
    // constants so resolution stays deterministic there. An off-main-thread
    // caller degrades the same way: QGuiApplication::palette() is not
    // documented thread-safe, and trackSystemPaletteChanges() already
    // tolerates off-main-thread Settings by disabling tracking — this keeps
    // the read side equally safe instead of racing the GUI thread. Note the
    // degraded value is deliberately WRONG-but-safe (the constants, not the
    // live palette); no in-tree caller reads these colours off-thread, and
    // one that starts to will get stable colours rather than a race.
    if (!qGuiApp || QThread::currentThread() != qGuiApp->thread()) {
        switch (role) {
        case SystemColorRole::Highlight:
            return ConfigDefaults::highlightFallbackColor();
        case SystemColorRole::Inactive:
            return ConfigDefaults::inactiveFallbackColor();
        case SystemColorRole::Border:
            return ConfigDefaults::borderFallbackColor();
        case SystemColorRole::LabelFont:
            return ConfigDefaults::labelFontFallbackColor();
        case SystemColorRole::DropIndicator:
            return ConfigDefaults::scrollingDropIndicatorFallbackColor();
        }
        return ConfigDefaults::highlightFallbackColor();
    }

    // QPalette respects QT_QPA_PLATFORMTHEME — on non-KDE desktops, Qt reads
    // the platform theme (qt6ct, gnome, lxqt) to populate the palette.
    const QPalette pal = QGuiApplication::palette();
    switch (role) {
    case SystemColorRole::Highlight: {
        QColor highlight = pal.color(QPalette::Active, QPalette::Highlight);
        highlight.setAlpha(::PhosphorZones::ZoneDefaults::HighlightAlpha);
        return highlight;
    }
    // Inactive fill and border derive from the background family, not Text.
    // Text-at-alpha renders as a washed grey film on every dark scheme (the
    // same fabrication the QML side eliminated); AlternateBase is the View
    // alternate surface, and Mid is the palette's separator-grade shade, so
    // both follow the active color scheme with the intended emphasis.
    case SystemColorRole::Inactive: {
        QColor inactive = pal.color(QPalette::Active, QPalette::AlternateBase);
        inactive.setAlpha(::PhosphorZones::ZoneDefaults::InactiveAlpha);
        return inactive;
    }
    case SystemColorRole::Border: {
        QColor border = pal.color(QPalette::Active, QPalette::Mid);
        border.setAlpha(::PhosphorZones::ZoneDefaults::BorderAlpha);
        return border;
    }
    case SystemColorRole::LabelFont:
        return pal.color(QPalette::Active, QPalette::Text);
    // Same palette role as Highlight but forced to FULL alpha. The drop
    // indicator's fill takes its alpha from the opacity slider, which replaces
    // whatever the colour carries, and its border has no slider — so any alpha
    // below 255 would show up there as an unset border quietly drawing half
    // transparent. The setAlpha is what guarantees that, not the absence of
    // ZoneDefaults' HighlightAlpha: a platform theme is free to ship a
    // translucent Highlight, and the two no-palette fallbacks
    // (ConfigDefaults::scrollingDropIndicatorFallbackColor and the
    // isettings_detail twin) both force 255 the same way.
    case SystemColorRole::DropIndicator: {
        QColor drop = pal.color(QPalette::Active, QPalette::Highlight);
        drop.setAlpha(255);
        return drop;
    }
    }
    return pal.color(QPalette::Active, QPalette::Highlight);
}

} // namespace PlasmaZones
