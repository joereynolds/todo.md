#pragma once

#include <QString>
#include <QStringList>

// A filter query (the filter box language) split into the parts the filter
// builder offers as choices, and back. Terms it doesn't model are kept, as
// typed, in `other`, so a round trip never loses anything.
//
//   "#music|#video due:overdue|due:week is:todo ~<=15m report"
//     tags {music, video} (any), due {overdue, week}, states {todo}, eta "<=15m",
//     other {report}
struct Query {
    QStringList tags;     // without '#', as written
    bool allTags = false; // true: "#a #b" (every tag); false: "#a|#b" (any). Only meaningful for 2+ tags
    QStringList due;      // overdue, today, week, none, any — any of them
    QStringList states;   // todo, doing, done, cancelled, open — any of them
    QStringList done;     // today, yesterday, week, any — any of them
    QString eta;          // one ETA condition without '~' ("<=15m", "none"); empty = no condition
    QStringList other;    // everything else, in order

    static Query parse(const QString &text);
    QString toString() const; // tags, due, states, done, eta, then the other terms
};
