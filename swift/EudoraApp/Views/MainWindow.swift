// The main window: mailbox list on the left (the classic Mailboxes
// window), the message summary table on top (the classic mailbox window
// with its Status/Priority/Attachment/Label/Who/Date/K/Subject columns),
// and the preview pane below.

#if os(macOS)

import EudoraKit
import SwiftUI

struct MainWindow: View {
    @EnvironmentObject var model: AppModel
    @State private var selectedMessage: Int?
    @State private var newMailboxPrompt = false
    @State private var newMailboxName = ""

    var body: some View {
        NavigationSplitView {
            sidebar
        } detail: {
            if let boxName = model.selectedMailbox {
                MailboxPane(mailboxName: boxName, selectedMessage: $selectedMessage)
                    .navigationTitle(boxName)
            } else {
                Text("Select a mailbox")
                    .foregroundStyle(.secondary)
            }
        }
        .toolbar { toolbarContent }
        .safeAreaInset(edge: .bottom) { statusBar }
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
