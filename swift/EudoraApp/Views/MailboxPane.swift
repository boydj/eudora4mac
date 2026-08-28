// One mailbox: the summary table with the classic columns, over the
// preview pane (the drawer-era split the original used).

#if os(macOS)

import EudoraKit
import SwiftUI

struct SummaryRow: Identifiable {
    let id: Int // index in the TOC
    let summary: MessageSummary
}

struct MailboxPane: View {
    @EnvironmentObject var model: AppModel

    let mailboxName: String
    @Binding var selectedMessage: Int?

    @State private var tableSelection = Set<Int>()

    private var rows: [SummaryRow] {
        _ = model.mailboxGeneration
        guard let mb = model.mailbox(named: mailboxName) else { return [] }
        return mb.summaries.map { SummaryRow(id: $0.index, summary: $0) }
    }

    var body: some View {
        VSplitView {
            table
                .frame(minHeight: 160)
            PreviewPane(mailboxName: mailboxName,
                        messageIndex: tableSelection.first)
                .frame(minHeight: 120)
        }
    }

    private var table: some View {
        Table(rows, selection: $tableSelection) {
            TableColumn("•") { row in
                Text(ClassicStyle.statusGlyph(for: row.summary.state))
                    .fontWeight(row.summary.state == .unread ? .bold : .regular)
            }
            .width(18)

            TableColumn("P") { row in
                Text(ClassicStyle.priorityGlyph(row.summary.priorityDisplay))
            }
            .width(18)

            TableColumn("A") { row in
                if row.summary.hasAttachments {
                    Image(systemName: "paperclip")
                }
            }
            .width(18)

            TableColumn("L") { row in
                if let color = model.settings.labelColor(row.summary.labelIndex) {
                    Circle().fill(color).frame(width: 10, height: 10)
                }
            }
            .width(18)

            TableColumn("Who") { row in
                Text(row.summary.from)
                    .fontWeight(row.summary.state == .unread ? .semibold : .regular)
            }
            .width(min: 120, ideal: 170)

            TableColumn("Date") { row in
                Text(ClassicStyle.summaryDate(row.summary.date))
            }
            .width(min: 70, ideal: 110)

            TableColumn("K") { row in
                Text(ClassicStyle.sizeK(row.summary.length))
                    .monospacedDigit()
            }
            .width(44)

            TableColumn("Subject") { row in
                Text(row.summary.subject)
                    .fontWeight(row.summary.state == .unread ? .semibold : .regular)
            }
            .width(min: 180, ideal: 340)
        }
        .font(.system(size: CGFloat(model.settings.displayFontSize)))
        .contextMenu(forSelectionType: Int.self) { selection in
            messageContextMenu(selection: selection)
        } primaryAction: { selection in
            // Double-click opens the message in its own window (classic).
            if let index = selection.first {
                openMessage(at: index)
            }
        }
        .onDeleteCommand {
            if let index = tableSelection.first {
                model.delete(messageAt: index, from: mailboxName)
                tableSelection.removeAll()
            }
        }
        .onChange(of: tableSelection) { newValue in
            selectedMessage = newValue.first
        }
    }

    private func openMessage(at index: Int) {
        guard let mb = model.mailbox(named: mailboxName),
              let sum = mb.summary(at: index) else { return }
        openWindow(id: "message",
                   value: MessageRef(mailbox: mailboxName, index: index,
                                     serial: sum.serialNumber))
    }

    @Environment(\.openWindow) private var openWindow

    @ViewBuilder
    private func messageContextMenu(selection: Set<Int>) -> some View {
        if let index = selection.first {
            Button("Open") { openMessage(at: index) }
            Divider()
            Button("Reply") { compose(.reply, index) }
            Button("Reply to All") { compose(.replyAll, index) }
            Button("Forward") { compose(.forward, index) }
            Button("Redirect") { compose(.redirect, index) }
            Button("Send Again") { compose(.sendAgain, index) }
            Divider()
            Menu("Status") {
                statusButton("Unread", .unread, index)
                statusButton("Read", .read, index)
                statusButton("Replied", .replied, index)
                statusButton("Forwarded", .forwarded, index)
            }
            Menu("Priority") {
                ForEach(1..<6) { p in
                    Button(["Highest", "High", "Normal", "Low", "Lowest"][p - 1]) {
                        model.setPriority(p, messageAt: index, in: mailboxName)
                    }
                }
            }
            Menu("Label") {
                ForEach(0..<model.settings.labels.count, id: \.self) { i in
                    Button {
                        setLabel(i, at: index)
                    } label: {
                        if let color = model.settings.labelColor(i) {
                            Label(model.settings.labelName(i),
                                  systemImage: "circle.fill")
                                .foregroundStyle(color)
                        } else {
                            Text(model.settings.labelName(i))
                        }
                    }
                }
            }
            Menu("Transfer") {
                ForEach(model.mailboxNames.filter { $0 != mailboxName }, id: \.self) { target in
                    Button(target) {
                        model.transfer(messageAt: index, from: mailboxName, to: target)
                    }
                }
            }
            Divider()
            Button(mailboxName == "Junk" ? "Not Junk" : "Junk") {
                model.markJunk(messageAt: index, from: mailboxName,
                               junk: mailboxName != "Junk")
                tableSelection.removeAll()
            }
            Button("Make Address Book Entry") {
                model.makeAddressBookEntry(mailbox: mailboxName, index: index)
            }
            Divider()
            Button("Delete", role: .destructive) {
                model.delete(messageAt: index, from: mailboxName)
                tableSelection.removeAll()
            }
        }
    }

    private func compose(_ kind: AppModel.ComposeActionKind, _ index: Int) {
        guard let seed = model.composeSeed(kind, mailbox: mailboxName,
                                           index: index) else { return }
        openWindow(id: "compose", value: seed)
    }

    private func statusButton(_ title: String, _ state: MessageState,
                              _ index: Int) -> some View {
        Button(title) {
            guard let mb = model.mailbox(named: mailboxName) else { return }
            mb.setState(state, at: index)
            try? mb.save()
            model.mailboxGeneration += 1
        }
    }

    private func setLabel(_ label: Int, at index: Int) {
        guard let mb = model.mailbox(named: mailboxName) else { return }
        mb.setLabel(label, at: index)
        try? mb.save()
        model.mailboxGeneration += 1
    }
}

#endif // os(macOS)
