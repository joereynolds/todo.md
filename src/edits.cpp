#include "edits.h"

#include <algorithm>

LineBuffer LineBuffer::fromData(const QByteArray &data)
{
    LineBuffer b;
    if (data.isEmpty())
        return b;
    b.lines = data.split('\n');
    if (data.endsWith('\n'))
        b.lines.removeLast();
    else
        b.trailingNewline = false;
    b.crlf = !b.lines.isEmpty() && b.lines.first().endsWith('\r');
    return b;
}

QByteArray LineBuffer::toData() const
{
    QByteArray out = lines.join('\n');
    if (trailingNewline && !lines.isEmpty())
        out += '\n';
    return out;
}

static QString lineText(const QByteArray &l)
{
    QByteArrayView v(l);
    if (v.endsWith('\r'))
        v.chop(1);
    return QString::fromUtf8(v);
}

ApplyResult applyEdits(LineBuffer &buf, std::vector<Edit> edits)
{
    ApplyResult res;
    for (Edit &e : edits) {
        if (e.line < 0)
            e.line = int(buf.lines.size());
    }
    // Bottom-up, so earlier line numbers stay valid while applying. At the same
    // line, edits that consume lines go before pure insertions (which would
    // otherwise push the consumed lines down), and insertions go last-first so
    // they end up in list order.
    std::vector<size_t> order(edits.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const Edit &x = edits[a], &y = edits[b];
        if (x.line != y.line)
            return x.line > y.line;
        if (x.before.size() != y.before.size())
            return x.before.size() > y.before.size();
        return a > b;
    });
    std::vector<Edit> sorted;
    sorted.reserve(edits.size());
    for (size_t i : order)
        sorted.push_back(std::move(edits[i]));
    edits = std::move(sorted);

    std::vector<Edit> applied;
    qsizetype limit = buf.lines.size();
    for (Edit &e : edits) {
        const qsizetype k = e.before.size();
        bool ok = e.line + k <= limit;
        for (qsizetype i = 0; ok && i < k; ++i)
            ok = lineText(buf.lines[e.line + i]) == e.before[i];
        if (!ok) {
            ++res.conflicts;
            continue;
        }
        buf.lines.remove(e.line, k);
        for (qsizetype i = 0; i < e.after.size(); ++i) {
            QByteArray l = e.after[i].toUtf8();
            if (buf.crlf)
                l += '\r';
            buf.lines.insert(e.line + i, l);
        }
        limit = e.line;
        applied.push_back(e);
    }

    // An edit's final position is shifted by the size changes of every edit above it.
    std::reverse(applied.begin(), applied.end());
    int shift = 0;
    for (const Edit &e : applied) {
        res.inverse.push_back({e.path, e.line + shift, e.after, e.before});
        shift += int(e.after.size() - e.before.size());
    }
    res.applied = int(applied.size());
    return res;
}
