#include "entry.h"
#include "secretstore.h"

#include <QCoreApplication>
#include <QTest>

namespace {

QVariantMap value(const char *text)
{
    return {{QStringLiteral("value"), QString::fromLatin1(text)}};
}

} // namespace

// Entry::Kind must mean the same kind in the core (validation and ranges come from there).
class TestEntry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void invalidByKind()
    {
        kladde::MemorySecretStore secrets;
        kladde::Session session(&secrets);
        Entry entry(&session);
        const QStringList bad{QStringLiteral("value")};

        // 100 fits weight and pulse, 3 fits body fat and mood (see kladde::entryFields).
        QCOMPARE(entry.invalidFields(Entry::Kind::Weight, value("100")), QStringList());
        QCOMPARE(entry.invalidFields(Entry::Kind::Weight, value("3")), bad);
        QCOMPARE(entry.invalidFields(Entry::Kind::Pulse, value("100")), QStringList());
        QCOMPARE(entry.invalidFields(Entry::Kind::Pulse, value("3")), bad);
        QCOMPARE(entry.invalidFields(Entry::Kind::BodyFat, value("3")), QStringList());
        QCOMPARE(entry.invalidFields(Entry::Kind::BodyFat, value("100")), bad);
        QCOMPARE(entry.invalidFields(Entry::Kind::Mood, value("3")), QStringList());
        QCOMPARE(entry.invalidFields(Entry::Kind::Mood, value("100")), bad);
        QCOMPARE(entry.invalidFields(Entry::Kind::BloodPressure, {}),
                 (QStringList{QStringLiteral("systolic"), QStringLiteral("diastolic")}));
    }

    void rangeByKind()
    {
        kladde::MemorySecretStore secrets;
        kladde::Session session(&secrets);
        Entry entry(&session);
        const QLocale locale;
        const QString value = QStringLiteral("value");

        QCOMPARE(entry.range(Entry::Kind::Weight, value),
                 QStringLiteral("%1 – %2").arg(locale.toString(20.0), locale.toString(350.0)));
        QCOMPARE(entry.range(Entry::Kind::BodyFat, value),
                 QStringLiteral("%1 – %2").arg(locale.toString(2.0), locale.toString(70.0)));
        QCOMPARE(entry.range(Entry::Kind::Mood, value),
                 QStringLiteral("%1 – %2").arg(locale.toString(1.0), locale.toString(5.0)));
        QCOMPARE(entry.range(Entry::Kind::BloodPressure, QStringLiteral("systolic")),
                 QStringLiteral("%1 – %2").arg(locale.toString(60.0), locale.toString(260.0)));
    }
};

QTEST_MAIN(TestEntry)
#include "test_entry.moc"
