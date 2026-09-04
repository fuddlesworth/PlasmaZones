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
// exponent literals, + - * / % ^, parentheses, unary minus, and sqrt().
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

    // The display form: up to 10 significant digits, integers without a
    // decimal point, in the C locale so "1000" never becomes "1,000" in
    // a field the user may paste into a shell.
    [[nodiscard]] static QString format(double value);

private:
    QString m_query;
    QList<PhosphorRegistry::LauncherResult> m_results;
    QString m_answer;
};

} // namespace PhosphorShellLauncher
