// The composition window: Eudora's classic header block (From / To /
// Subject / Cc / Bcc / Attachments) over the body editor, with the
// Queue and Send buttons the original put in the icon bar.  The body is an
// NSTextView-backed editor so it gets the system spell/grammar checker and,
// in Styled mode, real rich text sent as an HTML alternative.

#if os(macOS)

import AppKit
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
    @State private var attributedBody: NSAttributedString
    @State private var priority: Int
    @State private var attachments: [URL] = []
    @State private var styled = false
    @State private var showImporter = false
    @State private var errorText: String?
    @State private var nicknames: [String] = []
    @State private var stationerySavePrompt = false
    @State private var stationerySaveName = ""
    @StateObject private var richText = RichTextController()

    @FocusState private var focusedRecipient: RecipientField?

    private enum RecipientField: Hashable { case to, cc, bcc }

    init(seed: ComposeSeed = ComposeSeed()) {
        self.seed = seed
        _to = State(initialValue: seed.to)
        _cc = State(initialValue: seed.cc)
        _bcc = State(initialValue: seed.bcc)
        _subject = State(initialValue: seed.subject)
        _priority = State(initialValue: seed.priority)
        _attributedBody = State(initialValue: NSAttributedString(
            string: seed.body,
            attributes: [.font: NSFont.userFixedPitchFont(ofSize: 12)]))
        _attachments = State(initialValue: seed.attachmentPaths.map {
            URL(fileURLWithPath: $0)
        })
    }

    var body: some View {
        VStack(spacing: 0) {
            iconBar
            Divider()
            headerGrid
            if !suggestions.isEmpty { suggestionBar }
            if styled { Divider(); formatBar }
            Divider()
            RichTextEditor(attributed: $attributedBody, isRichText: styled,
                           controller: richText)
        }
        .frame(minWidth: 560, minHeight: 420)
        .navigationTitle(subject.isEmpty ? "No Subject" : subject)
        .onAppear(perform: loadNicknames)
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
        .alert("Save as Stationery", isPresented: $stationerySavePrompt) {
            TextField("Name", text: $stationerySaveName)
            Button("Save") {
                model.saveStationery(named: stationerySaveName,
                                     text: attributedBody.string)
                stationerySaveName = ""
            }
            Button("Cancel", role: .cancel) { stationerySaveName = "" }
        } message: {
            Text("Save the current message body as reusable stationery.")
        }
    }

    // The classic icon bar: priority popup, attach, styled toggle, stationery,
    // Queue, Send.
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

            Toggle(isOn: $styled) {
                Label("Styled", systemImage: "textformat")
            }
            .toggleStyle(.button)
            .help("Compose styled text, sent as an HTML alternative")

            stationeryMenu

            Spacer()

            Button("Queue") { queue() }
                .help("Save to Out as queued (the classic Queue button)")
            Button("Send") { send() }
                .keyboardShortcut(.return, modifiers: [.command])
                .buttonStyle(.borderedProminent)
        }
        .padding(8)
    }

    private var stationeryMenu: some View {
        Menu {
            let names = model.stationeryNames()
            if names.isEmpty {
                Text("No stationery saved").foregroundStyle(.secondary)
            } else {
                ForEach(names, id: \.self) { name in
                    Button(name) { applyStationery(named: name) }
                }
            }
            Divider()
            Button("Save as Stationery…") { stationerySavePrompt = true }
        } label: {
            Label("Stationery", systemImage: "doc.text")
        }
        .fixedSize()
    }

    // Bold / Italic / Underline / Fonts, shown only in Styled mode.
    private var formatBar: some View {
        HStack(spacing: 6) {
            Button { richText.toggleBold() } label: { Image(systemName: "bold") }
                .keyboardShortcut("b", modifiers: [.command])
            Button { richText.toggleItalic() } label: { Image(systemName: "italic") }
                .keyboardShortcut("i", modifiers: [.command])
            Button { richText.toggleUnderline() } label: {
                Image(systemName: "underline")
            }
            .keyboardShortcut("u", modifiers: [.command])
            Button { richText.showFonts() } label: {
                Label("Fonts", systemImage: "textformat.size")
            }
            Spacer()
        }
        .buttonStyle(.borderless)
        .padding(.horizontal, 10)
        .padding(.vertical, 4)
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
                    .focused($focusedRecipient, equals: .to)
            }
            GridRow {
                headerLabel("Subject:")
                TextField("", text: $subject).textFieldStyle(.plain)
            }
            GridRow {
                headerLabel("Cc:")
                TextField("", text: $cc).textFieldStyle(.plain)
                    .focused($focusedRecipient, equals: .cc)
            }
            GridRow {
                headerLabel("Bcc:")
                TextField("", text: $bcc).textFieldStyle(.plain)
                    .focused($focusedRecipient, equals: .bcc)
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

    // Address-book completions for the focused recipient field.
    private var suggestionBar: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(suggestions, id: \.self) { name in
                    Button {
                        completeRecipient(with: name)
                    } label: {
                        Label(name, systemImage: "person.crop.circle")
                            .font(.callout)
                    }
                    .buttonStyle(.bordered)
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 4)
        }
        .background(.quaternary.opacity(0.2))
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

    // MARK: recipient autocomplete

    private func loadNicknames() {
        guard let book = try? AddressBook(path: model.nicknamesURL.path)
        else { return }
        nicknames = book.entries.map(\.name)
    }

    private func recipientBinding(_ field: RecipientField) -> Binding<String> {
        switch field {
        case .to: return $to
        case .cc: return $cc
        case .bcc: return $bcc
        }
    }

    /// The token being typed (after the last comma) in the focused field.
    private var currentToken: String {
        guard let field = focusedRecipient else { return "" }
        let text = recipientBinding(field).wrappedValue
        return String(text.split(separator: ",", omittingEmptySubsequences: false)
            .last ?? "").trimmingCharacters(in: .whitespaces)
    }

    private var suggestions: [String] {
        let token = currentToken
        guard focusedRecipient != nil, token.count >= 1 else { return [] }
        let lower = token.lowercased()
        return nicknames
            .filter { $0.lowercased().contains(lower) && $0.lowercased() != lower }
            .prefix(6)
            .map { $0 }
    }

    private func completeRecipient(with name: String) {
        guard let field = focusedRecipient else { return }
        let binding = recipientBinding(field)
        var parts = binding.wrappedValue
            .split(separator: ",", omittingEmptySubsequences: false)
            .map { String($0) }
        let quoted = name.contains(" ") ? "\"\(name)\"" : name
        if parts.isEmpty {
            binding.wrappedValue = quoted + ", "
        } else {
            let lead = parts.dropLast().map {
                $0.trimmingCharacters(in: .whitespaces)
            }
            binding.wrappedValue = (lead + [quoted]).joined(separator: ", ") + ", "
        }
    }

    // MARK: stationery

    private func applyStationery(named name: String) {
        guard let text = model.stationeryText(named: name) else { return }
        attributedBody = NSAttributedString(
            string: text,
            attributes: [.font: NSFont.userFixedPitchFont(ofSize: 12)])
    }

    // MARK: build / send

    /// Plain body (always) and an HTML alternative (only when styled with real
    /// formatting), each with the signature appended for non-redirects.
    private func composedBodies() -> (plain: String, html: String?) {
        let full = NSMutableAttributedString(attributedString: attributedBody)
        if account.useSignature, seed.fromAddress == nil {
            let sig = model.signatureText(named: account.signatureName)
            if !sig.isEmpty {
                var text = full.string.hasSuffix("\n") ? "" : "\n"
                text += "\n-- \n" + sig
                full.append(NSAttributedString(
                    string: text,
                    attributes: [.font: NSFont.userFixedPitchFont(ofSize: 12)]))
            }
        }
        let html = (styled && attributedBody.hasStyling) ? full.htmlBody : nil
        return (full.string, html)
    }

    private func buildMessage() -> (message: String, recipients: String)? {
        guard !to.isEmpty || !cc.isEmpty || !bcc.isEmpty else {
            errorText = "The message has no recipients."
            return nil
        }
        let bodies = composedBodies()
        let composer = Composer()
            .from(name: seed.fromAddress != nil ? (seed.fromName ?? "")
                                                : account.realName,
                  address: seed.fromAddress ?? account.emailAddress)
            .subject(subject)
            .body(bodies.plain)
            .priority(priority)
            .header("X-Mailer", "Eudora (EudoraCore \(String(cString: eudora_core_version())))")
        if let html = bodies.html { composer.htmlBody(html) }
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
