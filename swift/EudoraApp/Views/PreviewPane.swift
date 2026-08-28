// The preview pane: the classic header strip (Who/Date/Subject) over the
// decoded message body.

#if os(macOS)

import EudoraKit
import SwiftUI

struct PreviewPane: View {
    @EnvironmentObject var model: AppModel

    let mailboxName: String
    let messageIndex: Int?

    var body: some View {
        let _ = model.mailboxGeneration
        if let index = messageIndex,
           let mb = model.mailbox(named: mailboxName),
           index < mb.count,
           let summary = mb.summary(at: index),
           let raw = try? mb.rawMessage(at: index),
           let message = try? ParsedMessage(raw: raw) {
            VStack(alignment: .leading, spacing: 0) {
                headerStrip(summary: summary, message: message)
                Divider()
                ScrollView {
                    Text(bodyText(of: message))
                        .font(.system(size: CGFloat(model.settings.displayFontSize))
                            .monospaced())
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(10)
                }
            }
        } else {
            VStack {
                Spacer()
                Text("No message selected")
                    .foregroundStyle(.secondary)
                Spacer()
            }
            .frame(maxWidth: .infinity)
        }
    }

    private func headerStrip(summary: MessageSummary,
                             message: ParsedMessage) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(message.decodedHeader("From"))
                    .fontWeight(.semibold)
                Spacer()
                Text(summary.date.formatted(date: .abbreviated, time: .shortened))
                    .foregroundStyle(.secondary)
            }
            Text(message.decodedHeader("Subject"))
            let to = message.decodedHeader("To")
            if !to.isEmpty {
                Text("To: \(to)")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        }
        .padding(8)
        .background(.quaternary.opacity(0.4))
    }

    private func bodyText(of message: ParsedMessage) -> String {
        // The mailbox stores CR-terminated lines; normalize for display.
        var text = message.decodedBody
        text = text.replacingOccurrences(of: "\r\n", with: "\n")
        text = text.replacingOccurrences(of: "\r", with: "\n")
        return text
    }
}

#endif // os(macOS)
