// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace PhosphorShellLauncher {

// Answers arithmetic typed into the launcher: "2*(3+4)" yields a single
// row "14", and Enter copies it to the clipboard.
//
// A hand-written recursive-descent evaluator rather than a JS engine:
// the query is user input, and evaluating it as a script would be a
// footgun for the sake of saving a hundred lines. Supported: decimal and
// exponent literals, + - * / % ^, parentheses, unary minus, and the sqrt() and abs() functions.
//
// A bare number yields nothing ("5" is not a calculation, and a row
// reading "5" under every query that happens to be digits is noise); the
// query has to contain an operator, parentheses or a function call.
//
// The row scores above any fuzzy match (FuzzyMatcher::perfectScore for
// the query's length, plus one), since an exact answer to what the user
// typed is by definition the best result for it.
class PHOSPHORSHELLLAUNCHER_EXPORT CalculatorProvider : public PhosphorRegistry::ILauncherProvider
{
    Q_OBJECT

public:
    explicit CalculatorProvider(QObject* parent = nullptr);
    ~CalculatorProvider() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString iconName() const override;

    void setQuery(const QString& query) override;
    [[nodiscard]] QList<PhosphorRegistry::LauncherResult> results() const override;
    [[nodiscard]] bool activate(const QString& resultId, Activation activation) override;

    // The evaluator, public and static so it is testable without a
    // provider and reusable by anything else that wants safe arithmetic.
    // Returns nullopt for anything that is not a complete, well-formed
    // expression, for division by zero, and for a result that is not
    // finite.
    [[nodiscard]] static std::optional<double> evaluate(QStringView expression);

    // Whether `expression` is worth answering at all: parses, and is not
    // a bare literal. Public for the same reason as evaluate().
    [[nodiscard]] static bool isCalculation(QStringView expression);

    // The display form. An exact integer inside double's exact range prints
    // in FULL and without a decimal point, however many digits that takes;
    // everything else prints to at most 10 significant digits with trailing
    // zeros trimmed. Truncating an exact integer would be a wrong answer
    // rather than a rounded one.
    //
    // Always the C locale, even where the user's locale groups digits or
    // uses a decimal comma. This string is both what the row shows and what
    // activation copies, and a grouped "1 234,5" is not something that can be
    // pasted into a shell or another calculator. Localising the display would
    // mean carrying two strings; the pasteable one wins.
    //
    // The INPUT side is not symmetric: the parser accepts the reader's own
    // decimal separator as well as '.', so typing what a French or German
    // keyboard and locale produce is understood. Only the answer is
    // normalised.
    [[nodiscard]] static QString format(double value);

private:
    QString m_query;
    QList<PhosphorRegistry::LauncherResult> m_results;
    QString m_answer;
};

} // namespace PhosphorShellLauncher
