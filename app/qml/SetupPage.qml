import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Kante
import Kladde.Core

// First start (and after signing out): the HealthLog address, then the sign-in, then the
// second factor if the account has one. As in the iOS app: server, account, done.
Kirigami.ScrollablePage {
    id: page
    title: qsTr("Connect HealthLog")

    readonly property bool needsMfa: account.state === Account.NeedsMfa
    readonly property bool working: account.state === Account.SigningIn || account.state === Account.Restoring

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        KanteHeading {
            level: 4
            text: qsTr("Server")
        }
        KanteTextField {
            id: serverField
            Layout.fillWidth: true
            placeholderText: "https://hl.example.org"
            text: account.server
            enabled: !page.needsMfa && !page.working
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            onEditingFinished: account.server = text
            Accessible.name: qsTr("Server address")
        }
        RowLayout {
            Layout.fillWidth: true
            KanteButton {
                text: qsTr("Check")
                busy: account.probe === Account.Checking
                enabled: serverField.text.length > 0 && !busy && !page.needsMfa
                onClicked: {
                    account.server = serverField.text
                    account.checkServer()
                }
            }
            KanteStatusLight {
                Layout.fillWidth: true
                visible: account.probe !== Account.Unknown
                status: account.probe === Account.Reachable ? KanteStatusLight.State.Ok
                      : account.probe === Account.Checking ? KanteStatusLight.State.Warn
                      : KanteStatusLight.State.Bad
                name: qsTr("HealthLog")
                detail: account.probe === Account.Reachable ? qsTr("Version %1").arg(account.serverVersion)
                      : account.probe === Account.Checking ? qsTr("Checking …")
                      : account.probeError
            }
        }

        KanteHeading {
            level: 4
            text: page.needsMfa ? qsTr("Second factor") : qsTr("Account")
        }
        KanteTextField {
            id: emailField
            Layout.fillWidth: true
            visible: !page.needsMfa
            placeholderText: qsTr("E-mail or user name")
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            Accessible.name: placeholderText
        }
        KanteTextField {
            id: passwordField
            Layout.fillWidth: true
            visible: !page.needsMfa
            placeholderText: qsTr("Password")
            echoMode: TextInput.Password
            Accessible.name: placeholderText
            onAccepted: signIn.clicked()
        }
        KanteTextField {
            id: codeField
            Layout.fillWidth: true
            visible: page.needsMfa
            placeholderText: recovery.checked ? qsTr("Recovery code") : qsTr("Code from the authenticator app")
            inputMethodHints: recovery.checked ? Qt.ImhNoAutoUppercase : Qt.ImhDigitsOnly
            Accessible.name: placeholderText
            onAccepted: signIn.clicked()
        }
        KanteSwitch {
            id: recovery
            visible: page.needsMfa && account.recoveryAllowed
            text: qsTr("Use a recovery code")
        }
        KanteButton {
            id: signIn
            Layout.fillWidth: true
            text: page.needsMfa ? qsTr("Confirm") : qsTr("Sign in")
            emphasis: KanteButton.Emphasis.Primary
            busy: page.working
            enabled: !busy && (page.needsMfa ? codeField.text.length > 0
                                             : serverField.text.length > 0 && emailField.text.length > 0
                                               && passwordField.text.length > 0)
            onClicked: {
                if (page.needsMfa) {
                    account.verifyMfa(codeField.text, recovery.checked)
                    codeField.text = ""
                } else {
                    account.server = serverField.text
                    account.login(emailField.text, passwordField.text)
                    passwordField.text = ""
                }
            }
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: account.error.length > 0
            text: account.error
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            color: KanteStyle.mutedTextColor
            text: qsTr("Kladde keeps only a renewable sign-in in the system keychain, never the password. On Android it creates its own measurement token for the upload in the background, visible and revocable in HealthLog.")
        }
    }
}
