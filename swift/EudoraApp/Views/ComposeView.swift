// The composition window: Eudora's classic header block (From / To /
// Subject / Cc / Bcc / Attachments) over the body editor, with the
// Queue and Send buttons the original put in the icon bar.

#if os(macOS)

import CEudoraCore // eudora_core_version for the X-Mailer header
import EudoraKit
import SwiftUI
import UniformTypeIdentifiers

struct ComposeView: View {
    @EnvironmentObject var model: AppModel
    @Environment(\.dismiss) private var dismiss

    let seed: ComposeSeed

    @State private var to: String
    @State private var cc: String
    @State private var bcc: String
    @State private var subject: String
    @State private var bodyText: String
    @State private var priority: Int
    @State private var attachments: [URL] = []
    @State private var showImporter = false
    @State private var errorText: String?

    init(seed: ComposeSeed = ComposeSeed()) {
        self.seed = seed
        _to = State(initialValue: seed.to)
        _cc = State(initialValue: seed.cc)
        _bcc = State(initialValue: seed.bcc)
        _subject = State(initialValue: seed.subject)
        _bodyText = State(initialValue: seed.body)
        _priority = State(initialValue: seed.priority)
        _attachments = State(initialValue: seed.attachmentPaths.map {
            URL(fileURLWithPath: $0)
        })
    }

    var body: some View {
        VStack(spacing: 0) {
            iconBar
            Divider()
            headerGrid
            Divider()
            TextEditor(text: $bodyText)
                .font(.body.monospaced())
                .padding(4)
        }
        .frame(minWidth: 560, minHeight: 420)
        .navigationTitle(subject.isEmpty ? "No Subject" : subject)
        .fileImporter(isPresented: $showImporter,
                      allowedContentTypes: [UTType.item],
                      allowsMultipleSelection: true) { result in
            if case let .success(urls) = result {
                attachments.append(contentsOf: urls)
            }
        }
        .alert("Cannot Send", isPresented: .constant(errorText != nil)) {
            Button("OK") { errorText = nil }
        } message: {
            Text(errorText ?? "")
        }
    }

    // The classic icon bar: priority popup, Queue, Send.
    private var iconBar: some View {
        HStack {
            Picker("Priority", selection: $priority) {
                Text("Highest").tag(1)
                Text("High").tag(2)
                Text("Normal").tag(3)
                Text("Low").tag(4)
                Text("Lowest").tag(5)
            }
            .frame(width: 130)

            Button {
                showImporter = true
            } label: {
                Label("Attach", systemImage: "paperclip")
            }

            Spacer()

            Button("Queue") { queue() }
                .help("Save to Out as queued (the classic Queue button)")
            Button("Send") { send() }
                .keyboardShortcut(.return, modifiers: [.command])
                .buttonStyle(.borderedProminent)
        }
        .padding(8)
    }

    private var headerGrid: some View {
        Grid(alignment: .leadingFirstTextBaseline,
             horizontalSpacing: 8, verticalSpacing: 4) {
            GridRow {
                headerLabel("From:")
                Text(fromDisplay)
                    .foregroundStyle(.secondary)
                    .gridCellAnchor(.leading)
            }
            GridRow {
                headerLabel("To:")
                TextField("", text: $to).textFieldStyle(.plain)
            }
            GridRow {
                headerLabel("Subject:")
                TextField("", text: $subject).textFieldStyle(.plain)
            }
            GridRow {
                headerLabel("Cc:")
                TextField("", text: $cc).textFieldStyle(.plain)
            }
            GridRow {
                headerLabel("Bcc:")
                TextField("", text: $bcc).textFieldStyle(.plain)
            }
            GridRow {
                headerLabel("Attached:")
                attachmentRow
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(.quaternary.opacity(0.3))
    }

    private func headerLabel(_ text: String) -> some View {
        Text(text)
            .fontWeight(.semibold)
            .gridColumnAlignment(.trailing)
            .frame(width: 70, alignment: .trailing)
    }

    private var attachmentRow: some View {
        HStack {
            if attachments.isEmpty {
                Text("none").foregroundStyle(.tertiary)
            }
            ForEach(attachments, id: \.self) { url in
                HStack(spacing: 2) {
                    Image(systemName: "doc")
                    Text(url.lastPathComponent)
                    Button {
                        attachments.removeAll { $0 == url }
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                    }
                    .buttonStyle(.plain)
                }
                .font(.callout)
                .padding(.horizontal, 4)
                .background(.quaternary, in: RoundedRectangle(cornerRadius: 4))
            }
            Spacer()
        }
    }

    private var account: Personality { model.settings.dominant }

    private var fromDisplay: String {
        if let address = seed.fromAddress {
            let name = seed.fromName ?? ""
            return name.isEmpty ? address : "\(name) <\(address)>"
        }
        return account.realName.isEmpty
            ? account.emailAddress
            : "\(account.realName) <\(account.emailAddress)>"
    }

    private func buildMessage() -> (message: String, recipients: String)? {
        guard !to.isEmpty || !cc.isEmpty || !bcc.isEmpty else {
            errorText = "The message has no recipients."
            return nil
        }
        var body = bodyText
        // The classic signature: appended after "-- " for personalities
        // that use one (SIG_INTRO), except on redirects.
        if account.useSignature, seed.fromAddress == nil {
            let sig = model.signatureText(named: account.signatureName)
            if !sig.isEmpty {
                if !body.hasSuffix("\n") { body += "\n" }
                body += "\n-- \n" + sig
            }
        }
        let composer = Composer()
            .from(name: seed.fromAddress != nil ? (seed.fromName ?? "")
                                                : account.realName,
                  address: seed.fromAddress ?? account.emailAddress)
            .subject(subject)
            .body(body)
            .priority(priority)
            .header("X-Mailer", "Eudora (EudoraCore \(String(cString: eudora_core_version())))")
        if !to.isEmpty { composer.to(to) }
        if !cc.isEmpty { composer.cc(cc) }
        if !bcc.isEmpty { composer.bcc(bcc) }
        for extra in seed.extraHeaders {
            composer.header(extra.name, extra.value)
        }
        for url in attachments {
            composer.attach(path: url.path)
        }
        do {
            let message = try composer.build()
            return (message, composer.recipients)
        } catch {
            errorText = "\(error)"
            return nil
        }
    }

    private func queue() {
        guard let built = buildMessage() else { return }
        model.queue(message: built.message)
        if let original = seed.original {
            model.markOriginal(original)
        }
        dismiss()
    }

    private func send() {
        guard let built = buildMessage() else { return }
        let acct = account
        guard !acct.smtpHost.isEmpty else {
            errorText = "Set up an SMTP server in Settings first."
            return
        }
        let tls: TLSMode = {
            switch acct.smtpSecurity {
            case .none: return .none
            case .startTLS: return .startTLS
            case .immediateTLS: return .immediate
            }
        }()
        model.statusText = "Sending…"
        Task.detached {
            var failure: String?
            do {
                _ = try smtpSend(host: acct.smtpHost, port: acct.smtpPort, tls: tls,
                                 user: acct.username, password: acct.password,
                                 from: acct.emailAddress,
                                 recipients: built.recipients,
                                 message: built.message)
            } catch {
                failure = "\(error)"
            }
            let failureFinal = failure
            let original = seed.original
            await MainActor.run {
                if let failure = failureFinal {
                    model.statusText = "Send failed: \(failure)"
                } else {
                    // Keep a copy in Out marked sent (FLAG_KEEP_COPY spirit).
                    if let out = model.mailbox(named: "Out") {
                        _ = try? out.append(message: built.message, state: .sent)
                        try? out.save()
                        model.refreshMailbox(named: "Out")
                    }
                    if let original {
                        model.markOriginal(original)
                    }
                    model.statusText = "Message sent."
                }
            }
        }
        dismiss()
    }
}

#endif // os(macOS)
