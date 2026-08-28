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
            // Double-click opens (classic behavior); mark read.
            if let index = selection.first,
               let mb = model.mailbox(named: mailboxName) {
                mb.setState(.read, at: index)
                try? mb.save()
                model.mailboxGeneration += 1
            }
        }
        .onChange(of: tableSelection) { newValue in
            selectedMessage = newValue.first
        }
    }

    @ViewBuilder
    private func messageContextMenu(selection: Set<Int>) -> some View {
        if let index = selection.first {
            Menu("Status") {
                statusButton("Unread", .unread, index)
                statusButton("Read", .read, index)
                statusButton("Replied", .replied, index)
                statusButton("Forwarded", .forwarded, index)
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
            Button("Delete", role: .destructive) {
                model.delete(messageAt: index, from: mailboxName)
                tableSelection.removeAll()
            }
        }
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
