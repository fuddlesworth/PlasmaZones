// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/PixelUnits.h>

#include <QTest>
#include <QVariantList>
#include <QVariantMap>
#include <QtNumeric>

using namespace PhosphorShaders;

namespace {

QVariantMap paramInfo(const QString& id, const QString& unit, const QVariant& def)
{
    return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("unit"), unit}, {QStringLiteral("default"), def}};
}

/// One px length, one plain number, in the shape a registry hands to QML.
QVariantList twoParams()
{
    return QVariantList{paramInfo(QStringLiteral("blurRadius"), QStringLiteral("px"), 24.0),
                        paramInfo(QStringLiteral("tintStrength"), QString(), 0.15)};
}

} // namespace

class TestPixelUnits : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// The reason the whole thing exists: a preview a quarter the size of the
    /// real surface shows a quarter-size length, so the effect reads as the
    /// same effect rather than one four times too coarse.
    void scales_a_px_parameter_and_leaves_the_rest()
    {
        QVariantMap values{{QStringLiteral("blurRadius"), 24.0}, {QStringLiteral("tintStrength"), 0.15}};
        scalePixelParams(twoParams(), values, 0.25);
        QCOMPARE(values.value(QStringLiteral("blurRadius")).toDouble(), 6.0);
        QCOMPARE(values.value(QStringLiteral("tintStrength")).toDouble(), 0.15);
    }

    /// The ordinary case for a browser thumbnail, which edits nothing: an
    /// absent px parameter is INSERTED at its scaled default. Skipping it would
    /// leave the pack's own unscaled default to be filled in downstream, which
    /// is exactly the value being corrected.
    void inserts_a_scaled_default_for_an_absent_px_parameter()
    {
        QVariantMap values;
        scalePixelParams(twoParams(), values, 0.5);
        QCOMPARE(values.value(QStringLiteral("blurRadius")).toDouble(), 12.0);
        // A unitless parameter is still left to the pipeline's own default
        // handling — this function speaks only for lengths.
        QVERIFY(!values.contains(QStringLiteral("tintStrength")));
    }

    /// The runtime path. A surface drawn at its real size must come through
    /// byte-identical, so nothing here can restyle a real window.
    void a_unit_factor_changes_nothing()
    {
        const QVariantMap before{{QStringLiteral("blurRadius"), 24.0}};
        QVariantMap values = before;
        scalePixelParams(twoParams(), values, 1.0);
        QCOMPARE(values, before);
    }

    /// A host mid-layout reports a zero width, and a broken one can produce
    /// worse. Every unusable factor means "leave it alone" — flattening every
    /// length to zero would blank a page of previews for a frame.
    void an_unusable_factor_changes_nothing()
    {
        const QVariantMap before{{QStringLiteral("blurRadius"), 24.0}};
        for (const double factor : {0.0, -2.0, qQNaN(), qInf()}) {
            QVariantMap values = before;
            scalePixelParams(twoParams(), values, factor);
            QCOMPARE(values, before);
        }
    }

    /// A px parameter whose value is not a number is a pack authoring fault.
    /// It is left exactly as the caller had it rather than being guessed at or
    /// dropped, so the parameter pipeline downstream still reports its own
    /// type faults against the value the author actually wrote.
    void a_non_numeric_px_value_is_left_alone()
    {
        const QVariantList infos{paramInfo(QStringLiteral("blurRadius"), QStringLiteral("px"), 24.0)};
        QVariantMap values{{QStringLiteral("blurRadius"), QStringLiteral("wide")}};
        scalePixelParams(infos, values, 0.5);
        QCOMPARE(values.value(QStringLiteral("blurRadius")).toString(), QStringLiteral("wide"));
    }

    /// Only `px` opts in. An unknown unit is not a length this code knows how
    /// to reinterpret, and quietly scaling it would corrupt a pack that used
    /// the field for something else.
    void an_unknown_unit_is_not_scaled()
    {
        const QVariantList infos{paramInfo(QStringLiteral("angle"), QStringLiteral("deg"), 45.0)};
        QVariantMap values{{QStringLiteral("angle"), 45.0}};
        scalePixelParams(infos, values, 0.25);
        QCOMPARE(values.value(QStringLiteral("angle")).toDouble(), 45.0);
    }

    /// Values a caller passed for parameters the shader never declared belong
    /// to the caller; this function must not curate the map.
    void unknown_entries_survive()
    {
        QVariantMap values{{QStringLiteral("legacyKnob"), 3.0}};
        scalePixelParams(twoParams(), values, 0.5);
        QCOMPARE(values.value(QStringLiteral("legacyKnob")).toDouble(), 3.0);
    }

    /// An `int` px parameter keeps its fractional scaled value. The uniform is
    /// a float on the GPU, and rounding a 14px cell at quarter scale to 4 (or
    /// to 0) would put back, in the small, the mismatch this removes.
    void an_int_px_parameter_keeps_its_fraction()
    {
        const QVariantList infos{paramInfo(QStringLiteral("cellSize"), QStringLiteral("px"), 14)};
        QVariantMap values;
        scalePixelParams(infos, values, 0.25);
        QCOMPARE(values.value(QStringLiteral("cellSize")).toDouble(), 3.5);
    }
};

QTEST_MAIN(TestPixelUnits)
#include "test_pixelunits.moc"
