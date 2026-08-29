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

        // The classic Eudora icon.  The bundled app carries it at
        // Contents/Resources/Eudora.icns (also its CFBundleIconFile), so read
        // it from Bundle.main — NOT Bundle.module, whose SwiftPM accessor
        // fatalErrors at launch when the resource bundle isn't found on an
        // end user's machine.  Bundle.main.url returns nil instead, so a
        // `swift run` (where the icns isn't beside the binary) just skips it.
        if let url = Bundle.main.url(forResource: "Eudora", withExtension: "icns"),
           let icon = NSImage(contentsOf: url) {
            NSApp.applicationIconImage = icon
        }
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

        // Value-based groups: each action opens a fresh window whose @State
        // initializes from the value (a single Window scene cannot do this).
        WindowGroup("Composition", id: "compose", for: ComposeSeed.self) { $seed in
            // With defaultValue: provided, the binding is non-optional.
            ComposeView(seed: seed)
                .environmentObject(model)
        } defaultValue: {
            ComposeSeed()
        }

        WindowGroup("Message", id: "message", for: MessageRef.self) { $ref in
            if let ref {
                MessageWindow(reference: ref)
                    .environmentObject(model)
            }
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

        // A plain window hosting the same SettingsView, used only by the
        // screenshot director (the Settings *scene* can't be opened by
        // openWindow, and showSettingsWindow: doesn't fire on a headless
        // runner).  Nothing in the UI opens this, so a normal launch never
        // shows it; ⌘, still opens the real Settings scene above.
        Window("Settings", id: "settings-shot") {
            SettingsView()
                .environmentObject(model)
                .frame(minWidth: 720, minHeight: 480)
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
            Button("Reply") { composeFromSelection(.reply) }
                .keyboardShortcut("r", modifiers: [.command])
                .disabled(model.selectedMessage == nil)
            Button("Reply to All") { composeFromSelection(.replyAll) }
                .keyboardShortcut("r", modifiers: [.command, .shift])
                .disabled(model.selectedMessage == nil)
            Button("Forward") { composeFromSelection(.forward) }
                .disabled(model.selectedMessage == nil)
            Button("Redirect") { composeFromSelection(.redirect) }
                .disabled(model.selectedMessage == nil)
            Button("Send Again") { composeFromSelection(.sendAgain) }
                .disabled(model.selectedMessage == nil)
            Divider()
            Menu("Change Priority") {
                ForEach(1..<6) { p in
                    Button(["Highest", "High", "Normal", "Low", "Lowest"][p - 1]) {
                        if let box = model.selectedMailbox,
                           let index = model.selectedMessage {
                            model.setPriority(p, messageAt: index, in: box)
                        }
                    }
                }
            }
            .disabled(model.selectedMessage == nil)
            Button("Junk") { junkSelection(true) }
                .keyboardShortcut("j", modifiers: [.command, .shift])
                .disabled(model.selectedMessage == nil)
            Button("Not Junk") { junkSelection(false) }
                .disabled(model.selectedMessage == nil)
            Button("Make Address Book Entry") {
                if let box = model.selectedMailbox,
                   let index = model.selectedMessage {
                    model.makeAddressBookEntry(mailbox: box, index: index)
                }
            }
            .keyboardShortcut("k", modifiers: [.command])
            .disabled(model.selectedMessage == nil)
            Button("Print…") {
                if let box = model.selectedMailbox,
                   let index = model.selectedMessage {
                    model.printMessages(at: [index], in: box)
                }
            }
            .keyboardShortcut("p", modifiers: [.command])
            .disabled(model.selectedMessage == nil)
            Divider()
            Button("Filter Messages") {
                if let box = model.selectedMailbox {
                    model.runFilters(on: box)
                }
            }
            .keyboardShortcut("j", modifiers: [.command])
            Button("Delete") {
                if let box = model.selectedMailbox,
                   let index = model.selectedMessage {
                    model.delete(messageAt: index, from: box)
                    model.selectedMessage = nil
                }
            }
            .keyboardShortcut(.delete, modifiers: [.command])
            .disabled(model.selectedMessage == nil)
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

    private func composeFromSelection(_ kind: AppModel.ComposeActionKind) {
        guard let box = model.selectedMailbox,
              let index = model.selectedMessage,
              let seed = model.composeSeed(kind, mailbox: box, index: index)
        else { return }
        openWindow(id: "compose", value: seed)
    }

    private func junkSelection(_ junk: Bool) {
        guard let box = model.selectedMailbox,
              let index = model.selectedMessage else { return }
        model.markJunk(messageAt: index, from: box, junk: junk)
        model.selectedMessage = nil
    }
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
