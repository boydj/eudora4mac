// One mailbox: the summary table with the classic columns, over the
// preview pane (the drawer-era split the original used).  The table sorts
// by column, filters by a search field, and its actions work across a
// multi-message selection.

#if os(macOS)

import AppKit
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
    @State private var sortOrder = [KeyPathComparator(\SummaryRow.summary.date,
                                                      order: .reverse)]
    @State private var searchText = ""

    private var rows: [SummaryRow] {
        _ = model.mailboxGeneration
        guard let mb = model.mailbox(named: mailboxName) else { return [] }
        var out = mb.summaries.map { SummaryRow(id: $0.index, summary: $0) }
        if !searchText.isEmpty {
            let q = searchText.lowercased()
            out = out.filter {
                $0.summary.from.lowercased().contains(q) ||
                $0.summary.subject.lowercased().contains(q)
            }
        }
        out.sort(using: sortOrder)
        return out
    }

    var body: some View {
        VSplitView {
            table
                .frame(minHeight: 160)
            PreviewPane(mailboxName: mailboxName,
                        messageIndex: tableSelection.first)
                .frame(minHeight: 120)
        }
        .searchable(text: $searchText, placement: .toolbar,
                    prompt: "Search From and Subject")
    }

    private var table: some View {
        Table(rows, selection: $tableSelection, sortOrder: $sortOrder) {
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

            TableColumn("Who", value: \.summary.from) { row in
                Text(row.summary.from)
                    .fontWeight(row.summary.state == .unread ? .semibold : .regular)
            }
            .width(min: 120, ideal: 170)

            TableColumn("Date", value: \.summary.date) { row in
                Text(ClassicStyle.summaryDate(row.summary.date))
            }
            .width(min: 70, ideal: 110)

            TableColumn("K", value: \.summary.length) { row in
                Text(ClassicStyle.sizeK(row.summary.length))
                    .monospacedDigit()
            }
            .width(44)

            TableColumn("Subject", value: \.summary.subject) { row in
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
            deleteSelection()
        }
        .onChange(of: tableSelection) { newValue in
            if newValue.first != selectedMessage { selectedMessage = newValue.first }
        }
        // Reverse sync: a selection set on the model (menu commands, the
        // screenshot director) highlights the row and shows it in the preview.
        .onChange(of: selectedMessage) { newValue in
            let desired = newValue.map { Set([$0]) } ?? Set<Int>()
            if desired != tableSelection { tableSelection = desired }
        }
    }

    // MARK: selection helpers (act across the whole multi-selection)

    /// The selected TOC indices, highest first so deletes/transfers stay valid.
    private func selectedDescending(_ selection: Set<Int>) -> [Int] {
        selection.sorted(by: >)
    }

    private func deleteSelection() {
        for index in selectedDescending(tableSelection) {
            model.delete(messageAt: index, from: mailboxName)
        }
        tableSelection.removeAll()
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
            let multi = selection.count > 1
            Button(multi ? "Open \(selection.count) Messages" : "Open") {
                for i in selection { openMessage(at: i) }
            }
            Divider()
            Button("Reply") { compose(.reply, index) }
            Button("Reply to All") { compose(.replyAll, index) }
            Button("Forward") { compose(.forward, index) }
            Button("Redirect") { compose(.redirect, index) }
            Button("Send Again") { compose(.sendAgain, index) }
            Button("Print…") { printMessages(selection) }
            Divider()
            Menu("Status") {
                statusButton("Unread", .unread, selection)
                statusButton("Read", .read, selection)
                statusButton("Replied", .replied, selection)
                statusButton("Forwarded", .forwarded, selection)
            }
            Menu("Priority") {
                ForEach(1..<6) { p in
                    Button(["Highest", "High", "Normal", "Low", "Lowest"][p - 1]) {
                        for i in selection {
                            model.setPriority(p, messageAt: i, in: mailboxName)
                        }
                    }
                }
            }
            Menu("Label") {
                ForEach(0..<model.settings.labels.count, id: \.self) { i in
                    Button {
                        setLabel(i, across: selection)
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
                        for i in selectedDescending(selection) {
                            model.transfer(messageAt: i, from: mailboxName, to: target)
                        }
                        tableSelection.removeAll()
                    }
                }
            }
            Divider()
            Button(mailboxName == "Junk" ? "Not Junk" : "Junk") {
                for i in selectedDescending(selection) {
                    model.markJunk(messageAt: i, from: mailboxName,
                                   junk: mailboxName != "Junk")
                }
                tableSelection.removeAll()
            }
            Button("Make Address Book Entry") {
                model.makeAddressBookEntry(mailbox: mailboxName, index: index)
            }
            Divider()
            Button(multi ? "Delete \(selection.count) Messages" : "Delete",
                   role: .destructive) {
                for i in selectedDescending(selection) {
                    model.delete(messageAt: i, from: mailboxName)
                }
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
                              _ selection: Set<Int>) -> some View {
        Button(title) {
            guard let mb = model.mailbox(named: mailboxName) else { return }
            for i in selection { mb.setState(state, at: i) }
            try? mb.save()
            model.mailboxGeneration += 1
        }
    }

    private func setLabel(_ label: Int, across selection: Set<Int>) {
        guard let mb = model.mailbox(named: mailboxName) else { return }
        for i in selection { mb.setLabel(label, at: i) }
        try? mb.save()
        model.mailboxGeneration += 1
    }

    private func printMessages(_ selection: Set<Int>) {
        model.printMessages(at: Array(selection), in: mailboxName)
    }
}

#endif // os(macOS)
