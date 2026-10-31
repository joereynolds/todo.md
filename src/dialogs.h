#pragma once

#include <QDate>
#include <QDialog>

class QCalendarWidget;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

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
