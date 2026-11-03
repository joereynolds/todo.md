#pragma once

#include <QDate>
#include <QDialog>

#include <functional>

class QCalendarWidget;
class QCheckBox;
class QComboBox;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QueryBuilder;
class TagIndex;

class DueDateDialog : public QDialog
{
    Q_OBJECT
public:
    DueDateDialog(const QDate &initial, int count, QWidget *parent = nullptr);
    QDate date() const;

private:
    QCalendarWidget *m_calendar;
};

// The add task / subtask dialog: the task text plus an optional due date,
// picked from None, Today, Tomorrow, Next week (+7 days), Next month (+1 month)
// or Pick… (a calendar). A !date typed in the text wins over the choice.
class AddTaskDialog : public QDialog
{
    Q_OBJECT
public:
    AddTaskDialog(const QString &title, const QString &prompt, QWidget *parent = nullptr);
    QLineEdit *lineEdit() const { return m_text; }
    QString text() const;
    QDate due() const { return m_due; } // invalid = none
    QPushButton *dueButton(const QString &name) const; // "none", "today", "tomorrow", "week", "month", "pick"

private:
    void choose(int id, const QDate &date);
    void pick();

    QLineEdit *m_text;
    QButtonGroup *m_group;
    QLabel *m_dueLabel;
    QDate m_due;
    int m_choice = 0;
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

// Create / edit a saved view: a name and a filter query, with a live count of
// what it matches (countFor) and a check that the name is free (nameTaken).
class ViewDialog : public QDialog
{
    Q_OBJECT
public:
    ViewDialog(const QString &title, const QString &name, const QString &query,
               std::function<int(const QString &)> countFor, std::function<bool(const QString &)> nameTaken,
               TagIndex *tags = nullptr, QWidget *parent = nullptr);
    QString name() const;
    QString query() const;
    QLineEdit *nameEdit() const { return m_name; }
    QLineEdit *queryEdit() const { return m_query; }
    QueryBuilder *builder() const { return m_builder; }

private:
    void validate();

    QLineEdit *m_name, *m_query;
    QueryBuilder *m_builder = nullptr;
    QLabel *m_status;
    QPushButton *m_ok;
    std::function<int(const QString &)> m_countFor;
    std::function<bool(const QString &)> m_nameTaken;
};

// Picks an ETA as a number and a unit. eta() is e.g. "15m", or empty when
// "Remove" was chosen.
class EtaDialog : public QDialog
{
    Q_OBJECT
public:
    EtaDialog(const QString &current, int count, QWidget *parent = nullptr);
    QString eta() const { return m_result; }

private:
    void set(int amount, QChar unit);
    QString value() const;

    QSpinBox *m_amount;
    QComboBox *m_unit;
    QString m_result;
};
