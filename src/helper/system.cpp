// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "system.h"

#include "helperconstants.h"
#include "udevrule.h"

#include <QStandardPaths>

#if defined(TACETOS_HAVE_DBUS)
#include "udisksbackend.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <limits>
#include <unistd.h>
#endif

namespace tacetos::helper {

#if defined(TACETOS_HAVE_DBUS)
namespace {

// udisks2 over the system bus. Never used by the tests.
class UdisksService : public DiskService
{
public:
    QString version() override
    {
        QDBusMessage get = QDBusMessage::createMethodCall(
            QString::fromLatin1(tacetos::device::kUdisksService),
            QStringLiteral("/org/freedesktop/UDisks2/Manager"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        get << QStringLiteral("org.freedesktop.UDisks2.Manager") << QStringLiteral("Version");
        const QDBusMessage answer = QDBusConnection::systemBus().call(get);
        if (answer.type() != QDBusMessage::ReplyMessage || answer.arguments().isEmpty())
            return {};
        return answer.arguments().first().value<QDBusVariant>().variant().toString();
    }

    std::optional<tacetos::device::UdisksObjects> objects(QString *error) override
    {
        tacetos::device::UdisksDBusBackend backend;
        tacetos::device::UdisksReply reply;
        auto result = backend.managedObjects(&reply);
        if (!result && error)
            *error = reply.errorName + QLatin1Char(' ') + reply.errorMessage;
        return result;
    }

    OpenResult open(const QString &blockPath, bool write) override
    {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QString::fromLatin1(tacetos::device::kUdisksService), blockPath,
            QStringLiteral("org.freedesktop.UDisks2.Block"), QStringLiteral("OpenDevice"));
        call << QString(write ? QLatin1String("rw") : QLatin1String("r")) << QVariantMap();
        // The user may be asked for a password: no time limit.
        const QDBusMessage answer =
            QDBusConnection::systemBus().call(call, QDBus::Block, std::numeric_limits<int>::max());
        OpenResult out;
        if (answer.type() != QDBusMessage::ReplyMessage || answer.arguments().isEmpty()) {
            out.detail = answer.errorName() + QLatin1Char(' ') + answer.errorMessage();
            switch (tacetos::device::diskErrorFromName(answer.errorName())) {
            case tacetos::device::DiskError::NotAuthorized:
                out.code = QString(err::permissionDenied);
                break;
            case tacetos::device::DiskError::Busy:
                out.code = QString(err::inUse);
                break;
            default:
                out.code = QString(err::openFailed);
            }
            return out;
        }
        const auto descriptor = answer.arguments().first().value<QDBusUnixFileDescriptor>();
        out.fd = descriptor.isValid() ? ::dup(descriptor.fileDescriptor()) : -1;
        if (out.fd < 0) {
            out.code = QString(err::openFailed);
            out.detail = QStringLiteral("udisks2 gave no file");
        }
        return out;
    }
};

} // namespace
#endif

std::unique_ptr<SystemLayer> makeRealSystem()
{
    auto system = std::make_unique<SystemLayer>();
#if defined(Q_OS_LINUX)
    system->linuxSystem = true;
#if defined(TACETOS_HAVE_DBUS)
    system->disks = std::make_unique<UdisksService>();
#endif
    static const std::unique_ptr<ProcessRunner> runner = makeProcessRunner();
    system->udev.rulesFile = QString(kUdevRulesFile);
    system->udev.pkexecPath = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    system->udev.runner = runner.get();
#endif
    return system;
}

} // namespace tacetos::helper
