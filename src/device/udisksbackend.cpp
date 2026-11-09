// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "udisksbackend.h"

#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <limits>

namespace tacetos::device {

namespace {

const QString kObjectManagerPath = QStringLiteral("/org/freedesktop/UDisks2");

// An object path becomes a string, and a value that came out of a D-Bus
// message as a QDBusArgument (an array of byte strings) becomes a Qt type.
// Other values are left alone.
QVariant plain(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    if (!value.canConvert<QDBusArgument>())
        return value;
    const QDBusArgument argument = value.value<QDBusArgument>();
    const QString signature = argument.currentSignature();
    if (signature == QLatin1String("ay"))
        return QVariant::fromValue(qdbus_cast<QByteArray>(argument));
    if (signature == QLatin1String("aay"))
        return QVariant::fromValue(qdbus_cast<QByteArrayList>(argument));
    return value;
}

UdisksReply errorOf(const QDBusMessage &message)
{
    UdisksReply reply;
    reply.errorName = message.errorName();
    reply.errorMessage = message.errorMessage();
    // A reply of the wrong kind has no error name: never take it for a success.
    if (reply.errorName.isEmpty())
        reply.errorName = QStringLiteral("org.freedesktop.DBus.Error.Failed");
    return reply;
}

} // namespace

UdisksDBusBackend::UdisksDBusBackend(QDBusConnection bus, QString service)
    : bus_(std::move(bus)), service_(std::move(service))
{
}

std::optional<UdisksObjects> UdisksDBusBackend::managedObjects(UdisksReply *error)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        service_, kObjectManagerPath, QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    const QDBusMessage answer = bus_.call(message);
    if (answer.type() != QDBusMessage::ReplyMessage || answer.arguments().isEmpty()) {
        if (error)
            *error = errorOf(answer);
        return std::nullopt;
    }

    // a{oa{sa{sv}}}
    UdisksObjects objects;
    const QDBusArgument argument = answer.arguments().first().value<QDBusArgument>();
    argument.beginMap();
    while (!argument.atEnd()) {
        argument.beginMapEntry();
        QDBusObjectPath path;
        argument >> path;
        QMap<QString, QVariantMap> interfaces;
        argument.beginMap();
        while (!argument.atEnd()) {
            argument.beginMapEntry();
            QString name;
            QVariantMap properties;
            argument >> name >> properties;
            for (auto it = properties.begin(); it != properties.end(); ++it)
                it.value() = plain(it.value());
            interfaces.insert(name, properties);
            argument.endMapEntry();
        }
        argument.endMap();
        argument.endMapEntry();
        objects.insert(path.path(), interfaces);
    }
    argument.endMap();
    return objects;
}

UdisksReply UdisksDBusBackend::call(const QString &path, const QString &interface,
                                    const QString &method, const QVariantMap &options)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service_, path, interface, method);
    message << options;

    // The user may be asked for a permission: wait for as long as it takes.
    // For Qt, -1 is the default limit of 25 s; the largest value means none.
    const QDBusMessage answer = bus_.call(message, QDBus::Block, std::numeric_limits<int>::max());
    if (answer.type() != QDBusMessage::ReplyMessage)
        return errorOf(answer);
    UdisksReply reply;
    if (!answer.arguments().isEmpty())
        reply.value = answer.arguments().first().toString();
    return reply;
}

UdisksReply UdisksDBusBackend::mount(const QString &blockPath, bool readOnly)
{
    QVariantMap options;
    if (readOnly)
        options.insert(QStringLiteral("options"), QStringLiteral("ro"));
    return call(blockPath, QStringLiteral("org.freedesktop.UDisks2.Filesystem"),
                QStringLiteral("Mount"), options);
}

UdisksReply UdisksDBusBackend::unmount(const QString &blockPath)
{
    // No force: a volume that is in use stays, and the user is told.
    return call(blockPath, QStringLiteral("org.freedesktop.UDisks2.Filesystem"),
                QStringLiteral("Unmount"), QVariantMap());
}

UdisksReply UdisksDBusBackend::eject(const QString &drivePath)
{
    return call(drivePath, QStringLiteral("org.freedesktop.UDisks2.Drive"), QStringLiteral("Eject"),
                QVariantMap());
}

UdisksReply UdisksDBusBackend::powerOff(const QString &drivePath)
{
    return call(drivePath, QStringLiteral("org.freedesktop.UDisks2.Drive"),
                QStringLiteral("PowerOff"), QVariantMap());
}

} // namespace tacetos::device
