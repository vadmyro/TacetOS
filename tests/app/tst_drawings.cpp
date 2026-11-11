// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Every model of models/models.toml has a drawing in app/resources/models/, and
// the drawings follow the rules of app/resources/models/README.md.

#include "models.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTest>

namespace {

const QString kGenericId = QStringLiteral("generic");

QString drawingPath(const QString &id)
{
    return QStringLiteral(":/models/%1.svg").arg(id);
}

QStringList modelIds()
{
    const auto list = tacetos::device::loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QStringList ids;
    for (const auto &model : list.models)
        ids.append(model.id);
    return ids;
}

QStringList allIds()
{
    return modelIds() + QStringList{kGenericId};
}

} // namespace

class TestDrawings : public QObject
{
    Q_OBJECT

private slots:
    void modelListIsRead();
    void everyModelHasADrawing();
    void noDrawingWithoutAModel();
    void drawingsAreValid();
    void viewBoxHeightIsTheSame();
    void onlyTinyFeatures();
    void drawingsPaint();
};

void TestDrawings::modelListIsRead()
{
    const auto list = tacetos::device::loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    QCOMPARE(list.models.size(), 10);
}

void TestDrawings::everyModelHasADrawing()
{
    for (const QString &id : allIds())
        QVERIFY2(QFile::exists(drawingPath(id)), qPrintable(id));
}

void TestDrawings::noDrawingWithoutAModel()
{
    QStringList found = QDir(QStringLiteral(":/models")).entryList({QStringLiteral("*.svg")});
    QStringList expected;
    for (const QString &id : allIds())
        expected.append(id + QStringLiteral(".svg"));
    found.sort();
    expected.sort();
    QCOMPARE(found, expected);
}

void TestDrawings::drawingsAreValid()
{
    for (const QString &id : allIds()) {
        QSvgRenderer renderer(drawingPath(id));
        QVERIFY2(renderer.isValid(), qPrintable(id));
        // A drawing with no viewBox would not scale with the card.
        QVERIFY2(renderer.viewBoxF().width() > 0, qPrintable(id));
        QVERIFY2(renderer.viewBoxF().height() > 0, qPrintable(id));
    }
}

void TestDrawings::viewBoxHeightIsTheSame()
{
    const qreal expected = QSvgRenderer(drawingPath(kGenericId)).viewBoxF().height();
    for (const QString &id : allIds()) {
        QSvgRenderer renderer(drawingPath(id));
        QCOMPARE(renderer.viewBoxF().height(), expected);
        QCOMPARE(renderer.viewBoxF().topLeft(), QPointF(0, 0));
    }
}

void TestDrawings::onlyTinyFeatures()
{
    // No text, picture, script, style sheet or filter: flat shapes with
    // presentation attributes only.
    const QStringList forbidden = {
        QStringLiteral("<text"),    QStringLiteral("<image"),   QStringLiteral("<script"),
        QStringLiteral("<style"),   QStringLiteral("<filter"),  QStringLiteral("<foreignObject"),
        QStringLiteral("style=\""), QStringLiteral("class=\""),
    };
    for (const QString &id : allIds()) {
        QFile file(drawingPath(id));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(id));
        const QString content = QString::fromUtf8(file.readAll());
        QVERIFY2(content.contains(QStringLiteral("baseProfile=\"tiny\"")), qPrintable(id));
        for (const QString &word : forbidden)
            QVERIFY2(!content.contains(word), qPrintable(id + QStringLiteral(": ") + word));
    }
}

void TestDrawings::drawingsPaint()
{
    for (const QString &id : allIds()) {
        QSvgRenderer renderer(drawingPath(id));
        QImage image(120, 200, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            renderer.render(&painter);
        }
        // The body is drawn in the middle of the picture.
        QVERIFY2(qAlpha(image.pixel(60, 100)) == 255, qPrintable(id));
        // The corner is outside the rounded body.
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
    }
}

QTEST_GUILESS_MAIN(TestDrawings)
#include "tst_drawings.moc"
