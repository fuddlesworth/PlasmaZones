// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QStringList>

namespace PhosphorScrollEngine {
class ScrollEngine;
}

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for the scrolling placement engine
 *
 * Provides D-Bus interface: org.plasmazones.Scrolling
 *
 * The scroll-SPECIFIC wire surface: the scrolling screen set the KWin
 * effect uses as its Mode-stamp discriminator, and the home for future
 * columnar methods. Window lifecycle and tile-request traffic for
 * scrolling screens deliberately stays on org.plasmazones.Tiling — the
 * effect keeps ONE engine-managed screen set and one geometry pipeline
 * for both tiling-family engines, and TilingAdaptor routes per screen.
 */
class PLASMAZONES_EXPORT ScrollingAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Scrolling")

    Q_PROPERTY(QStringList scrollingScreens READ scrollingScreens NOTIFY scrollingScreensChanged)

public:
    explicit ScrollingAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent = nullptr);
    ~ScrollingAdaptor() override = default;

    /// The scrolling screen set, sorted so a property read and a signal
    /// payload for the same set compare equal.
    ///
    /// This is the RAW set. The KWin effect does not answer its Mode
    /// discriminator from it directly: it intersects this with the
    /// engine-managed union published on org.plasmazones.Tiling at read
    /// time, because the two arrive as independent signals with no
    /// ordering guarantee and this set can transiently name a screen the
    /// union has already dropped.
    QStringList scrollingScreens() const;

    /// Clear the engine pointer during shutdown (same late-D-Bus-call
    /// contract as the sibling adaptors' clearEngine).
    void clearEngine();

public Q_SLOTS:
    /**
     * @brief Focus the adjacent column on a scrolling screen
     *
     * The KWin effect's Meta+wheel axis shortcut calls this with the
     * cursor's screen. Gated on the engine actually owning @p screenId —
     * the engine's own screen fallback would otherwise redirect a wheel
     * event from a non-scrolling monitor onto the active scrolling one.
     *
     * Every rejection is a SILENT no-op, not an error reply: an empty
     * @p screenId, a screen the engine does not own, and any @p delta
     * other than -1 or +1 all return without acting, so a caller cannot
     * distinguish a refusal from a call that landed on an empty strip.
     *
     * @param screenId Screen whose strip should move (the cursor's screen);
     *                 an empty string is ignored
     * @param delta -1 focuses the column to the left, +1 to the right; any
     *              other value is ignored
     */
    void focusColumn(const QString& screenId, int delta);

    /**
     * @brief The strip as it currently looks on a screen, for previews
     *
     * Returns a JSON array with ONE OBJECT PER TILE carrying
     * {x, y, width, height} plus zoneNumber, the 1-based visible tile slot
     * the rect occupies in strip order. The rects are 0.0 to 1.0 per axis,
     * normalized against the screen's FULL geometry — the tiles themselves
     * are clipped to the gap-inset work area, so the fractions show the
     * panel gap, and a consumer maps back to pixels by scaling against the
     * screen rectangle. Same basis as the daemon's own OSD strip card.
     *
     * Excluded and unnumbered: hidden tabs of a tabbed column, minimized
     * tiles, parked columns, and tiles whose intersection with the work
     * area is EMPTY (a stack whose minimum heights overflow the work area
     * resolves its tail below the bottom edge). Partially visible columns
     * are clipped rather than dropped, with no minimum-visibility
     * threshold, so an arbitrarily thin sliver still carries its number.
     *
     * The settings app renders it where the other modes show a layout
     * thumbnail. Empty array when the screen has no strip or is not
     * scrolling.
     *
     * @param screenId Screen whose strip to describe
     * @return JSON array string
     */
    QString visibleStripJson(const QString& screenId) const;

    /**
     * @brief The screen's effective preset vocabulary, for inspection
     *
     * Returns a JSON object {"columnWidths": [...], "windowHeights": [...]}
     * of 0.0 to 1.0 fractions. Each list resolves independently: a list the
     * context's resolved scrolling template supplies is that template's own
     * preset list, and a list it does not supply falls back to the configured
     * preset list, so a template that defines widths but no heights yields
     * template widths beside the configured heights. This is the vocabulary
     * the cycle-preset-width and cycle-preset-height shortcuts walk on that
     * screen. Same silent ownership gate as focusColumn: an empty object when
     * the screen is not scrolling.
     *
     * @param screenId Screen whose vocabulary to describe
     * @return JSON object string
     */
    QString presetVocabularyJson(const QString& screenId) const;

Q_SIGNALS:
    /**
     * @brief Emitted when the set of screens using the scrolling engine changes
     * @param screenIds List of screen IDs currently in scrolling mode
     */
    void scrollingScreensChanged(const QStringList& screenIds);

private:
    PhosphorScrollEngine::ScrollEngine* m_engine = nullptr;
    /// Last set broadcast on the bus (the change gate's memory; the engine
    /// re-emits identical sets on desktop switches for the tiling channel).
    QStringList m_lastBroadcastScreens;
};

} // namespace PlasmaZones
