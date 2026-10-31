#pragma once

#include <QObject>

class QCompleter;
class QLineEdit;
class QStringListModel;
class TagIndex;

// Suggests known tags for the "#word" under the cursor of a line edit and
// replaces just that word (QLineEdit::setCompleter would replace the whole
// line). Owned by the line edit.
class TagCompleter : public QObject
{
    Q_OBJECT
public:
    TagCompleter(TagIndex *index, QLineEdit *edit);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void textEdited();
    void insert(const QString &completion);
    void refreshModel();
    // The word around the cursor: [start, end).
    void wordAtCursor(qsizetype *start, qsizetype *end) const;

    TagIndex *m_index;
    QLineEdit *m_edit;
    QCompleter *m_completer;
    QStringListModel *m_model;
    bool m_dirty = true;
};
