// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "tilinghandler/tilinghandler.h"

#include <PhosphorProtocol/ClientHelpers.h>

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QTimer>

// The compositor-drawn scrolling tab indicators' PAINT settings: the loaders
// that mirror the daemon's Scrolling.TabIndicator style / gaps / radius /
// colours and its five own font keys into the effect's cached members, the
// change funnel that rebuilds the pills when any of them moves, and the
// label-font builder. Split from daemon_settings.cpp (which hosts
// loadCachedSettings and the ~50 other loaders) for the file-size ceiling;
// loadCachedSettings calls loadScrollTabIndicatorSettings() in the same
// position the block used to sit.
namespace PlasmaZones {

void PlasmaZonesEffect::loadScrollTabIndicatorSettings()
{
    // ── Scrolling tab indicator PAINT settings ──
    //
    // The effect draws the tab pills itself, so it needs the paint half of
    // Scrolling.TabIndicator. These are the GLOBAL keys, pulled over the
    // settings channel below; per-screen rule overrides for the same keys
    // arrive separately, pushed by the daemon through
    // TilingAdaptor::setScrollTabPaintOverrides (src/daemon/daemon/scrolling.cpp).
    // Neither reaches the QML overlay, which has no tab surface at all. The
    // geometry half never comes here either: it reaches the engine through
    // IScrollSettings and is already baked into the committed column rects the
    // effect paints against.
    //
    // Defaults live on the members (see the header block) and are the
    // ConfigDefaults::scrollingTabIndicator*() accessors in
    // src/config/configdefaults_scrolling.h. Every loader type-guards, because
    // an older daemon answering an unknown key replies VALID-but-empty: a
    // bare toBool()/toInt() would silently turn the master switch off or
    // collapse the style to chips for the duration of the skew. A guarded
    // reply simply leaves the shipped default standing.
    //
    // Change-gated: only a real value change queues a rebuild, since
    // loadCachedSettings re-runs in full on EVERY settingsChanged and an
    // ungated assignment would cost a full rebuild per unrelated setting edit.
    //
    // Four numeric ranges are guarded below, but only THREE of them are
    // ConfigDefaults {Min,Max} pairs duplicated by hand beside each guard
    // (gaps 0..64, corner radius -1..64, font weight 100..900) — the effect
    // cannot include the daemon's config headers, and no shared phosphor-*
    // header carries them, so a bound change there is a change here too. The
    // fourth, the style, is a closed set mirrored from
    // ConfigDefaults::isValidScrollingTabIndicatorStyle rather than a pair.
    // The same four ranges are guarded once more in
    // kwin-effect/tilinghandler/scrolltabs.cpp for the per-screen context
    // overrides, which ride a different channel; that file spells the style as
    // a kStyleMin/kStyleMax pair, so it declares four pairs where this one has
    // three.
    loadSettingAsync(QStringLiteral("scrollingTabIndicatorEnabled"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool enabled = v.toBool();
        if (m_cachedTabIndicatorEnabled != enabled) {
            m_cachedTabIndicatorEnabled = enabled;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("scrollingTabIndicatorStyle"), [this](const QVariant& v) {
        bool ok = false;
        const int style = v.toInt(&ok);
        // Closed set mirrored from ConfigDefaults::isValidScrollingTabIndicatorStyle:
        // 0 = title chips, 1 = segment bar. Anything else is skew, not a style.
        if (!ok || (style != 0 && style != 1)) {
            return;
        }
        if (m_cachedTabIndicatorStyle != style) {
            m_cachedTabIndicatorStyle = style;
            onScrollTabIndicatorStyleChanged();
        }
    });

    // Duplicates ConfigDefaults::scrollingTabIndicatorGapsBetweenTabs{Min,Max}() (0..64).
    loadSettingAsync(QStringLiteral("scrollingTabIndicatorGapsBetweenTabs"), [this](const QVariant& v) {
        bool ok = false;
        const int gaps = v.toInt(&ok);
        if (!ok || gaps < 0 || gaps > 64) {
            return;
        }
        if (m_cachedTabIndicatorGapsBetweenTabs != gaps) {
            m_cachedTabIndicatorGapsBetweenTabs = gaps;
            onScrollTabIndicatorStyleChanged();
        }
    });

    // Duplicates ConfigDefaults::scrollingTabIndicatorCornerRadius{Min,Max}() (-1..64).
    // The floor is the -1 "fully rounded" sentinel
    // (ConfigDefaults::scrollingTabIndicatorCornerRadiusPill), which the painter
    // resolves against the tab's short extent, so it must survive the clamp.
    loadSettingAsync(QStringLiteral("scrollingTabIndicatorCornerRadius"), [this](const QVariant& v) {
        bool ok = false;
        const int radius = v.toInt(&ok);
        if (!ok || radius < -1 || radius > 64) {
            return;
        }
        if (m_cachedTabIndicatorCornerRadius != radius) {
            m_cachedTabIndicatorCornerRadius = radius;
            onScrollTabIndicatorStyleChanged();
        }
    });

    // Tab colours are theme-fallback keys: the daemon marshals them as the raw
    // stored string, and EMPTY means "follow the theme". An empty reply must
    // therefore land as an INVALID QColor (the painter's own sentinel), NOT be
    // rejected like a malformed one — otherwise clearing a colour on the
    // settings page would leave the old concrete colour painted until relog.
    // A non-empty string that QColor cannot parse IS malformed and is dropped.
    // The slot is captured as a POINTER, not as the reference parameter by
    // reference: the inner callback outlives this lambda's invocation (it
    // runs when the D-Bus reply lands), and a reference captured by reference
    // formally names an entity whose lifetime has ended by then. The pointee
    // is a member of `this`, which the callback already keeps alive.
    const auto loadTabColor = [this](const QString& key, QColor* slot) {
        loadSettingAsync(key, [this, slot](const QVariant& v) {
            const QString s = v.toString();
            const QColor c = s.isEmpty() ? QColor() : QColor(s);
            if (!s.isEmpty() && !c.isValid()) {
                return;
            }
            if (*slot != c) {
                *slot = c;
                onScrollTabIndicatorStyleChanged();
            }
        });
    };
    loadTabColor(QStringLiteral("scrollingTabIndicatorActiveColor"), &m_cachedTabIndicatorActiveColor);
    loadTabColor(QStringLiteral("scrollingTabIndicatorInactiveColor"), &m_cachedTabIndicatorInactiveColor);
    loadTabColor(QStringLiteral("scrollingTabIndicatorUrgentColor"), &m_cachedTabIndicatorUrgentColor);

    // Label font. The pills carry their OWN font keys
    // (Scrolling.TabIndicator), not the Appearance label font the zone-label
    // overlays use: the chip label lives inside a pill whose thickness the
    // user sets in the same settings group, so tying it to the global label
    // font meant one slider silently resized text in two unrelated places.
    //
    // There is deliberately NO size key here. The pill's Width setting drives
    // its thickness, and the raster fits the label to that thickness per
    // indicator (see scrolltabindicatorpainter_raster.cpp), so Width IS the
    // size control and a second one could only disagree with it.
    // Duplicates ConfigDefaults::scrollingTabIndicatorFontWeight{Min,Max}()
    // (100..900).
    loadSettingAsync(QStringLiteral("scrollingTabIndicatorFontFamily"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::QString) {
            return;
        }
        const QString family = v.toString();
        if (m_cachedTabIndicatorFontFamily != family) {
            m_cachedTabIndicatorFontFamily = family;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("scrollingTabIndicatorFontWeight"), [this](const QVariant& v) {
        bool ok = false;
        const int weight = v.toInt(&ok);
        if (!ok || weight < 100 || weight > 900) {
            return;
        }
        if (m_cachedTabIndicatorFontWeight != weight) {
            m_cachedTabIndicatorFontWeight = weight;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("scrollingTabIndicatorFontItalic"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool italic = v.toBool();
        if (m_cachedTabIndicatorFontItalic != italic) {
            m_cachedTabIndicatorFontItalic = italic;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("scrollingTabIndicatorFontUnderline"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool underline = v.toBool();
        if (m_cachedTabIndicatorFontUnderline != underline) {
            m_cachedTabIndicatorFontUnderline = underline;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("scrollingTabIndicatorFontStrikeout"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool strikeout = v.toBool();
        if (m_cachedTabIndicatorFontStrikeout != strikeout) {
            m_cachedTabIndicatorFontStrikeout = strikeout;
            onScrollTabIndicatorStyleChanged();
        }
    });
}

void PlasmaZonesEffect::onScrollTabIndicatorStyleChanged()
{
    // The handler caches the pills' theme palette, units and LABEL FONT, and
    // the font is built from the members these loaders just wrote, so the
    // cache is stale the moment any of them moved.
    m_tilingHandler->invalidateScrollTabTheme();
    // The painter's model carries the style by value, so a change has to be
    // pushed through a rebuild; the rebuild damages exactly the pill rects.
    // COALESCED: loadCachedSettings re-runs whole on every settingsChanged,
    // so a settings-page apply lands several tab-key replies in one turn, and
    // each would otherwise rebuild every screen synchronously (a JSON
    // re-parse and a model push per screen). One queued rebuild per burst,
    // the same latch shape the decoration loaders use (scheduleBorderSweep).
    // m_tilingHandler is constructed before the first settings load can run
    // (the handler is a constructor-time member), so no null guard is needed.
    if (m_tabIndicatorRebuildPending) {
        return;
    }
    m_tabIndicatorRebuildPending = true;
    QTimer::singleShot(0, this, [this] {
        m_tabIndicatorRebuildPending = false;
        m_tilingHandler->rebuildAllScrollTabIndicators();
    });
}

QFont PlasmaZonesEffect::scrollTabIndicatorFont() const
{
    // Only the TYPEFACE half of the label font is decided here: family,
    // weight, italic, underline, strikeout. The SIZE the returned font carries
    // is never the size a label is DRAWN at — the raster re-sizes the label to
    // fit each chip's thickness before it draws
    // (scrolltabindicatorpainter_raster.cpp), because the pill's Width setting
    // is what the user sizes these labels with. The segment-bar style draws no
    // text at all, so for it the size is not read even that far.
    //
    // The starting point is QFontDatabase's SmallestReadableFont, the same
    // system font Kirigami's desktop platform plugin hands out as
    // Kirigami.Theme.smallFont (the "Small" font on Plasma's Fonts page). It
    // supplies the family when the user has not named one, and its size SEEDS
    // the raster's fit: fitLabelFont measures this font's height ratio at its
    // own pixel size to land a first guess, then only ever walks down from
    // there. A readable system size therefore keeps that first guess close,
    // and a wildly different one would only cost extra steps of the walk.
    QFont font = QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont);
    // Drop the inherited style NAME before the five settings are applied. A
    // system font can arrive carrying one ("Book", "Regular", "Display"), and
    // Qt matches a named style in preference to weight and italic, which would
    // make the setWeight/setItalic below resolve to whatever face that name
    // picks instead. Clearing it puts this font back on the weight-and-flags
    // path the settings expect. On a system whose font names no style this is
    // a no-op; where it does name one, the shipped 700 finally applies and the
    // labels render bold.
    font.setStyleName(QString());
    if (!m_cachedTabIndicatorFontFamily.isEmpty()) {
        font.setFamily(m_cachedTabIndicatorFontFamily);
    }
    // The loader already constrains the weight to the 100..900 band, so the
    // member is always a valid QFont::Weight value.
    font.setWeight(static_cast<QFont::Weight>(m_cachedTabIndicatorFontWeight));
    font.setItalic(m_cachedTabIndicatorFontItalic);
    font.setUnderline(m_cachedTabIndicatorFontUnderline);
    font.setStrikeOut(m_cachedTabIndicatorFontStrikeout);
    return font;
}

} // namespace PlasmaZones
