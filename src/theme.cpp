#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace theme {

namespace {
QString s_systemStyle;
QPalette s_systemPalette;

QPalette darkPalette()
{
    const QColor window(0x2b, 0x2b, 0x2b), base(0x1e, 0x1e, 0x1e), alt(0x26, 0x26, 0x26);
    const QColor text(0xdc, 0xdc, 0xdc), dim(0x80, 0x80, 0x80), button(0x35, 0x35, 0x35);
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::ToolTipBase, window);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x60, 0x60));
    p.setColor(QPalette::Link, QColor(0x6c, 0xaa, 0xf0));
    p.setColor(QPalette::Highlight, QColor(0x2d, 0x5a, 0x8a));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x45, 0x45, 0x45));
    p.setColor(QPalette::Midlight, QColor(0x3a, 0x3a, 0x3a));
    p.setColor(QPalette::Mid, QColor(0x25, 0x25, 0x25));
    p.setColor(QPalette::Dark, QColor(0x1a, 0x1a, 0x1a));
    p.setColor(QPalette::Shadow, Qt::black);
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, dim);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x40, 0x40, 0x40));
    return p;
}
QPalette softPalette()
{
    const QColor window(0xd5, 0xd8, 0xdc), base(0xe2, 0xe5, 0xe8), alt(0xdb, 0xde, 0xe2);
    const QColor text(0x1f, 0x23, 0x28), dim(0x83, 0x89, 0x91), button(0xcc, 0xd0, 0xd5);
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::ToolTipBase, QColor(0xe8, 0xea, 0xed));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xb0, 0x20, 0x20));
    p.setColor(QPalette::Link, QColor(0x1f, 0x5f, 0xb0));
    p.setColor(QPalette::Highlight, QColor(0x9d, 0xb8, 0xdc));
    p.setColor(QPalette::HighlightedText, text);
    p.setColor(QPalette::Light, QColor(0xec, 0xee, 0xf0));
    p.setColor(QPalette::Midlight, QColor(0xdc, 0xdf, 0xe2));
    p.setColor(QPalette::Mid, QColor(0xaf, 0xb3, 0xb9));
    p.setColor(QPalette::Dark, QColor(0x93, 0x98, 0x9f));
    p.setColor(QPalette::Shadow, QColor(0x5f, 0x63, 0x69));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, dim);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0xc4, 0xc8, 0xcd));
    return p;
}
} // namespace

void init()
{
    s_systemStyle = QApplication::style()->name();
    s_systemPalette = QApplication::palette();
}

void apply(Theme t)
{
    switch (t) {
    case Theme::System:
        QApplication::setStyle(QStyleFactory::create(s_systemStyle));
        QApplication::setPalette(s_systemPalette);
        break;
    case Theme::Light:
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(QApplication::style()->standardPalette());
        break;
    case Theme::Soft:
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(softPalette());
        break;
    case Theme::Dark:
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(darkPalette());
        break;
    }
}

Theme fromString(const QString &s)
{
    if (s == QLatin1String("system"))
        return Theme::System;
    if (s == QLatin1String("soft"))
        return Theme::Soft;
    if (s == QLatin1String("dark"))
        return Theme::Dark;
    return Theme::Light;
}

QString toString(Theme t)
{
    switch (t) {
    case Theme::System: return QStringLiteral("system");
    case Theme::Light: return QStringLiteral("light");
    case Theme::Soft: return QStringLiteral("soft");
    case Theme::Dark: return QStringLiteral("dark");
    }
    return QStringLiteral("light");
}

} // namespace theme
