#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

#include <algorithm>

namespace theme {

namespace {
QString s_systemStyle;
QPalette s_systemPalette;
QColor s_accent;
int s_rowPadding = 9;
bool s_card = false;

// Design tokens of a modern look; both the palette and the style sheet come from these.
struct Look {
    enum Tabs { Underline, Pills, Segmented };
    QColor window, surface, sidebar, border, text, dim, accent, selection, hover, input, tray;
    int radius;
    Tabs tabs;
    bool card; // the list sits in a rounded, bordered card on the window colour
    int rowPadding;
};

Look cleanLook()
{
    Look l;
    l.window = QColor(0xff, 0xff, 0xff);
    l.surface = QColor(0xff, 0xff, 0xff);
    l.sidebar = QColor(0xf7, 0xf7, 0xf8);
    l.border = QColor(0xe8, 0xe8, 0xec);
    l.text = QColor(0x1c, 0x1d, 0x21);
    l.dim = QColor(0x8a, 0x8f, 0x98);
    l.accent = QColor(0x5e, 0x6a, 0xd2);
    l.selection = QColor(0xec, 0xee, 0xfc);
    l.hover = QColor(0xf5, 0xf5, 0xf7);
    l.input = QColor(0xff, 0xff, 0xff);
    l.tray = QColor(0xf1, 0xf1, 0xf4);
    l.radius = 6;
    l.tabs = Look::Underline;
    l.card = false;
    l.rowPadding = 15;
    return l;
}

Look midnightLook()
{
    Look l;
    l.window = QColor(0x17, 0x18, 0x1c);
    l.surface = QColor(0x17, 0x18, 0x1c);
    l.sidebar = QColor(0x11, 0x12, 0x15);
    l.border = QColor(0x27, 0x29, 0x30);
    l.text = QColor(0xe4, 0xe5, 0xe9);
    l.dim = QColor(0x7b, 0x80, 0x8c);
    l.accent = QColor(0x8b, 0x7c, 0xf6);
    l.selection = QColor(0x2a, 0x27, 0x44);
    l.hover = QColor(0x1f, 0x20, 0x26);
    l.input = QColor(0x1e, 0x1f, 0x25);
    l.tray = QColor(0x1e, 0x1f, 0x25);
    l.radius = 8;
    l.tabs = Look::Pills;
    l.card = false;
    l.rowPadding = 15;
    return l;
}

Look paperLook()
{
    Look l;
    l.window = QColor(0xf3, 0xef, 0xe7);
    l.surface = QColor(0xff, 0xfd, 0xf9);
    l.sidebar = l.window;
    l.border = QColor(0xe2, 0xda, 0xcc);
    l.text = QColor(0x2b, 0x26, 0x20);
    l.dim = QColor(0x8f, 0x85, 0x78);
    l.accent = QColor(0x2f, 0x7d, 0x6d);
    l.selection = QColor(0xe2, 0xef, 0xe9);
    l.hover = QColor(0xf7, 0xf3, 0xec);
    l.input = QColor(0xff, 0xfd, 0xf9);
    l.tray = QColor(0xe8, 0xe2, 0xd6);
    l.radius = 10;
    l.tabs = Look::Segmented;
    l.card = true;
    l.rowPadding = 17;
    return l;
}

QColor mixColor(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QPalette lookPalette(const Look &l)
{
    QPalette p;
    p.setColor(QPalette::Window, l.window);
    p.setColor(QPalette::WindowText, l.text);
    p.setColor(QPalette::Base, l.surface);
    p.setColor(QPalette::AlternateBase, mixColor(l.surface, l.text, 0.022));
    p.setColor(QPalette::ToolTipBase, l.surface);
    p.setColor(QPalette::ToolTipText, l.text);
    p.setColor(QPalette::PlaceholderText, l.dim);
    p.setColor(QPalette::Text, l.text);
    p.setColor(QPalette::Button, l.input);
    p.setColor(QPalette::ButtonText, l.text);
    p.setColor(QPalette::BrightText, QColor(0xe0, 0x50, 0x50));
    p.setColor(QPalette::Link, l.accent);
    p.setColor(QPalette::Highlight, l.selection);
    p.setColor(QPalette::HighlightedText, l.text);
    p.setColor(QPalette::Light, mixColor(l.surface, l.text, 0.04));
    p.setColor(QPalette::Midlight, l.border);
    p.setColor(QPalette::Mid, mixColor(l.border, l.text, 0.2));
    p.setColor(QPalette::Dark, mixColor(l.border, l.text, 0.35));
    p.setColor(QPalette::Shadow, mixColor(l.border, Qt::black, 0.5));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, l.dim);
    p.setColor(QPalette::Disabled, QPalette::Highlight, l.hover);
    return p;
}

QString rgba(const QColor &c)
{
    return c.alpha() == 255 ? c.name() : QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString lookStyleSheet(const Look &l)
{
    QString css = QStringLiteral(R"(
QMainWindow, QMenuBar { background: @window; }
QMenuBar { padding: 2px 4px; }
QMenuBar::item { padding: 4px 10px; border-radius: @r6px; background: transparent; }
QMenuBar::item:selected { background: @hover; }
QMenu { background: @surface; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 5px 28px 5px 14px; border-radius: 4px; }
QMenu::item:selected { background: @selection; color: @text; }
QMenu::item:disabled { color: @dim; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
QToolTip { background: @surface; color: @text; border: 1px solid @border; padding: 4px 6px; }

QToolBar#main { background: @window; border: none; border-bottom: 1px solid @border; padding: 7px 10px; spacing: 6px; }
QToolBar#main::separator { width: 1px; background: @border; margin: 4px 6px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: @rpx; padding: 4px 10px; color: @text; }
QToolButton:hover { background: @hover; }
QToolButton:checked { background: @selection; color: @accent; }
QToolButton:checked:hover { background: @selection; border-color: @accentLine; }

QLineEdit, QComboBox, QSpinBox, QDateEdit {
    background: @input; color: @text; border: 1px solid @border; border-radius: @rpx;
    padding: 5px 10px; selection-background-color: @accent; selection-color: #ffffff;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid @accent; }
QComboBox { padding-right: 24px; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { image: url(:/src/icons/chevron-down.png); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: @surface; border: 1px solid @border; selection-background-color: @selection; selection-color: @text; outline: 0; }

QPushButton { background: @input; color: @text; border: 1px solid @border; border-radius: @rpx; padding: 5px 14px; }
QPushButton:hover { background: @hover; border-color: @borderStrong; }
QPushButton:pressed { background: @selection; }
QPushButton:default { background: @accent; color: #ffffff; border-color: @accent; }
QPushButton:disabled { color: @dim; background: @window; }
QPushButton:flat { background: transparent; border: none; color: @accent; padding: 4px 8px; }
QPushButton:flat:hover { background: @hover; }

QTreeView, QListView { background: @surface; border: none; outline: 0; selection-background-color: @selection; selection-color: @text; }
QTreeView::item:hover:!selected { background: @hover; }
QTreeView::item:selected { background: @selection; color: @text; }
QHeaderView { background: @surface; border: none; }
QHeaderView::section {
    background: @surface; color: @dim; border: none; border-bottom: 1px solid @border;
    padding: 6px 8px; font-weight: 600;
}

QScrollBar:vertical { background: transparent; width: 11px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 2px; }
QScrollBar::handle { background: @scroll; border-radius: 3px; }
QScrollBar::handle:vertical { min-height: 32px; }
QScrollBar::handle:horizontal { min-width: 32px; }
QScrollBar::handle:hover { background: @scrollHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSplitter::handle { background: @splitter; }
QSplitter::handle:horizontal { width: 1px; }
QStatusBar { background: @window; color: @dim; border-top: 1px solid @border; }
QStatusBar QLabel { color: @dim; padding: 0 6px; }
QCheckBox { spacing: 6px; }

#sidebar { background: @sidebar; }
#sidebar QLabel#heading { color: @dim; font-weight: 600; padding-top: 6px; }
#sidebar QTreeView { background: @sidebar; }
#sidebar QCalendarWidget QAbstractItemView {
    background: @sidebar; selection-background-color: @accent; selection-color: #ffffff; outline: 0;
}
#sidebar QCalendarWidget QWidget#qt_calendar_navigationbar { background: @sidebar; }
#sidebar QCalendarWidget QToolButton { color: @text; font-weight: 600; padding: 2px 6px; }
#sidebar QScrollArea, #sidebar QScrollArea > QWidget, #sidebar QScrollArea > QWidget > QWidget { background: @sidebar; }
#sidebar QCalendarWidget QToolButton::menu-indicator { image: none; width: 0; }
)");
    switch (l.tabs) {
    case Look::Underline:
        css += QStringLiteral(R"(
QTabWidget::pane { border: none; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: @dim; border: none; border-bottom: 2px solid transparent; padding: 9px 4px 7px 4px; margin: 0 10px 0 10px; }
QTabBar::tab:hover { color: @text; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; font-weight: 600; }
QTabWidget > QTabBar { border-bottom: 1px solid @border; }
)");
        break;
    case Look::Pills:
        css += QStringLiteral(R"(
QTabWidget::pane { border: none; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: @dim; border: none; border-radius: @rpx; padding: 6px 14px; margin: 8px 2px 6px 2px; }
QTabBar::tab:first { margin-left: 10px; }
QTabBar::tab:hover { color: @text; background: @hover; }
QTabBar::tab:selected { color: @accent; background: @selection; font-weight: 600; }
)");
        break;
    case Look::Segmented:
        css += QStringLiteral(R"(
QTabWidget::pane { border: none; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: @tray; color: @dim; border: 1px solid @tray; padding: 5px 16px; margin: 10px 0 8px 0; }
QTabBar::tab:first { border-top-left-radius: @rpx; border-bottom-left-radius: @rpx; margin-left: 12px; }
QTabBar::tab:last { border-top-right-radius: @rpx; border-bottom-right-radius: @rpx; }
QTabBar::tab:hover { color: @text; }
QTabBar::tab:selected { background: @surface; color: @text; border: 1px solid @border; border-radius: @rpx; font-weight: 600; }
)");
        break;
    }
    if (l.card) {
        // The tab pages become a rounded card; the views inside stay flat.
        css += QStringLiteral(R"(
QTabWidget::pane { background: @surface; border: 1px solid @border; border-radius: @rcardpx; margin: 0 12px 12px 12px; padding: 4px; }
QTabWidget > QStackedWidget > QWidget { background: @surface; }
QSplitter::handle { background: transparent; }
QStatusBar { border-top: none; }
QToolBar#main { border-bottom: none; }
)");
    }
    const bool dark = l.window.lightness() < 128;
    QColor scroll = l.text, scrollHover = l.text, accentLine = l.accent;
    scroll.setAlphaF(dark ? 0.18f : 0.16f);
    scrollHover.setAlphaF(dark ? 0.32f : 0.30f);
    accentLine.setAlphaF(0.5f);
    const std::pair<const char *, QString> vars[] = {
        {"@window", l.window.name()},
        {"@surface", l.surface.name()},
        {"@sidebar", l.sidebar.name()},
        {"@borderStrong", mixColor(l.border, l.text, 0.15).name()},
        {"@border", l.border.name()},
        {"@text", l.text.name()},
        {"@dim", l.dim.name()},
        {"@accentLine", rgba(accentLine)},
        {"@accent", l.accent.name()},
        {"@selection", l.selection.name()},
        {"@hover", l.hover.name()},
        {"@input", l.input.name()},
        {"@tray", l.tray.name()},
        {"@scrollHover", rgba(scrollHover)},
        {"@scroll", rgba(scroll)},
        {"@splitter", l.card ? QStringLiteral("transparent") : l.border.name()},
        {"@rcard", QString::number(l.radius + 4)},
        {"@r6", QString::number(std::min(l.radius, 6))},
        {"@r", QString::number(l.radius)},
    };
    for (const auto &[key, value] : vars)
        css.replace(QLatin1String(key), value);
    return css;
}

void applyLook(const Look &l)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(lookPalette(l));
    qApp->setStyleSheet(lookStyleSheet(l));
    s_accent = l.accent;
    s_rowPadding = l.rowPadding;
    s_card = l.card;
}

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
    qApp->setStyleSheet(QString());
    s_accent = QColor();
    s_rowPadding = 9;
    s_card = false;
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
    case Theme::Clean: applyLook(cleanLook()); break;
    case Theme::Midnight: applyLook(midnightLook()); break;
    case Theme::Paper: applyLook(paperLook()); break;
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
    if (s == QLatin1String("clean"))
        return Theme::Clean;
    if (s == QLatin1String("midnight"))
        return Theme::Midnight;
    if (s == QLatin1String("paper"))
        return Theme::Paper;
    return Theme::Light;
}

QString toString(Theme t)
{
    switch (t) {
    case Theme::System: return QStringLiteral("system");
    case Theme::Light: return QStringLiteral("light");
    case Theme::Soft: return QStringLiteral("soft");
    case Theme::Dark: return QStringLiteral("dark");
    case Theme::Clean: return QStringLiteral("clean");
    case Theme::Midnight: return QStringLiteral("midnight");
    case Theme::Paper: return QStringLiteral("paper");
    }
    return QStringLiteral("light");
}

QColor accent()
{
    return s_accent;
}

int rowPadding()
{
    return s_rowPadding;
}

bool cardLayout()
{
    return s_card;
}

} // namespace theme
