// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "style/appfont.h"
#include "style/palette.h"
#include "style/stylecontroller.h"

#include <QApplication>
#include <QRegularExpression>
#include <QTest>
#include <cmath>

using tacetos::Palette;

namespace {

double channel(double v)
{
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

// WCAG 2.1 relative luminance.
double luminance(const QColor &c)
{
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace

class TestStyle : public QObject
{
    Q_OBJECT

private slots:
    void lightValues();
    void darkValues();
    void applySwitchesTheWindowColour();
    void unknownSchemeIsLight();
    void contrastLight();
    void contrastDark();
    void styleSheetUsesOnlyPaletteColours();
    void missingFontFile();
    void fontSizes();

private:
    static void checkContrast(const Palette &p);
};

void TestStyle::lightValues()
{
    const Palette p = Palette::light();
    QCOMPARE(p.window, QColor("#F6F5F2"));
    QCOMPARE(p.card, QColor("#FFFFFF"));
    QCOMPARE(p.text, QColor("#1C1F24"));
    QCOMPARE(p.textSecondary, QColor("#5C6068"));
    QCOMPARE(p.border, QColor("#E3E1DC"));
    QCOMPARE(p.accent, QColor("#FF5A4E"));
    QCOMPARE(p.danger, QColor("#C2362B"));
    QCOMPARE(p.button, QColor("#1C1F24"));
    QCOMPARE(p.buttonText, QColor("#F6F5F2"));
    QCOMPARE(p.buttonHover, QColor("#33373E"));
}

void TestStyle::darkValues()
{
    const Palette p = Palette::dark();
    QCOMPARE(p.window, QColor("#1C1F24"));
    QCOMPARE(p.card, QColor("#262A31"));
    QCOMPARE(p.text, QColor("#F6F5F2"));
    QCOMPARE(p.textSecondary, QColor("#A9ADB4"));
    QCOMPARE(p.border, QColor("#353A42"));
    QCOMPARE(p.accent, QColor("#FF5A4E"));
    QCOMPARE(p.danger, QColor("#FF7A70"));
    QCOMPARE(p.button, QColor("#F6F5F2"));
    QCOMPARE(p.buttonText, QColor("#1C1F24"));
    QCOMPARE(p.buttonHover, QColor("#E3E1DC"));
}

void TestStyle::applySwitchesTheWindowColour()
{
    tacetos::StyleController controller;
    controller.apply(Qt::ColorScheme::Dark);
    QCOMPARE(QApplication::palette().color(QPalette::Window), Palette::dark().window);
    QVERIFY(!qApp->styleSheet().isEmpty());
    controller.apply(Qt::ColorScheme::Light);
    QCOMPARE(QApplication::palette().color(QPalette::Window), Palette::light().window);
}

void TestStyle::unknownSchemeIsLight()
{
    tacetos::StyleController controller;
    controller.apply(Qt::ColorScheme::Dark);
    controller.apply(Qt::ColorScheme::Unknown);
    QCOMPARE(QApplication::palette().color(QPalette::Window), Palette::light().window);
}

void TestStyle::checkContrast(const Palette &p)
{
    struct Pair
    {
        const char *name;
        QColor text;
        QColor background;
    };
    const Pair pairs[] = {
        {"text/window", p.text, p.window},
        {"text/card", p.text, p.card},
        {"textSecondary/window", p.textSecondary, p.window},
        {"textSecondary/card", p.textSecondary, p.card},
        {"danger/window", p.danger, p.window},
        {"danger/card", p.danger, p.card},
        {"buttonText/button", p.buttonText, p.button},
        {"buttonText/buttonHover", p.buttonText, p.buttonHover},
    };
    for (const Pair &pair : pairs) {
        const double ratio = contrast(pair.text, pair.background);
        QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("%1: %2").arg(pair.name).arg(ratio)));
    }
}

void TestStyle::contrastLight()
{
    checkContrast(Palette::light());
}

void TestStyle::contrastDark()
{
    checkContrast(Palette::dark());
}

void TestStyle::styleSheetUsesOnlyPaletteColours()
{
    for (const Palette &p : {Palette::light(), Palette::dark()}) {
        const QList<QColor> roles = {p.window, p.card,   p.text,   p.textSecondary, p.border,
                                     p.accent, p.danger, p.button, p.buttonText,    p.buttonHover};
        QStringList allowed;
        for (const QColor &c : roles)
            allowed << c.name();
        const QString css = tacetos::styleSheet(p);
        auto it = QRegularExpression(QStringLiteral("#[0-9a-fA-F]{3,8}\\b")).globalMatch(css);
        while (it.hasNext()) {
            const QString hex = it.next().captured().toLower();
            QVERIFY2(allowed.contains(hex), qPrintable(hex));
        }
    }
}

void TestStyle::missingFontFile()
{
    const QFont before = QApplication::font();
    QVERIFY(!tacetos::loadAppFont(QStringLiteral("/nonexistent/folder/AdwaitaSans-Regular.ttf")));
    QVERIFY(!tacetos::loadAppFont(QString()));
    QCOMPARE(QApplication::font(), before);
    QVERIFY(!QApplication::font().family().isEmpty());
}

void TestStyle::fontSizes()
{
    QCOMPARE(tacetos::fontFor(tacetos::TextStyle::Large).pixelSize(), 22);
    QCOMPARE(tacetos::fontFor(tacetos::TextStyle::Title).pixelSize(), 17);
    QCOMPARE(tacetos::fontFor(tacetos::TextStyle::Body).pixelSize(), 14);
    QCOMPARE(tacetos::fontFor(tacetos::TextStyle::Button).pixelSize(), 14);
    QCOMPARE(tacetos::fontFor(tacetos::TextStyle::Secondary).pixelSize(), 13);
    QCOMPARE(tacetos::fontFor(tacetos::TextStyle::Title).weight(), QFont::DemiBold);
    QCOMPARE(tacetos::fontFor(tacetos::TextStyle::Button).weight(), QFont::Medium);
}

QTEST_MAIN(TestStyle)
#include "tst_style.moc"
