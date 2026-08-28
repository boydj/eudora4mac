// Eudora for macOS — the classic mail client rebuilt on EudoraCore.
//
// Scenes: the main mailbox browser, composition windows, the Filters and
// Address Book windows, and Settings.  The menu bar mirrors the classic
// Message / Special menus where the commands still make sense.

#if os(macOS)

import AppKit
import SwiftUI

// When launched as a bare executable (swift run) there is no app bundle,
// and macOS treats the process as a background accessory: windows but no
// menu bar or Dock icon.  Promote it to a regular foreground app so the
// menu bar (App menu with Settings…, Message, Special) appears.  A bundled
// Xcode build gets this for free; the calls are harmless there.
final class EudoraAppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }
}

@main
struct EudoraApp: App {
    @NSApplicationDelegateAdaptor(EudoraAppDelegate.self) private var appDelegate
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup("Eudora") {
            MainWindow()
                .environmentObject(model)
                .frame(minWidth: 760, minHeight: 480)
        }
        .commands { menuCommands }

        Window("Composition", id: "compose") {
            ComposeView()
                .environmentObject(model)
        }

        Window("Filters", id: "filters") {
            FiltersView()
                .environmentObject(model)
        }

        Window("Address Book", id: "addressbook") {
            AddressBookView()
                .environmentObject(model)
        }

        Settings {
            SettingsView()
                .environmentObject(model)
        }
    }

    @CommandsBuilder
    private var menuCommands: some Commands {
        CommandGroup(after: .newItem) {
            Button("New Message") { openWindow(id: "compose") }
                .keyboardShortcut("n", modifiers: [.command])
        }

        CommandMenu("Message") {
            Button("Check Mail") { model.checkMail() }
                .keyboardShortcut("m", modifiers: [.command])
                .disabled(model.isCheckingMail)
            Button("Send Queued Messages") { model.sendQueuedMessages() }
                .keyboardShortcut("t", modifiers: [.command])
            Divider()
            Button("Filter Messages") {
                if let box = model.selectedMailbox {
                    model.runFilters(on: box)
                }
            }
            .keyboardShortcut("j", modifiers: [.command])
        }

        CommandMenu("Special") {
            Button("Filters…") { openWindow(id: "filters") }
            Button("Address Book…") { openWindow(id: "addressbook") }
                .keyboardShortcut("l", modifiers: [.command])
            Divider()
            Button("Empty Trash") { model.emptyTrash() }
            Button("Compact Mailbox") {
                if let box = model.selectedMailbox {
                    model.compact(mailboxNamed: box)
                }
            }
        }
    }

    @Environment(\.openWindow) private var openWindow
}

#else

// SwiftUI is macOS-only; give other platforms a stub entry point so
// `swift build` of the whole package still succeeds.
@main
struct EudoraAppUnavailable {
    static func main() {
        print("EudoraApp requires macOS.")
    }
}

#endif // os(macOS)
