// kladde-push: maps a Health Connect export (spikes/hc-probe) and sends it to HealthLog.
// The end-to-end check of phase 0 on the desktop, without the app.
//
//   kladde-push [--server URL] [--zone Europe/Berlin] [--app coredevices.coreapp] [--dry-run] export.json
//
// The token comes from the environment (KLADDE_TOKEN), never from the command line, so it
// stays out of the shell history. A HealthLog token with the scope measurements:write.

#include "batch.h"
#include "hcjson.h"
#include "healthlogclient.h"
#include "mapper.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>

#include <memory>

using namespace kladde;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Send a Health Connect export to HealthLog"));
    cli.addHelpOption();
    QCommandLineOption serverOpt(QStringLiteral("server"), QStringLiteral("HealthLog URL"),
                                 QStringLiteral("url"), QStringLiteral("https://hl.arianw.de"));
    QCommandLineOption zoneOpt(QStringLiteral("zone"),
                               QStringLiteral("Account time zone for day totals "
                                              "(default: from the server, else this machine)"),
                               QStringLiteral("iana"));
    QCommandLineOption dryOpt(QStringLiteral("dry-run"),
                              QStringLiteral("Print the batch entries, send nothing"));
    QCommandLineOption appOpt(QStringLiteral("app"),
                              QStringLiteral("Only samples this app wrote (default: the Pebble app; "
                                             "empty for all)"),
                              QStringLiteral("package"), QStringLiteral("coredevices.coreapp"));
    cli.addOptions({serverOpt, zoneOpt, appOpt, dryOpt});
    cli.addPositionalArgument(QStringLiteral("export"), QStringLiteral("JSON from hc-probe"));
    cli.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    if (cli.positionalArguments().size() != 1)
        cli.showHelp(2);

    QFile file(cli.positionalArguments().constFirst());
    if (!file.open(QIODevice::ReadOnly)) {
        err << "cannot read " << file.fileName() << ": " << file.errorString() << Qt::endl;
        return 1;
    }
    ParseResult parsed = parseHealthConnectJson(file.readAll());
    if (!parsed.error.isEmpty()) {
        err << "bad export: " << parsed.error << Qt::endl;
        return 1;
    }
    for (const QString &w : parsed.warnings)
        err << "warning: " << w << Qt::endl;
    const QString onlyApp = cli.value(appOpt);
    if (!onlyApp.isEmpty()) {
        const qsizetype before = parsed.samples.size();
        parsed.samples.removeIf([&](const Sample &s) { return s.sourceApp != onlyApp; });
        err << "kept " << parsed.samples.size() << " of " << before << " samples from " << onlyApp
            << Qt::endl;
    }

    const QString token = qEnvironmentVariable("KLADDE_TOKEN");
    const bool dryRun = cli.isSet(dryOpt);
    if (!dryRun && token.isEmpty()) {
        err << "KLADDE_TOKEN is not set" << Qt::endl;
        return 1;
    }

    HealthLogClient client;
    client.setServer(QUrl(cli.value(serverOpt)));
    client.setToken(token);

    auto send = [&](const QTimeZone &zone) {
        const MapResult mapped = mapToBatchEntries(parsed.samples, zone);
        for (const QString &s : mapped.skipped)
            err << "skipped: " << s << Qt::endl;
        const QList<Batch> batches = makeBatches(mapped.entries, QStringLiteral("manual"));
        err << parsed.samples.size() << " samples -> " << mapped.entries.size() << " entries in "
            << batches.size() << " batches (zone " << zone.id() << ")" << Qt::endl;
        if (dryRun) {
            for (const Batch &b : batches)
                out << QJsonDocument::fromJson(b.body).toJson(QJsonDocument::Indented);
            QCoreApplication::exit(0);
            return;
        }
        if (batches.isEmpty()) {
            QCoreApplication::exit(0);
            return;
        }
        // One batch at a time: the server runs one ingest per account anyway.
        auto next = std::make_shared<qsizetype>(0);
        auto failed = std::make_shared<bool>(false);
        QObject::connect(&client, &HealthLogClient::batchFinished, &app,
                         [&, next, failed, batches](const QString &, const BatchResult &r) {
            const qsizetype i = *next;
            if (r.ok) {
                err << "batch " << i + 1 << "/" << batches.size() << ": " << r.inserted
                    << " new, " << r.updated << " updated, " << r.duplicates << " duplicates, "
                    << r.failed << " failed" << Qt::endl;
                for (const QString &s : r.skipped)
                    err << "  skipped " << s << Qt::endl;
            } else {
                err << "batch " << i + 1 << "/" << batches.size() << " failed: " << r.error
                    << (r.retryable() ? " (retry later)" : "") << Qt::endl;
                *failed = true;
            }
            if (++*next < batches.size() && r.ok)
                client.postBatch(batches.at(*next));
            else
                QCoreApplication::exit(*failed ? 1 : 0);
        });
        client.postBatch(batches.constFirst());
    };

    if (cli.isSet(zoneOpt)) {
        const QTimeZone zone(cli.value(zoneOpt).toUtf8());
        if (!zone.isValid()) {
            err << "unknown time zone " << cli.value(zoneOpt) << Qt::endl;
            return 2;
        }
        // Queued: exit() only ends a running event loop.
        QTimer::singleShot(0, &app, [&send, zone] { send(zone); });
    } else if (dryRun) {
        QTimer::singleShot(0, &app, [&send] { send(QTimeZone::systemTimeZone()); });
    } else {
        QObject::connect(&client, &HealthLogClient::meReceived, &app,
                         [&](bool ok, const QTimeZone &zone, const QString &error) {
            if (!ok)
                err << "account time zone unavailable (" << error
                    << "), using this machine's" << Qt::endl;
            send(ok ? zone : QTimeZone::systemTimeZone());
        });
        client.fetchMe();
    }
    return app.exec();
}
