#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "apexdriver.h"
#include "backend.h"
#include "hidraw.h"
#include "keyboard.h"
#include "layout.h"
#include "theme.h"
#include "translator.h"

// A keyboard that only remembers what it was sent.
class FakeKeyboard : public Keyboard {
public:
    Settings stored;
    int writes = 0, lastChanges = 0;
    bool restored = false;
    QString model() const override { return "Fake"; }
    QString identity() const override { return "fake:1"; }
    QList<KeyGeometry> layout() const override { return buildLayout(Form::Sixty, Region::Ansi, {"AltRight", "Fn", "ControlRight"}); }
    bool isAnalog(const QString &code) const override { return code != "Fn"; }
    Capabilities capabilities() const override { return {}; }
    bool open(QString *) override { return true; }
    bool read(Settings *s, QString *) override {
        s->clear();
        for (const auto &k : layout()) s->insert(k.code, stored.value(k.code));
        return true;
    }
    bool write(const Settings &s, int changes, QString *) override { stored = s; ++writes; lastChanges = changes; return true; }
    bool restoreLighting(QString *) override { restored = true; return true; }
};

class OmniTests : public QObject {
    Q_OBJECT

private slots:
    void layoutsHaveTheRightKeys()
    {
        const auto right = QStringList{"AltRight", "MetaRight", "Fn", "ControlRight"};
        QCOMPARE(buildLayout(Form::Tkl, Region::Ansi, right).size(), 85);
        QCOMPARE(buildLayout(Form::Tkl, Region::Iso, right).size(), 86);
        QCOMPARE(buildLayout(Form::Sixty, Region::Ansi, right).size(), 61);
        QCOMPARE(buildLayout(Form::Sixty, Region::Iso, right).size(), 62);
        // Every row of the main block ends at 15u.
        for (const auto &k : buildLayout(Form::Sixty, Region::Ansi, right))
            QVERIFY2(k.x + k.w <= 15.001, qPrintable(k.code));
        QCOMPARE(hidUsage("KeyA"), quint8(0x04));
        QCOMPARE(hidUsage("IntlBackslash"), quint8(0x64));
        QCOMPARE(hidUsage("Fn"), quint8(0xF0));
        QCOMPARE(hidUsage("Nonsense"), quint8(0));
    }

    void descriptorUsagePageIsRead()
    {
        QCOMPARE(descriptorUsagePage(QByteArray::fromHex("06c0ff0901a101")), quint16(0xFFC0));
        QCOMPARE(descriptorUsagePage(QByteArray::fromHex("05010906a101")), quint16(0x0001));
        QCOMPARE(descriptorUsagePage(QByteArray()), quint16(0));
    }

    void hidrawIsEnumeratedFromSysfs()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("hidraw3/device");
        write(dir.path() + "/hidraw3/device/uevent", "HID_ID=0003:00001038:00001642\nHID_UNIQ=abc\n");
        write(dir.path() + "/hidraw3/device/report_descriptor", QByteArray::fromHex("06c0ff0901"));
        write(dir.path() + "/hidraw3/device/interface_number", "1\n");
        write(dir.path() + "/hidraw3/device/usb_device", "8-5\n");
        const auto nodes = enumerateHidraw(dir.path(), "/dev");
        QCOMPARE(nodes.size(), 1);
        QCOMPARE(nodes[0].path, QString("/dev/hidraw3"));
        QCOMPARE(nodes[0].vendor, quint16(0x1038));
        QCOMPARE(nodes[0].product, quint16(0x1642));
        QCOMPARE(nodes[0].interface, 1);
        QCOMPARE(nodes[0].usagePage, quint16(0xFFC0));
        QCOMPARE(nodes[0].serial, QString("abc"));
        QCOMPARE(nodes[0].usbDevice, QString("8-5"));
    }

    void apexRefusesEverythingOutsideTheSafeSet()
    {
        auto f = [](std::initializer_list<int> b) { QByteArray a; for (int x : b) a += char(x); return a; };
        QVERIFY(ApexDriver::allowed(f({0, 0x90, 0x00}), false));
        QVERIFY(ApexDriver::allowed(f({0, 0xF5}), false));
        QVERIFY(ApexDriver::allowed(f({0, 0x41}), false));
        QVERIFY(ApexDriver::allowed(f({0, 0x38, 0x61, 0x44}), true));
        QVERIFY(ApexDriver::allowed(f({0, 0xB8, 0x65, 0x44}), true));
        QVERIFY(ApexDriver::allowed(f({0, 0x40, 0x55}), true));
        // Flash writes, profile erase, firmware update, the region write one bit from F5.
        for (int cmd : {0x75, 0x71, 0x72, 0x77, 0x32, 0x02, 0x03, 0xB1, 0x91, 0x92, 0x36, 0x09})
            QVERIFY2(!ApexDriver::allowed(f({0, cmd, 0x01}), false) && !ApexDriver::allowed(f({0, cmd, 0x01}), true),
                     qPrintable(QString::number(cmd, 16)));
        QVERIFY(!ApexDriver::allowed(f({0, 0x40, 0xFF}), true)); // untested fill form
        QVERIFY(!ApexDriver::allowed(f({0, 0x38, 0x66, 0x01}), true)); // Rapid Tap is not used
        QVERIFY(!ApexDriver::allowed(f({1, 0x90, 0x00}), false)); // report ID must be 0
    }

    void apexActuationRoundTrips()
    {
        for (int tenths = 1; tenths <= 40; ++tenths) {
            const auto raw = ApexDriver::actuationRaw(tenths / 10.0);
            QCOMPARE(ApexDriver::actuationMm(raw.first, raw.second), tenths / 10.0);
        }
        QCOMPARE(ApexDriver::actuationRaw(9), ApexDriver::actuationRaw(4.0));
    }

    void rangesSnap()
    {
        const Range r{0.1, 4.0, 0.1};
        QCOMPARE(r.snap(0.34), 0.3);
        QCOMPARE(r.snap(9), 4.0);
        QCOMPARE(r.snap(-1), 0.1);
    }

    void backendEditsTheSelection()
    {
        QTemporaryDir dir;
        Backend backend(nullptr, [] { return QList<HidNode>{}; }, dir.path(), false);
        auto *fake = new FakeKeyboard;
        backend.adopt(fake, "test");
        QCOMPARE(backend.devices().size(), 1);

        backend.setSelection({"KeyW", "KeyA", "Fn"}, false);
        QCOMPARE(backend.selected().value("count").toInt(), 3);
        QCOMPARE(backend.selected().value("analogCount").toInt(), 2);

        backend.setActuation(0.74);
        backend.flush();
        QCOMPARE(fake->writes, 1);
        QCOMPARE(fake->lastChanges, int(ActuationChange));
        QCOMPARE(fake->stored["KeyW"].actuation, 0.7);
        QCOMPARE(fake->stored["Fn"].actuation, 1.8); // not analog, left alone

        backend.select("KeyS", false);
        backend.setActuation(3.0);
        backend.setSelection({"KeyW", "KeyS"}, false);
        QVERIFY(backend.selected().value("actuationMixed").toBool());

        backend.setColor("#ff0000");
        backend.flush();
        QCOMPARE(fake->stored["KeyW"].color, QColor("#ff0000"));
        QVERIFY(QFile::exists(dir.path() + "/fake_1.json"));
    }

    void backendUndoesOneDragAsOneStep()
    {
        QTemporaryDir dir;
        Backend backend(nullptr, [] { return QList<HidNode>{}; }, dir.path(), false);
        auto *fake = new FakeKeyboard;
        backend.adopt(fake, "test");
        backend.select("KeyQ", false);
        for (double v : {1.0, 1.2, 1.4, 1.6})
            backend.setActuation(v);
        backend.setRapidTrigger(true);
        QVERIFY(backend.canUndo());
        backend.undo();
        QCOMPARE(backend.selected().value("rapidTrigger").toBool(), false);
        QCOMPARE(backend.selected().value("actuation").toDouble(), 1.6);
        backend.undo();
        QCOMPARE(backend.selected().value("actuation").toDouble(), 1.8);
        QVERIFY(!backend.canUndo());
        backend.redo();
        QCOMPARE(backend.selected().value("actuation").toDouble(), 1.6);
        backend.flush();
        QCOMPARE(fake->stored["KeyQ"].actuation, 1.6);
    }

    void backendRestoresSavedColours()
    {
        QTemporaryDir dir;
        // A plain string: moc misreads '#' inside raw string literals.
        write(dir.path() + "/fake_1.json", "{\"colors\": {\"KeyE\": \"\x23" "00ff00\"}}");
        Backend backend(nullptr, [] { return QList<HidNode>{}; }, dir.path(), false);
        auto *fake = new FakeKeyboard;
        backend.adopt(fake, "test");
        QCOMPARE(fake->stored["KeyE"].color, QColor("#00ff00"));
        QCOMPARE(fake->lastChanges, int(ColorChange));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(OmniTests)
#include "omni_tests.moc"
