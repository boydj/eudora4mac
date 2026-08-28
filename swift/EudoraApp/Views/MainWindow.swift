// The main window: mailbox list on the left (the classic Mailboxes
// window), the message summary table on top (the classic mailbox window
// with its Status/Priority/Attachment/Label/Who/Date/K/Subject columns),
// and the preview pane below.

#if os(macOS)

import AppKit
import EudoraKit
import SwiftUI

struct MainWindow: View {
    @EnvironmentObject var model: AppModel
    @State private var newMailboxPrompt = false
    @State private var newMailboxName = ""

    var body: some View {
        NavigationSplitView {
            sidebar
        } detail: {
            if let boxName = model.selectedMailbox {
                // Selection lives on the model so menu commands can act on it.
                MailboxPane(mailboxName: boxName,
                            selectedMessage: $model.selectedMessage)
                    .navigationTitle(boxName)
            } else {
                Text("Select a mailbox")
                    .foregroundStyle(.secondary)
            }
        }
        .toolbar { toolbarContent }
        .safeAreaInset(edge: .bottom) { statusBar }
        .onChange(of: model.pendingMessageOpens) { pending in
            // A filter's Open action queues messages here; open one window
            // each and drain the queue.
            guard !pending.isEmpty else { return }
            for ref in pending {
                openWindow(id: "message",
                           value: MessageRef(mailbox: ref.mailbox,
                                             index: ref.index,
                                             serial: ref.serial))
            }
            model.pendingMessageOpens.removeAll()
        }
        .alert("New Mailbox", isPresented: $newMailboxPrompt) {
            TextField("Name", text: $newMailboxName)
            Button("Create") {
                model.newMailbox(named: newMailboxName)
                newMailboxName = ""
            }
            Button("Cancel", role: .cancel) { newMailboxName = "" }
        } message: {
            Text("Creating a mailbox called:")
        }
        .task { await runScreenshotDirectorIfRequested() }
    }

    /// Screenshot support (used only by the Screenshots CI action): when the
    /// EUDORA_SHOT environment variable names a screen, drive the UI to it on
    /// launch so `screencapture` grabs a deterministic view.  A no-op in
    /// normal use.
    @MainActor
    private func runScreenshotDirectorIfRequested() async {
        guard let shot = ProcessInfo.processInfo.environment["EUDORA_SHOT"],
              !shot.isEmpty else { return }
        // Let the window and mailbox list build first.
        try? await Task.sleep(nanoseconds: 1_800_000_000)
        model.selectedMailbox = "In"

        func firstInboxRef() -> MessageRef? {
            guard let mb = model.mailbox(named: "In"), mb.count > 0,
                  let sum = mb.summary(at: 0) else { return nil }
            return MessageRef(mailbox: "In", index: 0, serial: sum.serialNumber)
        }

        switch shot {
        case "inbox", "main":
            // Select the first message so the preview shows an open email.
            model.selectedMessage = 0
        case "message":
            model.selectedMessage = 0
            if let ref = firstInboxRef() { openWindow(id: "message", value: ref) }
            hideMainWindow()
        case "compose":
            if let seed = model.composeSeed(.reply, mailbox: "In", index: 0) {
                openWindow(id: "compose", value: seed)
            } else {
                openWindow(id: "compose")
            }
            hideMainWindow()
        case "filters":
            openWindow(id: "filters")
            hideMainWindow()
        case "addressbook":
            openWindow(id: "addressbook")
            hideMainWindow()
        case "settings":
            if !NSApp.sendAction(Selector(("showSettingsWindow:")), to: nil, from: nil) {
                _ = NSApp.sendAction(Selector(("showPreferencesWindow:")), to: nil, from: nil)
            }
            hideMainWindow()
        default:
            break
        }
    }

    /// Orders every window except the newly-opened front one out of view, so
    /// a full-screen capture shows just the target screen.  Best-effort.
    private func hideMainWindow() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) {
            let front = NSApp.keyWindow ?? NSApp.orderedWindows.first
            for window in NSApp.windows
            where window != front && window.isVisible && window.canBecomeKey {
                window.orderOut(nil)
            }
            front?.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
        }
    }

    private var sidebar: some View {
        List(selection: $model.selectedMailbox) {
            Section("Mailboxes") {
                ForEach(model.mailboxNames, id: \.self) { name in
                    Label {
                        HStack {
                            Text(name)
                            Spacer()
                            UnreadBadge(mailboxName: name)
                        }
                    } icon: {
                        Image(systemName: iconName(for: name))
                    }
                    .tag(name)
                }
            }
        }
        .frame(minWidth: 170)
        .contextMenu {
            Button("New Mailbox…") { newMailboxPrompt = true }
        }
    }

    private func iconName(for name: String) -> String {
        switch name {
        case "In": return "tray.and.arrow.down"
        case "Out": return "tray.and.arrow.up"
        case "Trash": return "trash"
        case "Junk": return "xmark.bin"
        default: return "envelope"
        }
    }

    @ToolbarContentBuilder
    private var toolbarContent: some ToolbarContent {
        ToolbarItemGroup {
            Button {
                model.checkMail()
            } label: {
                Label("Check Mail", systemImage: "envelope.arrow.triangle.branch")
            }
            .disabled(model.isCheckingMail)
            .help("Check Mail (the classic ⌘M)")

            Button {
                model.sendQueuedMessages()
            } label: {
                Label("Send Queued", systemImage: "paperplane")
            }
            .help("Send Queued Messages")

            Button {
                openWindow(id: "compose")
            } label: {
                Label("New Message", systemImage: "square.and.pencil")
            }
            .help("New Message (the classic ⌘N)")
        }
    }

    @Environment(\.openWindow) private var openWindow

    private var statusBar: some View {
        HStack {
            if model.isCheckingMail {
                ProgressView().controlSize(.small)
            }
            Text(model.statusText)
                .font(.callout)
                .foregroundStyle(.secondary)
                .lineLimit(1)
            if model.isCheckingMail {
                Button("Stop") { model.stopCheckingMail() }
                    .controlSize(.small)
                    .help("Stop checking mail; messages already retrieved are kept.")
            }
            Spacer()
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 4)
        .background(.bar)
    }
}

private struct UnreadBadge: View {
    @EnvironmentObject var model: AppModel

    let mailboxName: String

    var body: some View {
        let _ = model.mailboxGeneration // re-evaluate on change
        if let mb = model.mailbox(named: mailboxName) {
            let unread = mb.summaries.filter { $0.state == .unread }.count
            if unread > 0 {
                Text("\(unread)")
                    .font(.caption2.bold())
                    .foregroundStyle(.secondary)
            }
        }
    }
}

#endif // os(macOS)
