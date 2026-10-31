#include "parser.h"

#include <QFile>
#include <QHash>

#include <algorithm>
#include <cstring>

namespace parser {

namespace {

bool stateFromChar(char16_t c, TaskState *state)
{
    switch (c) {
    case ' ': *state = TaskState::Todo; return true;
    case '/': *state = TaskState::Doing; return true;
    case 'x':
    case 'X': *state = TaskState::Done; return true;
    case '-': *state = TaskState::Cancelled; return true;
    default: return false;
    }
}

inline bool isSpace(QChar c) { return c == u' ' || c == u'\t'; }

// "!2026-12-25", "+2026-12-25", "x2026-12-25", "-2026-12-25"
bool dateToken(QStringView tok, char16_t *kind, QDate *date)
{
    if (tok.size() != 11)
        return false;
    const char16_t k = tok[0].unicode();
    if (k != '!' && k != '+' && k != 'x' && k != '-')
        return false;
    if (tok[5] != u'-' || tok[8] != u'-')
        return false;
    int v[11];
    for (int i : {1, 2, 3, 4, 6, 7, 9, 10}) {
        const char16_t c = tok[i].unicode();
        if (c < '0' || c > '9')
            return false;
        v[i] = c - '0';
    }
    const QDate d(v[1] * 1000 + v[2] * 100 + v[3] * 10 + v[4], v[6] * 10 + v[7], v[9] * 10 + v[10]);
    if (!d.isValid())
        return false;
    *kind = k;
    *date = d;
    return true;
}

// Removes tokens matching `pred`, along with the whitespace before them.
template<typename Pred>
QString removeTokens(const QString &content, Pred pred)
{
    struct Span { qsizetype start, end; };
    std::vector<Span> remove;
    const qsizetype n = content.size();
    for (qsizetype i = 0; i < n;) {
        while (i < n && isSpace(content[i]))
            ++i;
        const qsizetype st = i;
        while (i < n && !isSpace(content[i]))
            ++i;
        if (st == i)
            break;
        if (pred(QStringView(content).mid(st, i - st)))
            remove.push_back({st, i});
    }
    if (remove.empty())
        return content;
    QString out = content;
    for (auto it = remove.rbegin(); it != remove.rend(); ++it) {
        qsizetype a = it->start, b = it->end;
        if (a > 0) {
            while (a > 0 && isSpace(out[a - 1]))
                --a;
        } else {
            while (b < out.size() && isSpace(out[b]))
                ++b;
        }
        out.remove(a, b - a);
    }
    return out;
}

auto dateKinds(QStringView kinds)
{
    return [kinds](QStringView tok) {
        char16_t k;
        QDate d;
        return dateToken(tok, &k, &d) && kinds.contains(QChar(k));
    };
}

bool etaToken(QStringView tok)
{
    return tok.size() >= 3 && tok[0] == u'~' && parseEta(tok.mid(1)) >= 0;
}

QString appendToken(const QString &content, const QString &token)
{
    if (content.isEmpty() || isSpace(content.back()))
        return content + token;
    return content + u' ' + token;
}

} // namespace

QStringView tagOf(QStringView tok)
{
    if (tok.size() < 2 || tok[0] != u'#')
        return {};
    qsizetype i = 1;
    bool digitsOnly = true;
    for (; i < tok.size(); ++i) {
        const QChar c = tok[i];
        if (c.isDigit())
            continue;
        if (!c.isLetterOrNumber() && c != u'_' && c != u'-' && c != u'/')
            break;
        digitsOnly = false;
    }
    if (i == 1 || digitsOnly) // "#", "#123" (issue numbers aren't tags)
        return {};
    return tok.mid(1, i - 1);
}

qint64 parseEta(QStringView t)
{
    // 1-9 digits followed by a unit.
    if (t.size() < 2 || t.size() > 10)
        return -1;
    qint64 n = 0;
    for (qsizetype i = 0; i + 1 < t.size(); ++i) {
        const char16_t c = t[i].unicode();
        if (c < '0' || c > '9')
            return -1;
        n = n * 10 + (c - '0');
    }
    switch (t.back().unicode()) {
    case 'm': return n;
    case 'h': return n * 60;
    case 'd': return n * 60 * 24;
    case 'w': return n * 60 * 24 * 7;
    case 'y': return n * 60 * 24 * 365;
    default: return -1;
    }
}

QChar stateChar(TaskState s)
{
    switch (s) {
    case TaskState::Todo: return u' ';
    case TaskState::Doing: return u'/';
    case TaskState::Done: return u'x';
    case TaskState::Cancelled: return u'-';
    }
    return u' ';
}

bool splitTaskLine(QStringView line, int *prefixLen, int *indent, TaskState *state)
{
    const qsizetype n = line.size();
    qsizetype i = 0;
    int col = 0;
    while (i < n && isSpace(line[i])) {
        col += line[i] == u'\t' ? 4 - (col % 4) : 1;
        ++i;
    }
    if (n - i < 6)
        return false;
    const QChar b = line[i];
    if ((b != u'-' && b != u'*' && b != u'+') || line[i + 1] != u' ' || line[i + 2] != u'['
        || line[i + 4] != u']' || line[i + 5] != u' ')
        return false;
    TaskState st;
    if (!stateFromChar(line[i + 3].unicode(), &st))
        return false;
    *prefixLen = int(i + 6);
    *indent = col;
    *state = st;
    return true;
}

void parseMeta(Task &t)
{
    const QStringView c(t.content);
    const qsizetype n = c.size();
    QString text;
    text.reserve(n);
    // Tags are kept in the text when words follow them ("Call #mum about it"),
    // and dropped from it when they trail ("Do the thing #office").
    QString pendingTags;
    for (qsizetype i = 0; i < n;) {
        while (i < n && isSpace(c[i]))
            ++i;
        const qsizetype st = i;
        while (i < n && !isSpace(c[i]))
            ++i;
        if (st == i)
            break;
        const QStringView tok = c.mid(st, i - st);
        char16_t k;
        QDate d;
        if (tok.size() >= 3 && tok[0] == u'~') {
            const qint64 eta = parseEta(tok.mid(1));
            if (eta >= 0) {
                t.eta = eta;
                t.etaText = tok.mid(1).toString();
                continue;
            }
        }
        if (dateToken(tok, &k, &d)) {
            switch (k) {
            case '!': t.due = d; break;
            case '+': t.created = d; break;
            case 'x': t.completed = d; break;
            case '-': t.cancelled = d; break;
            }
            continue;
        }
        if (const QStringView tag = tagOf(tok); !tag.isEmpty()) {
            const bool known = std::any_of(t.tags.cbegin(), t.tags.cend(), [&](const QString &x) {
                return tag.compare(x, Qt::CaseInsensitive) == 0;
            });
            if (!known)
                t.tags << tag.toString();
            if (tag.size() + 1 == tok.size()) { // the whole token is the tag
                if (!pendingTags.isEmpty())
                    pendingTags += u' ';
                pendingTags += tok;
                continue;
            }
        }
        if (!pendingTags.isEmpty()) {
            if (!text.isEmpty())
                text += u' ';
            text += pendingTags;
            pendingTags.clear();
        }
        if (!text.isEmpty())
            text += u' ';
        text += tok;
    }
    t.text = std::move(text);
}

std::unique_ptr<TaskFile> parse(const QString &path, const QString &relPath, const QByteArray &data)
{
    auto file = std::make_unique<TaskFile>();
    file->path = path;
    file->relPath = relPath;
    file->hash = qHash(data);

    struct Open { int indent; int idx; };
    std::vector<Open> stack;
    std::vector<int> parentIdx;

    const char *s = data.constData();
    const char *const end = s + data.size();
    char fence = 0;
    qsizetype fenceLen = 0;

    for (int lineNo = 0; s < end; ++lineNo) {
        const char *nl = static_cast<const char *>(std::memchr(s, '\n', size_t(end - s)));
        const char *e = nl ? nl : end;
        const char *next = nl ? nl + 1 : end;
        if (e > s && e[-1] == '\r')
            --e;

        const char *q = s;
        int col = 0;
        while (q < e && (*q == ' ' || *q == '\t')) {
            col += *q == '\t' ? 4 - (col % 4) : 1;
            ++q;
        }
        if (q == e) { // blank line: keeps nesting
            s = next;
            continue;
        }

        // Fenced code blocks: tasks inside them are examples, not tasks.
        if ((*q == '`' || *q == '~') && e - q >= 3 && q[1] == *q && q[2] == *q) {
            const char c = *q;
            const char *r = q;
            while (r < e && *r == c)
                ++r;
            if (!fence) {
                fence = c;
                fenceLen = r - q;
            } else if (c == fence && r - q >= fenceLen) {
                fence = 0;
            }
            s = next;
            continue;
        }
        if (fence) {
            s = next;
            continue;
        }

        // Any non-blank line closes every open task indented at or beyond it.
        while (!stack.empty() && stack.back().indent >= col)
            stack.pop_back();

        if ((*q == '-' || *q == '*' || *q == '+') && e - q >= 6 && q[1] == ' ' && q[2] == '['
            && q[4] == ']' && q[5] == ' ') {
            TaskState st;
            if (stateFromChar(char16_t(uchar(q[3])), &st)) {
                Task t;
                t.line = lineNo;
                t.indent = col;
                t.state = st;
                t.prefix = QString::fromLatin1(s, q + 6 - s);
                t.content = QString::fromUtf8(q + 6, e - (q + 6));
                parseMeta(t);
                parentIdx.push_back(stack.empty() ? -1 : stack.back().idx);
                stack.push_back({col, int(file->tasks.size())});
                file->tasks.push_back(std::move(t));
            }
        }
        s = next;
    }

    // Link. The vector is final now, so pointers into it are stable.
    auto &tasks = file->tasks;
    for (size_t i = 0; i < tasks.size(); ++i) {
        Task &t = tasks[i];
        t.file = file.get();
        if (parentIdx[i] >= 0) {
            Task &p = tasks[size_t(parentIdx[i])];
            t.parent = &p;
            t.row = int(p.children.size());
            t.depth = p.depth + 1;
            p.children.push_back(&t);
        } else {
            t.row = int(file->roots.size());
            file->roots.push_back(&t);
        }
    }
    // Children always follow their parent, so a reverse pass accumulates bottom-up.
    for (auto it = tasks.rbegin(); it != tasks.rend(); ++it) {
        if (it->state != TaskState::Cancelled) {
            ++file->total;
            if (it->state == TaskState::Done)
                ++file->done;
        }
        if (!it->parent || it->state == TaskState::Cancelled)
            continue;
        it->parent->subTotal += 1 + it->subTotal;
        it->parent->subDone += (it->state == TaskState::Done ? 1 : 0) + it->subDone;
    }
    return file;
}

std::unique_ptr<TaskFile> parseFile(const QString &path, const QString &relPath)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;
    return parse(path, relPath, f.readAll());
}

QString setState(const QString &line, TaskState state, const QDate &today)
{
    int pl, indent;
    TaskState old;
    if (!splitTaskLine(line, &pl, &indent, &old) || old == state)
        return line;
    QString prefix = line.left(pl);
    prefix[pl - 3] = stateChar(state);
    QString content = removeTokens(line.mid(pl), dateKinds(u"x-"));
    if (state == TaskState::Done)
        content = appendToken(content, u'x' + today.toString(Qt::ISODate));
    else if (state == TaskState::Cancelled)
        content = appendToken(content, u'-' + today.toString(Qt::ISODate));
    return prefix + content;
}

QString setDue(const QString &line, const QDate &due)
{
    int pl, indent;
    TaskState st;
    if (!splitTaskLine(line, &pl, &indent, &st))
        return line;
    QString content = removeTokens(line.mid(pl), dateKinds(u"!"));
    if (due.isValid())
        content = appendToken(content, u'!' + due.toString(Qt::ISODate));
    return line.left(pl) + content;
}

QString setEta(const QString &line, const QString &eta)
{
    int pl, indent;
    TaskState st;
    if (!splitTaskLine(line, &pl, &indent, &st))
        return line;
    QString content = removeTokens(line.mid(pl), etaToken);
    if (!eta.isEmpty())
        content = appendToken(content, u'~' + eta);
    return line.left(pl) + content;
}

QString setCreated(const QString &line, const QDate &date)
{
    int pl, indent;
    TaskState st;
    if (!splitTaskLine(line, &pl, &indent, &st) || !date.isValid())
        return line;
    Task t;
    t.content = line.mid(pl);
    parseMeta(t);
    if (t.created.isValid())
        return line; // keep a date the user typed
    return line.left(pl) + appendToken(t.content, u'+' + date.toString(Qt::ISODate));
}

QString setContent(const QString &line, const QString &content)
{
    int pl, indent;
    TaskState st;
    if (!splitTaskLine(line, &pl, &indent, &st))
        return line;
    return line.left(pl) + content;
}

QString removeDates(const QString &line, QStringView kinds)
{
    int pl, indent;
    TaskState st;
    if (!splitTaskLine(line, &pl, &indent, &st))
        return line;
    return line.left(pl) + removeTokens(line.mid(pl), dateKinds(kinds));
}

QString leadingWhitespace(const QString &line)
{
    qsizetype i = 0;
    while (i < line.size() && isSpace(line[i]))
        ++i;
    return line.left(i);
}

} // namespace parser
