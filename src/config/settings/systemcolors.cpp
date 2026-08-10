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

namespace PlasmaZones {

// ── Appearance: palette-following zone colours ───────────────────────────────
// The four zone-colour keys are theme-fallback strings: EMPTY means "follow
// the system palette". Resolution lives here, in the getters' shared helper,
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
        qGuiApp->installEventFilter(this);
    }
}

bool Settings::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qGuiApp && event->type() == QEvent::ApplicationPaletteChange) {
        // Re-announce whichever colours FOLLOW the palette (stored sentinel
        // empty). The resolved getters read the new palette on the next call,
        // so this is purely a NOTIFY fan-out: nothing is written, dirty
        // tracking never engages, and a colour the user pinned to a concrete
        // value stays silent. One aggregate settingsChanged, batched like the
        // setters, so aggregate consumers (daemon refreshConfigFromSettings,
        // KWin effect reload) run once per theme switch rather than four
        // times.
        const bool highlightFollows = highlightColorRaw().isEmpty();
        const bool inactiveFollows = inactiveColorRaw().isEmpty();
        const bool borderFollows = borderColorRaw().isEmpty();
        const bool labelFontFollows = labelFontColorRaw().isEmpty();
        // The NOTIFYs below are palette-driven, not user edits; the flag lets
        // SettingsController::onSettingsPropertyChanged() keep the
        // unsaved-changes footer quiet through a theme switch. RAII so an
        // emission handler that throws cannot leave it stuck up.
        QScopedValueRollback<bool> announcing(m_announcingPaletteChange, true);
        if (highlightFollows)
            Q_EMIT highlightColorChanged();
        if (inactiveFollows)
            Q_EMIT inactiveColorChanged();
        if (borderFollows)
            Q_EMIT borderColorChanged();
        if (labelFontFollows)
            Q_EMIT labelFontColorChanged();
        if (highlightFollows || inactiveFollows || borderFollows || labelFontFollows)
            Q_EMIT settingsChanged();
    }
    return ISettings::eventFilter(watched, event);
}

QColor Settings::resolvedSystemColor(SystemColorRole role)
{
    // Headless config tools have no palette to follow; serve the shipped
    // constants so resolution stays deterministic there.
    if (!qGuiApp) {
        switch (role) {
        case SystemColorRole::Highlight:
            return ConfigDefaults::highlightColor();
        case SystemColorRole::Inactive:
            return ConfigDefaults::inactiveColor();
        case SystemColorRole::Border:
            return ConfigDefaults::borderColor();
        case SystemColorRole::LabelFont:
            return ConfigDefaults::labelFontColor();
        }
        return ConfigDefaults::highlightColor();
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
    }
    return pal.color(QPalette::Active, QPalette::Highlight);
}

} // namespace PlasmaZones
