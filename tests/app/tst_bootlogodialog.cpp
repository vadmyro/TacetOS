// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The Boot logo button of the card and its dialog (docs/specs/boot-screen.md sections 8.1, 8.2
// and 12). A temporary folder stands for the mounted volume of the iPod.

#include "bootlogo.h"
#include "home/bootlogodialog.h"
#include "home/ipodcard.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos;
using namespace tacetos::device;

namespace {

Model classic()
{
    Model m;
    m.id = QStringLiteral("classic-6g");
    m.name = QStringLiteral("iPod Classic");
    m.screen = ModelScreen{320, 240, true};
    return m;
}

Model mini()
{
    Model m;
    m.id = QStringLiteral("mini-2g");
    m.name = QStringLiteral("iPod mini");
    m.screen = ModelScreen{138, 110, false};
    return m;
}

bootlogo::Screen screenOf(const Model &m)
{
    return {m.screen->width, m.screen->height, m.screen->colour};
}

// A mounted volume with the firmware folder of `m`.
struct Volume
{
    explicit Volume(const Model &m)
    {
        QDir(dir.path()).mkpath(m.firmwareFolder);
        folder = QDir(dir.path()).filePath(m.firmwareFolder);
    }
    QString logo() const { return bootlogo::logoPath(folder); }
    QTemporaryDir dir;
    QString folder;
};

// A picture three times wider than high, white.
QString widePicture(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("wide.png"));
    QImage image(900, 300, QImage::Format_RGB32);
    image.fill(Qt::white);
    image.save(path);
    return path;
}

QString put(BootLogoDialog &dialog)
{
    QSignalSpy finished(&dialog, &BootLogoDialog::writeFinished);
    dialog.putButton()->click();
    if (!finished.wait(5000))
        return QStringLiteral("no end");
    return finished.at(0).at(0).toString();
}

ScreenEntry readyEntry(const QString &model, FirmwareKind firmware, ScreenKind kind)
{
    DetectedIpod ipod;
    ipod.state = IpodState::Ready;
    ipod.model = model;
    ipod.portPath = QStringLiteral("1-2");
    ipod.firmwareLine.kind = firmware;
    ScreenEntry entry;
    entry.portPath = ipod.portPath;
    entry.state.kind = kind;
    entry.ipod = ipod;
    return entry;
}

} // namespace

class TestBootLogoDialog : public QObject
{
    Q_OBJECT

private slots:
    void opensOnTacetosLogo();
    void threeChoicesWrittenAndShownAgain_data();
    void threeChoicesWrittenAndShownAgain();
    void unusableFileSaysSo();
    void cutWriteCleanedAtTheNextWrite();
    void failedWriteSaysWhy();
    void fitAndFillChangeThePreview();
    void refusedPicture();
    void smallScreensShownTwice();
    void buttonOnTheCard_data();
    void buttonOnTheCard();
};

void TestBootLogoDialog::opensOnTacetosLogo()
{
    const Model m = classic();
    Volume volume(m);
    BootLogoDialog dialog(m, volume.dir.path());
    QCOMPARE(dialog.windowTitle(), QStringLiteral("Boot logo"));
    QVERIFY(dialog.tacetosButton()->isChecked());
    QVERIFY(dialog.message().isEmpty());
    QVERIFY(!dialog.fitButton()->isEnabled());
    // The banner of the resources, drawn as the firmware draws it.
    const QImage preview = dialog.previewImage();
    QCOMPARE(preview.size(), QSize(320, 240));
    QCOMPARE(preview, bootlogo::toImage(BootLogoDialog::tacetosLogo(screenOf(m)), screenOf(m)));
    // The banner is there: more than the background colour, and the corners are background.
    QSet<QRgb> colours;
    for (int y = 0; y < preview.height(); ++y)
        for (int x = 0; x < preview.width(); ++x)
            colours.insert(preview.pixel(x, y));
    QVERIFY(colours.size() > 4);
    QCOMPARE(preview.pixel(319, 239), preview.pixel(0, 0));

    // On a grey screen, the nearest of the 4 levels: a plain background, no dots.
    const QImage grey = BootLogoDialog(mini(), volume.dir.path()).previewImage();
    QCOMPARE(grey.size(), QSize(138, 110));
    for (int x = 0; x < 138; ++x)
        QCOMPARE(grey.pixel(x, 5), grey.pixel(0, 0));
}

void TestBootLogoDialog::threeChoicesWrittenAndShownAgain_data()
{
    QTest::addColumn<bool>("colour");
    QTest::newRow("320x240 colour") << true;
    QTest::newRow("138x110 grey") << false;
}

void TestBootLogoDialog::threeChoicesWrittenAndShownAgain()
{
    QFETCH(bool, colour);
    const Model m = colour ? classic() : mini();
    Volume volume(m);

    // My picture.
    QImage written;
    {
        BootLogoDialog dialog(m, volume.dir.path());
        QVERIFY(dialog.usePicture(widePicture(volume.dir)));
        QVERIFY(dialog.pictureButton()->isChecked());
        QVERIFY(dialog.fitButton()->isEnabled());
        written = dialog.previewImage();
        QCOMPARE(put(dialog), QString());
        QCOMPARE(dialog.message(),
                 QStringLiteral("Done. The new logo shows the next time the iPod starts."));
        QCOMPARE(dialog.putButton()->text(), QStringLiteral("Close"));
        QVERIFY(dialog.putButton()->isEnabled());
        QVERIFY(dialog.cancelButton()->isHidden());
    }
    const bootlogo::OnIpod onIpod = bootlogo::readLogo(volume.folder, screenOf(m));
    QCOMPARE(onIpod.found, bootlogo::Found::Picture);
    QCOMPARE(bootlogo::toImage(*onIpod.logo, screenOf(m)), written);
    {
        BootLogoDialog dialog(m, volume.dir.path());
        QVERIFY(dialog.pictureButton()->isChecked());
        QCOMPARE(dialog.previewImage(), written);
        // Shown as the current picture; no picture to scale again.
        QVERIFY(!dialog.fitButton()->isEnabled());

        // Black screen.
        dialog.blackButton()->setChecked(true);
        QCOMPARE(dialog.previewImage().pixel(10, 10), qRgb(0, 0, 0));
        QCOMPARE(put(dialog), QString());
    }
    QCOMPARE(QFileInfo(volume.logo()).size(), 16);
    {
        BootLogoDialog dialog(m, volume.dir.path());
        QVERIFY(dialog.blackButton()->isChecked());

        // TacetOS logo: the file goes.
        dialog.tacetosButton()->setChecked(true);
        QCOMPARE(put(dialog), QString());
    }
    QVERIFY(!QFile::exists(volume.logo()));
    BootLogoDialog dialog(m, volume.dir.path());
    QVERIFY(dialog.tacetosButton()->isChecked());
}

void TestBootLogoDialog::unusableFileSaysSo()
{
    const Model m = classic();
    Volume volume(m);
    QFile file(volume.logo());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("TLGO but nothing more");
    file.close();
    BootLogoDialog dialog(m, volume.dir.path());
    QVERIFY(dialog.tacetosButton()->isChecked());
    QCOMPARE(
        dialog.message(),
        QStringLiteral("The picture on the iPod cannot be read. The iPod shows the TacetOS logo"));
}

void TestBootLogoDialog::cutWriteCleanedAtTheNextWrite()
{
    const Model m = classic();
    Volume volume(m);
    QFile cut(volume.logo() + QStringLiteral(".new"));
    QVERIFY(cut.open(QIODevice::WriteOnly));
    cut.write("cut in the middle");
    cut.close();
    BootLogoDialog dialog(m, volume.dir.path());
    dialog.blackButton()->setChecked(true);
    QCOMPARE(put(dialog), QString());
    QVERIFY(!cut.exists());
    QCOMPARE(bootlogo::readLogo(volume.folder, screenOf(m)).found, bootlogo::Found::Black);
}

void TestBootLogoDialog::failedWriteSaysWhy()
{
    const Model m = classic();
    QTemporaryDir empty;
    // No firmware folder on this volume.
    BootLogoDialog dialog(m, empty.path());
    dialog.blackButton()->setChecked(true);
    const QString error = put(dialog);
    QVERIFY(!error.isEmpty());
    QVERIFY(
        dialog.message().startsWith(QStringLiteral("The logo could not be written to the iPod: ")));
    QVERIFY(dialog.message().endsWith(error));
    QCOMPARE(dialog.putButton()->text(), QStringLiteral("Put on iPod"));
    QVERIFY(dialog.putButton()->isEnabled());
    QVERIFY(dialog.cancelButton()->isEnabled());
    QVERIFY(dialog.blackButton()->isEnabled());
    QVERIFY(QDir(empty.path()).isEmpty());
}

void TestBootLogoDialog::fitAndFillChangeThePreview()
{
    const Model m = classic();
    Volume volume(m);
    BootLogoDialog dialog(m, volume.dir.path());
    QVERIFY(dialog.usePicture(widePicture(volume.dir)));
    QVERIFY(dialog.fitButton()->isChecked());
    // Fit: black bars above and below the wide picture.
    QCOMPARE(dialog.previewImage().pixel(160, 2), qRgb(0, 0, 0));
    QCOMPARE(dialog.previewImage().pixel(160, 120), qRgb(255, 255, 255));
    dialog.fillButton()->setChecked(true);
    QCOMPARE(dialog.previewImage().pixel(160, 2), qRgb(255, 255, 255));
    dialog.fitButton()->setChecked(true);
    QCOMPARE(dialog.previewImage().pixel(160, 2), qRgb(0, 0, 0));
}

void TestBootLogoDialog::refusedPicture()
{
    const Model m = classic();
    Volume volume(m);
    const QString text = volume.dir.filePath(QStringLiteral("notes.png"));
    QFile file(text);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a picture");
    file.close();
    BootLogoDialog dialog(m, volume.dir.path());
    QVERIFY(!dialog.usePicture(text));
    QCOMPARE(dialog.message(), QStringLiteral("This file is not a picture the app can read"));
    QVERIFY(dialog.tacetosButton()->isChecked());
    // My picture with no picture cannot be written.
    dialog.pictureButton()->setChecked(true);
    QVERIFY(!dialog.putButton()->isEnabled());
}

void TestBootLogoDialog::smallScreensShownTwice()
{
    const Model m = mini();
    Volume volume(m);
    BootLogoDialog dialog(m, volume.dir.path());
    const auto *preview = dialog.findChild<QLabel *>(QStringLiteral("bootLogoPreview"));
    QVERIFY(preview);
    QCOMPARE(preview->pixmap().size(), QSize(276, 220));
    BootLogoDialog big(classic(), volume.dir.path());
    QCOMPARE(big.findChild<QLabel *>(QStringLiteral("bootLogoPreview"))->pixmap().size(),
             QSize(320, 240));
}

void TestBootLogoDialog::buttonOnTheCard_data()
{
    QTest::addColumn<bool>("withScreen");
    QTest::addColumn<int>("firmware");
    QTest::addColumn<int>("kind");
    QTest::addColumn<bool>("shown");
    const int ready = int(ScreenKind::Ready);
    QTest::newRow("TacetOS") << true << int(FirmwareKind::Tacetos) << ready << true;
    QTest::newRow("present") << true << int(FirmwareKind::Custom) << ready << true;
    QTest::newRow("Apple") << true << int(FirmwareKind::Apple) << ready << false;
    QTest::newRow("no firmware read") << true << int(FirmwareKind::Unknown) << ready << false;
    QTest::newRow("model without screen") << false << int(FirmwareKind::Tacetos) << ready << false;
    QTest::newRow("busy") << true << int(FirmwareKind::Tacetos) << int(ScreenKind::Busy) << false;
    QTest::newRow("not mounted") << true << int(FirmwareKind::Tacetos)
                                 << int(ScreenKind::NotMounted) << false;
}

void TestBootLogoDialog::buttonOnTheCard()
{
    QFETCH(bool, withScreen);
    QFETCH(int, firmware);
    QFETCH(int, kind);
    QFETCH(bool, shown);
    Model m = classic();
    if (!withScreen)
        m.screen.reset();
    IpodCard card;
    card.setEntry(readyEntry(m.id, FirmwareKind(firmware), ScreenKind(kind)), {m},
                  HostSystem::Linux);
    card.show();
    QCOMPARE(card.bootLogoButton()->isVisible(), shown);
    // A model that is in no list is not known either.
    card.setEntry(readyEntry(QStringLiteral("other"), FirmwareKind(firmware), ScreenKind(kind)),
                  {m}, HostSystem::Linux);
    QVERIFY(!card.bootLogoButton()->isVisible());

    if (!shown)
        return;
    card.setEntry(readyEntry(m.id, FirmwareKind(firmware), ScreenKind(kind)), {m},
                  HostSystem::Linux);
    QSignalSpy asked(&card, &IpodCard::actionRequested);
    card.bootLogoButton()->click();
    QCOMPARE(asked.size(), 1);
    QCOMPARE(asked.at(0).at(0).value<HomeAction>(), HomeAction::BootLogo);
    // Hidden while the wizard window is open.
    card.setWizardState(false, true);
    QVERIFY(!card.bootLogoButton()->isVisible());
}

QTEST_MAIN(TestBootLogoDialog)
#include "tst_bootlogodialog.moc"
