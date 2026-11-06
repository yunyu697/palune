#include "settings/Toml.h"

#include <QStringList>

#include <cmath>

namespace palune {
namespace Toml {

namespace {

struct Cursor {
    const QString &s;
    int i = 0;

    void skipSpace()
    {
        while (i < s.size() && (s.at(i) == QLatin1Char(' ') || s.at(i) == QLatin1Char('\t')))
            ++i;
    }

    bool atEnd() const { return i >= s.size(); }
    QChar peek() const { return i < s.size() ? s.at(i) : QChar(); }
};

QVariant parseValue(Cursor &c, QString *error);

QString parseQuoted(Cursor &c, QChar quote, QString *error)
{
    QString out;
    ++c.i; // 跳过起始引号
    while (!c.atEnd()) {
        const QChar ch = c.s.at(c.i);
        if (ch == quote) {
            ++c.i;
            return out;
        }
        if (quote == QLatin1Char('"') && ch == QLatin1Char('\\') && c.i + 1 < c.s.size()) {
            const QChar next = c.s.at(c.i + 1);
            switch (next.unicode()) {
            case 'n': out += QLatin1Char('\n'); break;
            case 't': out += QLatin1Char('\t'); break;
            case 'r': out += QLatin1Char('\r'); break;
            case '"': out += QLatin1Char('"'); break;
            case '\\': out += QLatin1Char('\\'); break;
            default: out += next; break;
            }
            c.i += 2;
            continue;
        }
        out += ch;
        ++c.i;
    }
    if (error)
        *error = QStringLiteral("字符串缺少结束引号");
    return {};
}

QVariant parseValue(Cursor &c, QString *error)
{
    c.skipSpace();
    if (c.atEnd()) {
        if (error) *error = QStringLiteral("缺少值");
        return {};
    }
    const QChar ch = c.peek();
    if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
        return parseQuoted(c, ch, error);
    if (ch == QLatin1Char('[')) {
        ++c.i;
        QVariantList list;
        while (true) {
            c.skipSpace();
            if (c.atEnd()) {
                if (error) *error = QStringLiteral("数组缺少 ]");
                return {};
            }
            if (c.peek() == QLatin1Char(']')) {
                ++c.i;
                return list;
            }
            if (c.peek() == QLatin1Char(',')) {
                ++c.i;
                continue;
            }
            QVariant value = parseValue(c, error);
            if (error && !error->isEmpty())
                return {};
            list << value;
        }
    }
    // 裸值：读到逗号/右括号/空白结束
    int start = c.i;
    while (!c.atEnd()) {
        const QChar v = c.peek();
        if (v == QLatin1Char(',') || v == QLatin1Char(']') || v == QLatin1Char(' ')
            || v == QLatin1Char('\t') || v == QLatin1Char('#'))
            break;
        ++c.i;
    }
    const QString raw = c.s.mid(start, c.i - start);
    if (raw == QLatin1String("true"))
        return true;
    if (raw == QLatin1String("false"))
        return false;
    bool okInt = false;
    const qlonglong iv = raw.toLongLong(&okInt);
    if (okInt)
        return QVariant::fromValue(iv);
    bool okDouble = false;
    const double dv = raw.toDouble(&okDouble);
    if (okDouble)
        return dv;
    if (error)
        *error = QStringLiteral("无法解析的值：%1").arg(raw);
    return {};
}

QString escape(const QString &value)
{
    QString out = value;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    out.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    out.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return out;
}

QString writeValue(const QVariant &value)
{
    if (value.typeId() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.typeId() == QMetaType::QStringList) {
        QStringList parts;
        for (const QString &item : value.toStringList())
            parts << QLatin1Char('"') + escape(item) + QLatin1Char('"');
        return QLatin1Char('[') + parts.join(QStringLiteral(", ")) + QLatin1Char(']');
    }
    if (value.typeId() == QMetaType::QVariantList) {
        QStringList parts;
        for (const QVariant &item : value.toList())
            parts << writeValue(item);
        return QLatin1Char('[') + parts.join(QStringLiteral(", ")) + QLatin1Char(']');
    }
    if (value.typeId() == QMetaType::QString || value.typeId() == QMetaType::QUrl)
        return QLatin1Char('"') + escape(value.toString()) + QLatin1Char('"');
    if (value.typeId() == QMetaType::Double) {
        const double d = value.toDouble();
        if (d == std::floor(d) && std::abs(d) < 1e15)
            return QString::number(static_cast<qlonglong>(d));
        return QString::number(d, 'g', 15);
    }
    return value.toString();
}

void splitKey(const QString &full, QString *section, QString *key)
{
    const int dot = full.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        *section = QString();
        *key = full;
    } else {
        *section = full.left(dot);
        *key = full.mid(dot + 1);
    }
}

} // namespace

QVariantMap parse(const QString &text, QString *error)
{
    QVariantMap values;
    QString section;
    QStringList lines = text.split(QLatin1Char('\n'));
    for (int lineNo = 1; lineNo <= lines.size(); ++lineNo) {
        Cursor c{lines.at(lineNo - 1)};
        c.skipSpace();
        if (c.atEnd() || c.peek() == QLatin1Char('#'))
            continue;
        if (c.peek() == QLatin1Char('[')) {
            const int close = c.s.indexOf(QLatin1Char(']'), c.i);
            if (close < 0) {
                if (error) *error = QStringLiteral("第 %1 行表头缺少 ]").arg(lineNo);
                return {};
            }
            section = c.s.mid(c.i + 1, close - c.i - 1).trimmed();
            continue;
        }
        int eq = -1;
        for (int i = c.i; i < c.s.size(); ++i) {
            if (c.s.at(i) == QLatin1Char('=')) {
                eq = i;
                break;
            }
            if (c.s.at(i) == QLatin1Char('#'))
                break;
        }
        if (eq < 0) {
            if (error) *error = QStringLiteral("第 %1 行缺少 =").arg(lineNo);
            return {};
        }
        QString key = c.s.mid(c.i, eq - c.i).trimmed();
        if (key.size() >= 2 && key.startsWith(QLatin1Char('"')) && key.endsWith(QLatin1Char('"')))
            key = key.mid(1, key.size() - 2);
        c.i = eq + 1;
        QString valueError;
        const QVariant value = parseValue(c, &valueError);
        if (!valueError.isEmpty()) {
            if (error)
                *error = QStringLiteral("第 %1 行：%2").arg(lineNo).arg(valueError);
            return {};
        }
        if (key.isEmpty()) {
            if (error) *error = QStringLiteral("第 %1 行键名为空").arg(lineNo);
            return {};
        }
        values.insert(section.isEmpty() ? key : section + QLatin1Char('.') + key, value);
    }
    if (error) *error = QString();
    return values;
}

QString serialize(const QVariantMap &values)
{
    QStringList sections;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        QString section, key;
        splitKey(it.key(), &section, &key);
        if (!sections.contains(section))
            sections << section;
    }
    sections.sort();

    QString out;
    bool first = true;
    for (const QString &section : sections) {
        if (!first)
            out += QLatin1Char('\n');
        first = false;
        if (!section.isEmpty())
            out += QLatin1Char('[') + section + QLatin1Char(']') + QLatin1Char('\n');
        QStringList keys;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            QString s, k;
            splitKey(it.key(), &s, &k);
            if (s == section)
                keys << k;
        }
        keys.sort();
        for (const QString &key : keys) {
            out += key + QStringLiteral(" = ")
                + writeValue(values.value(section.isEmpty() ? key : section + QLatin1Char('.') + key))
                + QLatin1Char('\n');
        }
    }
    return out;
}

} // namespace Toml
} // namespace palune
