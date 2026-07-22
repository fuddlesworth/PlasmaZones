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

// ── Appearance: system-colour derivation (PhosphorConfig::Store-backed) ──────

void Settings::setUseSystemColors(bool use)
{
    if (useSystemColors() == use) {
        return;
    }
    m_store->write(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::useSystemKey(), use);
    if (use) {
        applySystemColorScheme();
    }
    Q_EMIT useSystemColorsChanged();
    Q_EMIT settingsChanged();
}

// ── Color helpers ────────────────────────────────────────────────────────────

QString Settings::loadColorsFromFile(const QString& filePath)
{
    ColorImportResult result = ColorImporter::importFromFile(filePath);
    if (!result.success) {
        return result.errorMessage;
    }
    setHighlightColor(result.highlightColor);
    setInactiveColor(result.inactiveColor);
    setBorderColor(result.borderColor);
    setLabelFontColor(result.labelFontColor);
    if (useSystemColors()) {
        setUseSystemColors(false);
    }
    return QString(); // Success - no error
}

void Settings::trackSystemPaletteChanges()
{
    // Track system palette changes at runtime. load() derives the zone
    // colors from the CURRENT QGuiApplication palette when useSystemColors
    // is on — a one-time snapshot. The palette changes underneath every
    // long-running process whenever the desktop color scheme changes
    // (wallpaper-driven schemes switch often), so without re-deriving, the
    // daemon's overlays and the settings app's previews render colors from
    // whichever scheme was active when the process started — they only
    // matched again after a daemon restart. Qt 6 delivers
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
    if (watched == qGuiApp && event->type() == QEvent::ApplicationPaletteChange && useSystemColors()) {
        // Derived values, not user edits. TWO mechanisms keep a runtime theme
        // switch from reading as unsaved changes:
        //  1. m_applyingSystemPalette (RAII, restored even if a setter throws)
        //     is up for the whole synchronous re-derive, so
        //     SettingsController::onSettingsPropertyChanged() — wired to every
        //     color NOTIFY — sees isApplyingSystemPalette() and skips
        //     setNeedsSave(true), keeping the global footer quiet.
        //  2. rebaselineDerivedColorKeys() refreshes the committed baseline so
        //     value-based isKeyModified() checks stay false afterwards. See
        //     that function for why ONLY this path rebaselines — and it is
        //     further gated here: when the useSystemColors toggle itself is a
        //     PENDING unsaved edit, the toggle and the colors it derived must
        //     stay discardable together, so the baseline is left alone.
        QScopedValueRollback<bool> applying(m_applyingSystemPalette, true);
        // Batch the announcement like load(): snapshot the four derived
        // colors, run the derive with the per-setter NOTIFY + settingsChanged
        // emissions squelched, then emit each changed NOTIFY exactly once plus
        // ONE aggregate settingsChanged. Without the squelch a single palette
        // event fired up to 4 settingsChanged (one per color setter), each of
        // which re-ran every aggregate consumer (daemon refreshConfigFrom-
        // Settings, KWin effect reload) mid-derive with partially-applied
        // colors. The explicit emissions stay INSIDE the m_applyingSystemPalette
        // scope so NOTIFY-driven dirty tracking still sees the flag up.
        //
        // Known (theoretical) misclassification: the derive window is
        // synchronous, but a NOTIFY handler above could re-enter a color
        // setter with a genuine user edit; that write lands while the squelch
        // flag is up, so it is announced by THIS batch (fine) yet also counted
        // as palette-derived by the rebaseline below (its baseline entry would
        // absorb the user's value). No in-tree handler writes colors from a
        // color NOTIFY, so this stays a documented hazard rather than code.
        const QColor highlightBefore = highlightColor();
        const QColor inactiveBefore = inactiveColor();
        const QColor borderBefore = borderColor();
        const QColor labelFontBefore = labelFontColor();
        {
            QScopedValueRollback<bool> squelch(m_suppressDerivedColorEmissions, true);
            applySystemColorScheme();
        }
        const bool highlightChanged = highlightColor() != highlightBefore;
        const bool inactiveChanged = inactiveColor() != inactiveBefore;
        const bool borderChanged = borderColor() != borderBefore;
        const bool labelFontChanged = labelFontColor() != labelFontBefore;
        if (highlightChanged)
            Q_EMIT highlightColorChanged();
        if (inactiveChanged)
            Q_EMIT inactiveColorChanged();
        if (borderChanged)
            Q_EMIT borderColorChanged();
        if (labelFontChanged)
            Q_EMIT labelFontColorChanged();
        if (highlightChanged || inactiveChanged || borderChanged || labelFontChanged)
            Q_EMIT settingsChanged();
        if (!isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::useSystemKey())) {
            rebaselineDerivedColorKeys();
        }
    }
    return ISettings::eventFilter(watched, event);
}

void Settings::applySystemColorScheme()
{
    // QPalette respects QT_QPA_PLATFORMTHEME — on non-KDE desktops, Qt reads
    // the platform theme (qt6ct, gnome, lxqt) to populate the palette.
    const QPalette pal = QGuiApplication::palette();

    QColor highlight = pal.color(QPalette::Active, QPalette::Highlight);
    highlight.setAlpha(::PhosphorZones::ZoneDefaults::HighlightAlpha);
    setHighlightColor(highlight);

    // Inactive fill and border derive from the background family, not Text.
    // Text-at-alpha renders as a washed grey film on every dark scheme (the
    // same fabrication the QML side eliminated); AlternateBase is the View
    // alternate surface, and Mid is the palette's separator-grade shade, so
    // both follow the active color scheme with the intended emphasis.
    QColor inactive = pal.color(QPalette::Active, QPalette::AlternateBase);
    inactive.setAlpha(::PhosphorZones::ZoneDefaults::InactiveAlpha);
    setInactiveColor(inactive);

    QColor border = pal.color(QPalette::Active, QPalette::Mid);
    border.setAlpha(::PhosphorZones::ZoneDefaults::BorderAlpha);
    setBorderColor(border);

    const QColor fontColor = pal.color(QPalette::Active, QPalette::Text);
    setLabelFontColor(fontColor);
}

} // namespace PlasmaZones
