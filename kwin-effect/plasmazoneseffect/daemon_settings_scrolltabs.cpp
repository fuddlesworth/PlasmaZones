// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "tilinghandler/tilinghandler.h"

#include <PhosphorProtocol/ClientHelpers.h>

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QTimer>
#include <QtNumeric>

// The compositor-drawn scrolling tab indicators' PAINT settings: the loaders
// that mirror the daemon's Scrolling.TabIndicator style / gaps / radius /
// colours and the six Appearance label-font keys into the effect's cached
// members, the change funnel that rebuilds the pills when any of them moves,
// and the label-font builder. Split from daemon_settings.cpp (which hosts
// loadCachedSettings and the ~50 other loaders) for the file-size ceiling;
// loadCachedSettings calls loadScrollTabIndicatorSettings() in the same
// position the block used to sit.
namespace PlasmaZones {

void PlasmaZonesEffect::loadScrollTabIndicatorSettings()
{
    // ── Scrolling tab indicator PAINT settings ──
    //
    // The effect draws the tab pills itself, so it needs the paint half of
    // Scrolling.TabIndicator that the daemon otherwise pushes onto the QML
    // overlay. The geometry half never comes here: it reaches the engine
    // through IScrollSettings and is already baked into the committed column
    // rects the effect paints against.
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
    // The numeric bounds below DUPLICATE, by hand, the ConfigDefaults
    // {Min,Max} accessors named beside each (the effect cannot include the
    // daemon's config headers, and no shared phosphor-* header carries these
    // four pairs); a bound change there is a change here too. The same four
    // pairs are repeated once more in kwin-effect/tilinghandler/scrolltabs.cpp
    // for the per-screen context overrides, which ride a different channel.
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

    // Label font, the same six Appearance keys writeFontProperties
    // (src/daemon/overlayservice/internal.h) pushes onto the QML overlay, so a
    // compositor-drawn chip label matches every other PlasmaZones label.
    // Duplicates ConfigDefaults::labelFontSizeScale{Min,Max}() (0.25..3.0) and
    // labelFontWeight{Min,Max}() (100..900).
    loadSettingAsync(QStringLiteral("labelFontFamily"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::QString) {
            return;
        }
        const QString family = v.toString();
        if (m_cachedLabelFontFamily != family) {
            m_cachedLabelFontFamily = family;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("labelFontSizeScale"), [this](const QVariant& v) {
        bool ok = false;
        const double scale = v.toDouble(&ok);
        if (!ok || !qIsFinite(scale) || scale < 0.25 || scale > 3.0) {
            return;
        }
        if (!qFuzzyCompare(m_cachedLabelFontSizeScale, scale)) {
            m_cachedLabelFontSizeScale = scale;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("labelFontWeight"), [this](const QVariant& v) {
        bool ok = false;
        const int weight = v.toInt(&ok);
        if (!ok || weight < 100 || weight > 900) {
            return;
        }
        if (m_cachedLabelFontWeight != weight) {
            m_cachedLabelFontWeight = weight;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("labelFontItalic"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool italic = v.toBool();
        if (m_cachedLabelFontItalic != italic) {
            m_cachedLabelFontItalic = italic;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("labelFontUnderline"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool underline = v.toBool();
        if (m_cachedLabelFontUnderline != underline) {
            m_cachedLabelFontUnderline = underline;
            onScrollTabIndicatorStyleChanged();
        }
    });

    loadSettingAsync(QStringLiteral("labelFontStrikeout"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool strikeout = v.toBool();
        if (m_cachedLabelFontStrikeout != strikeout) {
            m_cachedLabelFontStrikeout = strikeout;
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
    // Kirigami.Theme.smallFont, exactly: Kirigami's desktop platform plugin
    // hands out QFontDatabase's SmallestReadableFont system font (the "Small"
    // font on Plasma's Fonts page), so reading the same database entry here
    // gives the pills the size the QML rendering had, with no Kirigami link.
    // The base pixel size is measured through QFontInfo because the system
    // font is point-sized and the user's scale applies to the resolved pixel
    // height, as the QML's `smallFont.pixelSize * fontSizeScale` did.
    const QFont base = QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont);
    const int basePixelSize = qMax(1, QFontInfo(base).pixelSize());

    QFont font = base;
    if (!m_cachedLabelFontFamily.isEmpty()) {
        font.setFamily(m_cachedLabelFontFamily);
    }
    font.setPixelSize(qMax(1, qRound(basePixelSize * m_cachedLabelFontSizeScale)));
    // The loader already constrains the weight to the 100..900 band, so the
    // member is always a valid QFont::Weight value.
    font.setWeight(static_cast<QFont::Weight>(m_cachedLabelFontWeight));
    font.setItalic(m_cachedLabelFontItalic);
    font.setUnderline(m_cachedLabelFontUnderline);
    font.setStrikeOut(m_cachedLabelFontStrikeout);
    return font;
}

} // namespace PlasmaZones
