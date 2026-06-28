// tests/test_contract_checker.cpp
// Phase G3 unit tests for ContractChecker (the pre-write validation gate
// for opencode.json). Every Team / Trial / manual-export write goes through
// ContractChecker::validate() in src/apply_helpers.cpp::commit(); these
// tests lock the spec'd behaviour.

#include <QTest>
#include <QJsonArray>
#include <QJsonObject>

#include "generation/ContractChecker.h"

class TestContractChecker : public QObject
{
    Q_OBJECT

private slots:
    void validMinimalConfig_passes();
    void missingSchema_failsWithUsefulMessage();
    void unknownTopLevelKey_fails();
    void illegalPermissionKey_fails();
    void illegalAgentField_fails();
    void malformedModelString_fails();
};

namespace {

// Helpers for constructing a valid baseline config that the per-test
// cases then perturb. Only fields listed in
// docs/OPENCODE-CONFIG-INTROSPECTION.md §4 (ConfigV1.Info) are used.
QJsonObject minimalValidConfig()
{
    QJsonObject cfg;
    cfg.insert(QStringLiteral("$schema"),
               QStringLiteral("https://opencode.ai/config.json"));

    QJsonObject agent;
    agent.insert(QStringLiteral("model"),
                 QStringLiteral("anthropic/claude-sonnet-4-6"));
    agent.insert(QStringLiteral("prompt"),
                 QStringLiteral("You are the primary Build agent."));
    agent.insert(QStringLiteral("mode"), QStringLiteral("primary"));

    QJsonObject perm;
    perm.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    agent.insert(QStringLiteral("permission"), perm);

    QJsonObject agents;
    agents.insert(QStringLiteral("build"), agent);
    cfg.insert(QStringLiteral("agent"), agents);
    cfg.insert(QStringLiteral("default_agent"), QStringLiteral("build"));
    return cfg;
}

QString firstErrorOrEmpty(const QJsonObject &cfg)
{
    QString err;
    ContractChecker::validate(cfg, &err);
    return err;
}

} // namespace

void TestContractChecker::validMinimalConfig_passes()
{
    const QJsonObject cfg = minimalValidConfig();
    QString err;
    QVERIFY2(ContractChecker::validate(cfg, &err),
             qPrintable(QStringLiteral("expected pass; error: %1").arg(err)));
    QVERIFY(err.isEmpty());
}

void TestContractChecker::missingSchema_failsWithUsefulMessage()
{
    QJsonObject cfg = minimalValidConfig();
    cfg.remove(QStringLiteral("$schema"));

    QString err;
    QVERIFY(!ContractChecker::validate(cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("$schema")),
             qPrintable(QStringLiteral(
                 "error message should mention $schema; got: %1").arg(err)));
    QVERIFY2(err.contains(QStringLiteral("https://opencode.ai/config.json")),
             qPrintable(QStringLiteral(
                 "error message should mention the literal URL; got: %1").arg(err)));
}

void TestContractChecker::unknownTopLevelKey_fails()
{
    QJsonObject cfg = minimalValidConfig();
    cfg.insert(QStringLiteral("totallyMadeUpKey"),
               QStringLiteral("something"));

    QString err;
    QVERIFY(!ContractChecker::validate(cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("totallyMadeUpKey")),
             qPrintable(QStringLiteral(
                 "error message should name the offending key; got: %1").arg(err)));
}

void TestContractChecker::illegalPermissionKey_fails()
{
    QJsonObject cfg = minimalValidConfig();
    QJsonObject agents = cfg.value(QStringLiteral("agent")).toObject();
    QJsonObject agent = agents.value(QStringLiteral("build")).toObject();
    QJsonObject perm = agent.value(QStringLiteral("permission")).toObject();

    // "rm" is NOT one of the 15 legal permission keys from report §6.1.
    perm.insert(QStringLiteral("rm"), QStringLiteral("allow"));
    agent.insert(QStringLiteral("permission"), perm);
    agents.insert(QStringLiteral("build"), agent);
    cfg.insert(QStringLiteral("agent"), agents);

    QString err;
    QVERIFY(!ContractChecker::validate(cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("rm")),
             qPrintable(QStringLiteral(
                 "error message should name the illegal permission key; got: %1").arg(err)));
}

void TestContractChecker::illegalAgentField_fails()
{
    QJsonObject cfg = minimalValidConfig();
    QJsonObject agents = cfg.value(QStringLiteral("agent")).toObject();
    QJsonObject agent = agents.value(QStringLiteral("build")).toObject();

    // "personality" is NOT in the report §7.1 KNOWN_KEYS set.
    agent.insert(QStringLiteral("personality"),
                 QStringLiteral("overrides-everything"));
    agents.insert(QStringLiteral("build"), agent);
    cfg.insert(QStringLiteral("agent"), agents);

    QString err;
    QVERIFY(!ContractChecker::validate(cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("personality")),
             qPrintable(QStringLiteral(
                 "error message should name the illegal agent field; got: %1").arg(err)));
}

void TestContractChecker::malformedModelString_fails()
{
    QJsonObject cfg = minimalValidConfig();
    QJsonObject agents = cfg.value(QStringLiteral("agent")).toObject();
    QJsonObject agent = agents.value(QStringLiteral("build")).toObject();

    // Missing slash — not parseable as provider/model and not even
    // structurally a string-of-two-segments.
    agent.insert(QStringLiteral("model"), QStringLiteral("bare-model-no-slash"));
    agents.insert(QStringLiteral("build"), agent);
    cfg.insert(QStringLiteral("agent"), agents);

    QString err;
    QVERIFY(!ContractChecker::validate(cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("model")) || err.contains(QStringLiteral("provider/model")),
             qPrintable(QStringLiteral(
                 "error message should mention the model field; got: %1").arg(err)));
}

QTEST_MAIN(TestContractChecker)
#include "test_contract_checker.moc"
