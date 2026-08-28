// Getting Attention — what happens when new mail arrives: the modern
// PREF_NEW_ALERT / PREF_NEW_SOUND / menu-bar-flash behaviors.

#if os(macOS)

import AppKit
import UserNotifications

enum NewMailAttention {
    /// UNUserNotificationCenter aborts in a bare `swift run` executable
    /// (no bundle identifier, so there is no bundle proxy).  A bundled
    /// Xcode build gets real notifications; the bare build bounces the
    /// Dock icon instead.
    private static let canUseNotificationCenter =
        Bundle.main.bundleIdentifier != nil

    private static var requestedAuthorization = false

    @MainActor
    static func notify(count: Int, settings: EudoraSettings) {
        guard count > 0 else { return }
        if settings.newMailSound {
            playSound(named: settings.newMailSoundName)
        }
        let message = "You have \(count) new message\(count == 1 ? "" : "s")."
        if canUseNotificationCenter {
            postNotification(message)
        } else {
            NSApp.requestUserAttention(.informationalRequest)
        }
        if settings.newMailAlert {
            let alert = NSAlert()
            alert.messageText = "New Mail"
            alert.informativeText = message
            alert.runModal()
        }
    }

    @MainActor
    static func playSound(named name: String) {
        // NSSound resolves /System/Library/Sounds names without a bundle.
        if let sound = NSSound(named: NSSound.Name(name)) {
            sound.play()
        } else {
            NSSound.beep()
        }
    }

    @MainActor
    static func updateDockBadge(unread: Int, enabled: Bool) {
        NSApp.dockTile.badgeLabel = enabled && unread > 0 ? "\(unread)" : nil
    }

    @MainActor
    private static func postNotification(_ body: String) {
        let center = UNUserNotificationCenter.current()
        if !requestedAuthorization {
            // Only on first actual use, never at launch.
            requestedAuthorization = true
            center.requestAuthorization(options: [.alert, .sound, .badge]) { _, _ in }
        }
        let content = UNMutableNotificationContent()
        content.title = "Eudora"
        content.body = body
        center.add(UNNotificationRequest(identifier: UUID().uuidString,
                                         content: content, trigger: nil))
    }
}

#endif // os(macOS)
