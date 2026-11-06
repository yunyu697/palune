#include <QtTest>

#include "update/Version.h"

using namespace palune;

class TestVersion : public QObject
{
    Q_OBJECT

private slots:
    void parsesDevVersion()
    {
        QString error;
        const Version v = Version::parse(QStringLiteral("1.0.0-alpha1"), &error);
        QVERIFY2(v.valid, qPrintable(error));
        QVERIFY(v.hasAlpha);
        QCOMPARE(v.alpha, 1);
        QCOMPARE(v.toString(), QStringLiteral("1.0.0-alpha1"));
    }

    void acceptsAlpha10()
    {
        QString error;
        const Version v = Version::parse(QStringLiteral("1.0.0-alpha10"), &error);
        QVERIFY2(v.valid, qPrintable(error));
        QVERIFY2(validateVersionRule(v, &error), qPrintable(error));
    }

    void rejectsAlpha11()
    {
        QString error;
        Version::parse(QStringLiteral("1.0.0-alpha11"), &error);
        QVERIFY(!error.isEmpty());
    }

    void rejectsSegment11()
    {
        QString error;
        Version::parse(QStringLiteral("1.11.0-alpha1"), &error);
        QVERIFY(!error.isEmpty());
        Version::parse(QStringLiteral("1.0.11-alpha1"), &error);
        QVERIFY(!error.isEmpty());
    }

    void acceptsRelease()
    {
        QString error;
        const Version v = Version::parse(QStringLiteral("1.0.0"), &error);
        QVERIFY2(v.valid, qPrintable(error));
        QVERIFY2(validateVersionRule(v, &error), qPrintable(error));
    }

    void rejectsStrayAlphaOnOtherRelease()
    {
        QString error;
        const Version v = Version::parse(QStringLiteral("1.2.0"), &error);
        QVERIFY2(v.valid, qPrintable(error));
        QVERIFY(!validateVersionRule(v, &error));
    }

    void ordersPrereleaseBeforeRelease()
    {
        const Version a1 = Version::parse(QStringLiteral("1.0.0-alpha1"));
        const Version a2 = Version::parse(QStringLiteral("1.0.0-alpha2"));
        const Version release = Version::parse(QStringLiteral("1.0.0"));
        QVERIFY(a1 < a2);
        QVERIFY(a2 < release);
    }

    void rejectsGarbage()
    {
        QString error;
        Version::parse(QStringLiteral("1.0.0-alpha0"), &error);
        QVERIFY(!error.isEmpty());
        Version::parse(QStringLiteral("v1.0.0"), &error);
        QVERIFY(!error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestVersion)
#include "tst_version.moc"
