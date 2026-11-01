#pragma once

// AndroidBackFilter: Back on the first page leaves the app. Android only; taken from
// Plasmai (app/platform/androidbackfilter.h).

#include <QEvent>
#include <QQmlApplicationEngine>
#include <QKeyEvent>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

// Qt 6.11 turns Android's Back key into the StandardKey.Back shortcut, which Kirigami's
// PageRow takes even on the first page (Back did nothing there), while a Back nothing takes
// finishes the activity (with the drawer open, Back quit the app). Main.qml's androidBack()
// decides per press: "pass" leaves it to Qt/Kirigami (subpages, dialogs), "handled" means it
// closed something itself, "leave" sends the app to the background, as Android does for
// launcher activities since 12. One decision per press, at its first event.
class AndroidBackFilter : public QObject {
public:
    AndroidBackFilter(QQmlApplicationEngine *engine) : QObject(engine), m_engine(engine) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        const QEvent::Type type = event->type();
        if ((type != QEvent::ShortcutOverride && type != QEvent::KeyPress && type != QEvent::KeyRelease)
            || static_cast<QKeyEvent *>(event)->key() != Qt::Key_Back) {
            return QObject::eventFilter(watched, event);
        }
        if (m_action.isEmpty()) {
            if (type == QEvent::KeyRelease) {
                return QObject::eventFilter(watched, event); // a press we did not see
            }
            m_action = decide();
        }
        const QString action = m_action;
        if (type == QEvent::KeyRelease) {
            m_action.clear();
        }
        if (action == QLatin1String("pass")) {
            return QObject::eventFilter(watched, event);
        }
        event->accept(); // for ShortcutOverride: the key is ours, the Back shortcut stays quiet
        if (type == QEvent::KeyRelease && action == QLatin1String("leave")) {
            QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
                QJniObject activity = QNativeInterface::QAndroidApplication::context();
                activity.callMethod<jboolean>("moveTaskToBack", "(Z)Z", jboolean(true));
            });
        }
        return true;
    }

private:
    QString decide() const {
        const QList<QObject *> roots = m_engine->rootObjects();
        QVariant result;
        if (roots.isEmpty()
            || !QMetaObject::invokeMethod(roots.first(), "androidBack", Q_RETURN_ARG(QVariant, result))) {
            return QStringLiteral("pass");
        }
        return result.toString();
    }

    QQmlApplicationEngine *m_engine;
    QString m_action; // decision for the press in progress
};
