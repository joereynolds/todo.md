#include "tagcompleter.h"

#include "models.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStringListModel>

TagCompleter::TagCompleter(TagIndex *index, QLineEdit *edit)
    : QObject(edit), m_index(index), m_edit(edit), m_completer(new QCompleter(this)), m_model(new QStringListModel(this))
{
    m_completer->setModel(m_model);
    m_completer->setWidget(edit);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setMaxVisibleItems(8);
    // The list is rebuilt lazily, only when a completion is actually wanted.
    connect(index, &TagIndex::changed, this, [this] { m_dirty = true; });
    connect(edit, &QLineEdit::textEdited, this, &TagCompleter::textEdited);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated), this, &TagCompleter::insert);
    // Installed after QCompleter's own filter, so it runs first (see eventFilter).
    m_completer->popup()->installEventFilter(this);
}

bool TagCompleter::eventFilter(QObject *watched, QEvent *event)
{
    // QCompleter hands Enter to the line edit as well as completing, which would
    // submit the half-typed text (e.g. the sidebar add box). Enter or Tab on a
    // highlighted tag only completes it; Enter with nothing highlighted closes
    // the popup and goes to the line edit as usual.
    if (watched == m_completer->popup() && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab) {
            const QModelIndex cur = m_completer->popup()->currentIndex();
            m_completer->popup()->hide();
            if (cur.isValid()) {
                insert(cur.data().toString());
                return true;
            }
            if (key == Qt::Key_Tab)
                return true;
            QCoreApplication::sendEvent(m_edit, event);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void TagCompleter::refreshModel()
{
    if (!m_dirty)
        return;
    QStringList tags;
    tags.reserve(qsizetype(m_index->entries().size()));
    for (const TagIndex::Entry &e : m_index->entries())
        tags << u'#' + e.tag;
    m_model->setStringList(tags);
    m_dirty = false;
}

void TagCompleter::wordAtCursor(qsizetype *start, qsizetype *end) const
{
    const QString text = m_edit->text();
    const qsizetype cur = m_edit->cursorPosition();
    qsizetype s = cur;
    while (s > 0 && !text[s - 1].isSpace())
        --s;
    qsizetype e = cur;
    while (e < text.size() && !text[e].isSpace())
        ++e;
    *start = s;
    *end = e;
}

void TagCompleter::textEdited()
{
    qsizetype start, end;
    wordAtCursor(&start, &end);
    const QString prefix = m_edit->text().mid(start, m_edit->cursorPosition() - start);
    if (!prefix.startsWith(u'#')) {
        m_completer->popup()->hide();
        return;
    }
    refreshModel();
    m_completer->setCompletionPrefix(prefix);
    // Nothing to offer, or the word is already exactly a known tag.
    if (m_completer->completionCount() == 0
        || (m_completer->completionCount() == 1
            && m_completer->currentCompletion().compare(prefix, Qt::CaseInsensitive) == 0)) {
        m_completer->popup()->hide();
        return;
    }
    m_completer->complete();
}

void TagCompleter::insert(const QString &completion)
{
    qsizetype start, end;
    wordAtCursor(&start, &end);
    QString text = m_edit->text();
    QString replacement = completion;
    if (end >= text.size())
        replacement += u' '; // ready for the next word
    text.replace(start, end - start, replacement);
    m_edit->setText(text);
    m_edit->setCursorPosition(int(start + replacement.size()));
}
