// The preview pane: the classic header strip (Who/Date/Subject) over the
// decoded message body.

#if os(macOS)

import AppKit
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
                let attachments = message.attachments
                if !attachments.isEmpty {
                    attachmentStrip(attachments)
                    Divider()
                }
                if message.prefersHTML, let html = message.htmlBody {
                    HTMLMessageView(html: html,
                                    fontSize: CGFloat(model.settings.displayFontSize))
                } else {
                    ScrollView {
                        Text(bodyText(of: message))
                            .font(.system(size: CGFloat(model.settings.displayFontSize))
                                .monospaced())
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(10)
                    }
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
        // The readable text part of a multipart message (not the raw
        // boundary soup); CR-terminated lines normalized for display.
        var text = message.bestBodyText
        text = text.replacingOccurrences(of: "\r\n", with: "\n")
        text = text.replacingOccurrences(of: "\r", with: "\n")
        return text
    }

    // MARK: attachments

    private func attachmentStrip(_ parts: [MessagePart]) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(parts, id: \.index) { part in
                    Button {
                        save(part)
                    } label: {
                        HStack(spacing: 4) {
                            Image(systemName: "paperclip")
                            Text(part.filename.isEmpty
                                 ? "\(part.type)/\(part.subtype)"
                                 : part.filename)
                            Text(ClassicStyle.sizeK(part.size))
                                .foregroundStyle(.secondary)
                        }
                        .font(.callout)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(.quaternary,
                                    in: RoundedRectangle(cornerRadius: 5))
                    }
                    .buttonStyle(.plain)
                    .help("Save attachment…")
                }
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
        }
        .background(.quaternary.opacity(0.2))
    }

    private func save(_ part: MessagePart) {
        let panel = NSSavePanel()
        panel.nameFieldStringValue =
            part.filename.isEmpty ? "attachment" : part.filename
        panel.begin { response in
            guard response == .OK, let url = panel.url else { return }
            try? part.decode().write(to: url)
        }
    }
}

#endif // os(macOS)
