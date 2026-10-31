#pragma once

#include <QDate>
#include <QDialog>

class QCalendarWidget;
class QCheckBox;
class QLineEdit;

class DueDateDialog : public QDialog
{
    Q_OBJECT
public:
    DueDateDialog(const QDate &initial, int count, QWidget *parent = nullptr);
    QDate date() const;

private:
    QCalendarWidget *m_calendar;
};

// Find/replace over the raw content of the selected tasks.
class ReplaceDialog : public QDialog
{
    Q_OBJECT
public:
    ReplaceDialog(int count, QWidget *parent = nullptr);
    QString find() const;
    QString replacement() const;
    bool regex() const;
    bool caseSensitive() const;

private:
    QLineEdit *m_find, *m_replace;
    QCheckBox *m_regex, *m_case;
};
