#include "app/UpdateCheckPolicy.hpp"

#include <QtTest>

namespace wobble
{

class AppPolicyTests final : public QObject
{
    Q_OBJECT

private slots:
    void limitsAutomaticUpdateChecksToOncePerDay()
    {
        const QDateTime now(QDate(2026, 8, 1), QTime(12, 0), QTimeZone::UTC);
        QVERIFY(UpdateCheckPolicy::isAutomaticCheckDue({}, now));
        QVERIFY(!UpdateCheckPolicy::isAutomaticCheckDue(
            now.addSecs(-UpdateCheckPolicy::automaticCheckIntervalSeconds + 1),
            now));
        QVERIFY(UpdateCheckPolicy::isAutomaticCheckDue(
            now.addSecs(-UpdateCheckPolicy::automaticCheckIntervalSeconds),
            now));
    }

    void retriesAfterSystemClockMovesBackward()
    {
        const QDateTime now(QDate(2026, 8, 1), QTime(12, 0), QTimeZone::UTC);
        QVERIFY(UpdateCheckPolicy::isAutomaticCheckDue(now.addDays(1), now));
        QVERIFY(!UpdateCheckPolicy::isAutomaticCheckDue(now, {}));
    }
};

int runAppPolicyTests(int argc, char **argv)
{
    AppPolicyTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "AppPolicyTests.moc"
