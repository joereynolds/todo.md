#include "query.h"

#include "parser.h"

namespace {

// "due:overdue|due:week" with prefix "due:" -> {overdue, week}; false unless every
// alternative has the prefix and a known value.
bool keyedAlternatives(const QStringList &alts, QLatin1String prefix, std::initializer_list<const char *> known,
                       QStringList *out)
{
    QStringList values;
    for (const QString &a : alts) {
        if (!a.startsWith(prefix, Qt::CaseInsensitive))
            return false;
        const QString v = a.mid(prefix.size()).toLower();
        bool ok = false;
        for (const char *k : known)
            ok = ok || v == QLatin1String(k);
        if (!ok)
            return false;
        if (!values.contains(v))
            values << v;
    }
    *out = values;
    return true;
}

bool isWholeTag(const QString &term)
{
    const QStringView tag = parser::tagOf(term);
    return !tag.isEmpty() && tag.size() + 1 == term.size();
}

} // namespace

Query Query::parse(const QString &text)
{
    Query q;
    QStringList singleTags, orTags;
    QStringList singleTagTerms; // as typed, in case they end up in `other`
    QString orTagTerm;
    bool haveDue = false, haveStates = false, haveDone = false;
    for (const QString &term : text.split(u' ', Qt::SkipEmptyParts)) {
        const QStringList alts = term.split(u'|', Qt::SkipEmptyParts);
        if (alts.isEmpty()) {
            q.other << term;
            continue;
        }
        if (std::all_of(alts.cbegin(), alts.cend(), isWholeTag)) {
            if (alts.size() == 1) {
                singleTags << alts.front().mid(1);
                singleTagTerms << term;
            } else if (orTags.isEmpty()) {
                for (const QString &a : alts)
                    orTags << a.mid(1);
                orTagTerm = term;
            } else {
                q.other << term;
            }
            continue;
        }
        QStringList values;
        if (!haveDue && keyedAlternatives(alts, QLatin1String("due:"), {"overdue", "today", "week", "none", "any"}, &values)) {
            q.due = values;
            haveDue = true;
        } else if (!haveStates && keyedAlternatives(alts, QLatin1String("is:"), {"todo", "doing", "done", "cancelled", "open"}, &values)) {
            q.states = values;
            haveStates = true;
        } else if (!haveDone && keyedAlternatives(alts, QLatin1String("done:"), {"today", "yesterday", "week", "any"}, &values)) {
            q.done = values;
            haveDone = true;
        } else if (alts.size() == 1 && term.size() > 1 && term.front() == u'~' && q.eta.isEmpty()) {
            q.eta = term.mid(1);
        } else {
            q.other << term;
        }
    }
    if (orTags.isEmpty()) {
        q.tags = singleTags;
        q.allTags = singleTags.size() > 1;
    } else {
        q.tags = orTags;
        q.allTags = false;
        // "#a|#b #c" mixes any and all: keep the single ones as typed.
        for (int i = singleTagTerms.size() - 1; i >= 0; --i)
            q.other.prepend(singleTagTerms[i]);
    }
    return q;
}

QString Query::toString() const
{
    QStringList terms;
    if (!tags.isEmpty()) {
        QStringList t;
        for (const QString &tag : tags)
            t << u'#' + tag;
        if (allTags)
            terms << t;
        else
            terms << t.join(u'|');
    }
    auto keyed = [&](const QStringList &values, const QString &prefix) {
        if (values.isEmpty())
            return;
        QStringList t;
        for (const QString &v : values)
            t << prefix + v;
        terms << t.join(u'|');
    };
    keyed(due, QStringLiteral("due:"));
    keyed(states, QStringLiteral("is:"));
    keyed(done, QStringLiteral("done:"));
    if (!eta.isEmpty())
        terms << u'~' + eta;
    terms << other;
    return terms.join(u' ');
}
