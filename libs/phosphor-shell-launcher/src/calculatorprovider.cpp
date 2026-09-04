// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/CalculatorProvider.h>

#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QLocale>

#include <cmath>

namespace PhosphorShellLauncher {

using PhosphorRegistry::LauncherResult;

namespace {

// The number scanner and the C-locale conversion must agree on what a digit
// is. QChar::isDigit() is true for Arabic-Indic, Devanagari and fullwidth
// digits, which QLocale::c().toDouble() then rejects, so the literal would be
// consumed and the whole expression thrown away.
bool isAsciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

// Bound on nested sub-expressions. The query comes from a text field with no
// length cap, and each nesting level costs several stack frames across
// expr/term/factor/unary/primary, so an unbounded parse of a pasted string of
// open parens would overflow the stack of a shell process that is expected to
// live for the whole session. Far beyond any expression a person types.
constexpr int kMaxParseDepth = 128;

// Recursive descent over:
//   expr    := term (('+' | '-') term)*
//   term    := factor (('*' | '/' | '%') factor)*
//   factor  := unary ('^' factor)?            -- right-associative
//   unary   := ('-' | '+') unary | primary
//   primary := number | '(' expr ')' | ident '(' expr ')'
class Parser
{
public:
    explicit Parser(QStringView text)
        : m_text(text)
    {
    }

    // The whole input must be consumed: "2+2 apples" is not an expression
    // with trailing noise, it is not an expression.
    std::optional<double> run()
    {
        skipSpace();
        const auto value = expr();
        skipSpace();
        if (!value || m_pos != m_text.size()) {
            return std::nullopt;
        }
        return value;
    }

    bool sawOperator() const
    {
        return m_operators > 0;
    }

private:
    // Decrements on every exit path, of which expr() has several.
    struct DepthGuard
    {
        explicit DepthGuard(int& d)
            : depth(d)
        {
        }
        ~DepthGuard()
        {
            --depth;
        }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;
        DepthGuard(DepthGuard&&) = delete;
        DepthGuard& operator=(DepthGuard&&) = delete;
        int& depth;
    };

    void skipSpace()
    {
        while (m_pos < m_text.size() && m_text[m_pos].isSpace()) {
            ++m_pos;
        }
    }

    bool accept(QChar c)
    {
        skipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    std::optional<double> expr()
    {
        // Every nesting cycle passes through here, so one guard bounds the
        // whole grammar. Past the limit the input is simply not an
        // expression, which is the same answer the parser gives any other
        // malformed query.
        if (m_depth >= kMaxParseDepth) {
            return std::nullopt;
        }
        ++m_depth;
        const DepthGuard guard(m_depth);
        auto left = term();
        if (!left) {
            return std::nullopt;
        }
        for (;;) {
            if (accept(u'+')) {
                const auto right = term();
                if (!right) {
                    return std::nullopt;
                }
                ++m_operators;
                *left += *right;
            } else if (accept(u'-')) {
                const auto right = term();
                if (!right) {
                    return std::nullopt;
                }
                ++m_operators;
                *left -= *right;
            } else {
                return left;
            }
        }
    }

    std::optional<double> term()
    {
        auto left = factor();
        if (!left) {
            return std::nullopt;
        }
        for (;;) {
            if (accept(u'*')) {
                const auto right = factor();
                if (!right) {
                    return std::nullopt;
                }
                ++m_operators;
                *left *= *right;
            } else if (accept(u'/')) {
                const auto right = factor();
                if (!right || *right == 0.0) {
                    return std::nullopt;
                }
                ++m_operators;
                *left /= *right;
            } else if (accept(u'%')) {
                const auto right = factor();
                if (!right || *right == 0.0) {
                    return std::nullopt;
                }
                ++m_operators;
                *left = std::fmod(*left, *right);
            } else {
                return left;
            }
        }
    }

    std::optional<double> factor()
    {
        const auto base = unary();
        if (!base) {
            return std::nullopt;
        }
        if (accept(u'^')) {
            const auto exponent = factor();
            if (!exponent) {
                return std::nullopt;
            }
            ++m_operators;
            return std::pow(*base, *exponent);
        }
        return base;
    }

    std::optional<double> unary()
    {
        if (accept(u'-')) {
            const auto v = unary();
            if (!v) {
                return std::nullopt;
            }
            return -*v;
        }
        if (accept(u'+')) {
            return unary();
        }
        return primary();
    }

    std::optional<double> primary()
    {
        skipSpace();
        if (accept(u'(')) {
            const auto v = expr();
            if (!v || !accept(u')')) {
                return std::nullopt;
            }
            ++m_operators; // parentheses make it a calculation
            return v;
        }
        if (m_pos < m_text.size() && m_text[m_pos].isLetter()) {
            return function();
        }
        return number();
    }

    std::optional<double> function()
    {
        const qsizetype start = m_pos;
        while (m_pos < m_text.size() && m_text[m_pos].isLetter()) {
            ++m_pos;
        }
        const QStringView name = m_text.mid(start, m_pos - start);
        if (!accept(u'(')) {
            return std::nullopt;
        }
        const auto arg = expr();
        if (!arg || !accept(u')')) {
            return std::nullopt;
        }
        ++m_operators;
        // Case-insensitive, because every other part of the launcher is.
        // "SQRT(4)" being a parse error while "sqrt(4)" works is a
        // distinction the user has no way to predict.
        if (name.compare(u"sqrt", Qt::CaseInsensitive) == 0) {
            return *arg < 0 ? std::nullopt : std::optional<double>(std::sqrt(*arg));
        }
        if (name.compare(u"abs", Qt::CaseInsensitive) == 0) {
            return std::abs(*arg);
        }
        return std::nullopt;
    }

    std::optional<double> number()
    {
        const qsizetype start = m_pos;
        bool digits = false;
        while (m_pos < m_text.size() && isAsciiDigit(m_text[m_pos])) {
            ++m_pos;
            digits = true;
        }
        // The locale's own decimal separator is accepted alongside '.', so
        // a user who types what their keyboard and their locale produce
        // ("3,5" in French) gets an answer rather than a parse failure.
        const QChar localPoint =
            QLocale::system().decimalPoint().isEmpty() ? QChar(u'.') : QLocale::system().decimalPoint().front();
        if (m_pos < m_text.size() && (m_text[m_pos] == u'.' || m_text[m_pos] == localPoint)) {
            ++m_pos;
            while (m_pos < m_text.size() && isAsciiDigit(m_text[m_pos])) {
                ++m_pos;
                digits = true;
            }
        }
        if (!digits) {
            return std::nullopt;
        }
        // Exponent: only if a digit follows, so "2e" is a parse error
        // rather than 2.
        if (m_pos < m_text.size() && (m_text[m_pos] == u'e' || m_text[m_pos] == u'E')) {
            qsizetype p = m_pos + 1;
            if (p < m_text.size() && (m_text[p] == u'+' || m_text[p] == u'-')) {
                ++p;
            }
            if (p < m_text.size() && isAsciiDigit(m_text[p])) {
                m_pos = p;
                while (m_pos < m_text.size() && isAsciiDigit(m_text[m_pos])) {
                    ++m_pos;
                }
            }
        }
        bool ok = false;
        // Normalised to the C form before conversion, so one code path
        // handles both separators.
        QString literal = m_text.mid(start, m_pos - start).toString();
        literal.replace(localPoint, u'.');
        const double v = QLocale::c().toDouble(literal, &ok);
        if (!ok) {
            return std::nullopt;
        }
        return v;
    }

    QStringView m_text;
    qsizetype m_pos = 0;
    int m_operators = 0;
    int m_depth = 0;
};

} // namespace

CalculatorProvider::CalculatorProvider(QObject* parent)
    : ILauncherProvider(parent)
{
}

CalculatorProvider::~CalculatorProvider() = default;

QString CalculatorProvider::id() const
{
    return QStringLiteral("calculator");
}

QString CalculatorProvider::displayName() const
{
    return QCoreApplication::translate("PhosphorShellLauncher", "Calculator");
}

QString CalculatorProvider::iconName() const
{
    return QStringLiteral("accessories-calculator");
}

std::optional<double> CalculatorProvider::evaluate(QStringView expression)
{
    Parser parser(expression);
    const auto v = parser.run();
    if (!v || !std::isfinite(*v)) {
        // Note this folds two outcomes into one: input that is not an
        // expression, and an expression that overflowed to infinity.
        // setQuery separates the second so it can say so rather than showing
        // nothing. Division by zero is a third case and stays here: term()
        // refuses to produce a value for it, which is the documented
        // contract, so it never reaches the isfinite test.
        return std::nullopt;
    }
    return v;
}

bool CalculatorProvider::isCalculation(QStringView expression)
{
    Parser parser(expression);
    const auto v = parser.run();
    return v.has_value() && std::isfinite(*v) && parser.sawOperator();
}

QString CalculatorProvider::format(double value)
{
    // The C locale, deliberately, for the reason the header gives: this one
    // string is both what the row shows and what activation copies, and a
    // grouped or comma-pointed answer is not something that can be pasted
    // back into a shell, a field, or this calculator. The INPUT side accepts
    // the reader's own separator (see number()), so a user who types "3,5"
    // is understood even though the answer comes back with a point.
    //
    // Integers (within double's exact range) print without a point;
    // everything else with up to 10 significant digits, trailing zeros
    // trimmed by 'g'.
    if (std::abs(value) < 1e15 && value == std::floor(value)) {
        return QLocale::c().toString(static_cast<qlonglong>(value));
    }
    return QLocale::c().toString(value, 'g', 10);
}

void CalculatorProvider::setQuery(const QString& query)
{
    const QList<LauncherResult> previous = std::move(m_results);
    m_results.clear();
    m_answer.clear();

    // ONE parse. isCalculation and evaluate each build
    // their own Parser over the same text, and the out-of-range branch
    // built a third, so the old shape parsed two or three times per
    // keystroke to answer questions one run already knows.
    Parser parser(query);
    const auto value = parser.run();
    const bool sawOperator = parser.sawOperator();
    const bool answerable = value.has_value() && std::isfinite(*value) && sawOperator;

    if (answerable) {
        {
            const auto v = value;
            m_answer = format(*v);
            LauncherResult r;
            r.id = QStringLiteral("answer");
            r.title = m_answer;
            r.subtitle = query.trimmed();
            r.iconName = iconName();
            // An exact answer to what was typed outranks any fuzzy match.
            r.score = FuzzyMatcher::perfectScore(static_cast<int>(query.size())) + 1;
            r.primaryActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Copy");
            m_results.append(std::move(r));
        }
    } else if (value.has_value() && !std::isfinite(*value) && sawOperator) {
        // It WAS an expression, it just has no representable answer: 2^5000
        // overflowing to infinity. Say so, rather than showing nothing and
        // leaving the user to guess whether the syntax was wrong. No action
        // label and no answer, so activation refuses.
        //
        // Division by zero does NOT reach here. term() refuses to produce a
        // value for it at all, which is the documented contract, so it looks
        // to this branch exactly like text that is not an expression and
        // yields no row. That is a real gap: "1/0" is a question the user
        // asked and gets silence. Closing it means the parser distinguishing
        // "refused" from "not an expression", which is a contract change
        // rather than a branch here.
        LauncherResult r;
        r.id = QStringLiteral("answer");
        r.title = QCoreApplication::translate("PhosphorShellLauncher", "Result is out of range");
        r.subtitle = query.trimmed();
        r.iconName = iconName();
        r.score = FuzzyMatcher::perfectScore(static_cast<int>(query.size())) + 1;
        m_results.append(std::move(r));
    }
    // Conditional, like every sibling provider: an unconditional emission
    // costs the model a full reset on every keystroke that changes nothing.
    if (m_results == previous) {
        return;
    }
    Q_EMIT resultsChanged();
}

QList<LauncherResult> CalculatorProvider::results() const
{
    return m_results;
}

bool CalculatorProvider::activate(const QString& resultId, Activation activation)
{
    if (activation != Activation::Primary || resultId != QLatin1String("answer") || m_answer.isEmpty()) {
        return false;
    }
    // Copying needs a GUI application for the clipboard. Under a
    // QCoreApplication (a headless test, a CLI) there is nothing to copy
    // to, which is a refusal, not a crash.
    //
    // qobject_cast, NOT the qGuiApp macro: qGuiApp is an unchecked
    // static_cast of QCoreApplication::instance(), so under a plain
    // QCoreApplication it is non-null and ->clipboard() dereferences
    // GUI-private state that does not exist. That segfaulted the
    // headless test the first time this was written the "obvious" way.
    auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    QClipboard* clipboard = gui ? gui->clipboard() : nullptr;
    if (!clipboard) {
        return false;
    }
    clipboard->setText(m_answer);
    return true;
}

} // namespace PhosphorShellLauncher
