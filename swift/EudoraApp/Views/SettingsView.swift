// Settings — the classic Eudora Settings dialog: a panel list on the left
// (Getting Started, Checking Mail, Sending Mail, Replying, Junk Mail,
// Getting Attention, Eudora Labels, Fonts & Display…) and the selected
// panel's controls on the right.  The main panels edit the dominant
// personality, exactly as the original did; the Personalities panel edits
// any of them.

#if os(macOS)

import AppKit
import EudoraKit
import SwiftUI

private enum SettingsPanel: String, CaseIterable, Identifiable {
    case gettingStarted = "Getting Started"
    case personalities = "Personalities"
    case checkingMail = "Checking Mail"
    case sendingMail = "Sending Mail"
    case replying = "Replying"
    case signatures = "Signatures"
    case attention = "Getting Attention"
    case junkMail = "Junk Mail"
    case display = "Fonts & Display"
    case labels = "Labels"
    case mailFolder = "Mail Folder"

    var id: String { rawValue }

    var icon: String {
        switch self {
        case .gettingStarted: return "sparkles"
        case .personalities: return "person.2"
        case .checkingMail: return "tray.and.arrow.down"
        case .sendingMail: return "paperplane"
        case .replying: return "arrowshape.turn.up.left"
        case .signatures: return "signature"
        case .attention: return "bell"
        case .junkMail: return "xmark.bin"
        case .display: return "textformat.size"
        case .labels: return "tag"
        case .mailFolder: return "folder"
        }
    }
}

struct SettingsView: View {
    @EnvironmentObject var model: AppModel
    @State private var panel: SettingsPanel? = .gettingStarted
    @State private var selectedID: UUID?
    @State private var showFolderPicker = false
    @State private var selectedSignature: String?
    @State private var newSignaturePrompt = false
    @State private var newSignatureName = ""

    var body: some View {
        NavigationSplitView {
            List(SettingsPanel.allCases, selection: $panel) { p in
                Label(p.rawValue, systemImage: p.icon).tag(p)
            }
            .navigationSplitViewColumnWidth(min: 180, ideal: 190)
        } detail: {
            panelBody(panel ?? .gettingStarted)
        }
        .frame(minWidth: 760, minHeight: 520)
    }

    @ViewBuilder
    private func panelBody(_ panel: SettingsPanel) -> some View {
        switch panel {
        case .gettingStarted: gettingStartedPanel
        case .personalities: personalitiesPanel
        case .checkingMail: checkingMailPanel
        case .sendingMail: sendingMailPanel
        case .replying: replyingPanel
        case .signatures: signaturesPanel
        case .attention: attentionPanel
        case .junkMail: junkMailPanel
        case .display: displayPanel
        case .labels: labelsPanel
        case .mailFolder: mailFolderPanel
        }
    }

    // MARK: bindings

    /// A binding into a global setting; every write saves the JSON.
    private func setting<T>(_ kp: WritableKeyPath<EudoraSettings, T>) -> Binding<T> {
        Binding(get: { model.settings[keyPath: kp] },
                set: { model.settings[keyPath: kp] = $0; model.saveSettings() })
    }

    /// The dominant personality, editable (the classic panels' target).
    private var dominantBinding: Binding<Personality>? {
        let idx = model.settings.dominantIndex
        guard model.settings.personalities.indices.contains(idx) else { return nil }
        return Binding(
            get: { model.settings.personalities[idx] },
            set: { model.settings.personalities[idx] = $0; model.saveSettings() })
    }

    // MARK: Getting Started

    private var gettingStartedPanel: some View {
        Form {
            if let dom = dominantBinding {
                Section("Who You Are") {
                    TextField("Real name", text: dom.realName)
                    TextField("Email address", text: dom.emailAddress)
                }
                Section("Your Mail Servers") {
                    TextField("Incoming server", text: dom.popHost)
                    TextField("Outgoing (SMTP) server", text: dom.smtpHost)
                    TextField("Username", text: dom.username)
                    Text("Ports, security, and passwords are on the Checking Mail, Sending Mail, and Personalities panels.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            } else {
                Text("Create a personality first.")
            }
        }
        .formStyle(.grouped)
    }

    // MARK: Personalities

    private var personalitiesPanel: some View {
        HSplitView {
            VStack(spacing: 0) {
                List(selection: $selectedID) {
                    ForEach(model.settings.personalities) { p in
                        Text(p.name).tag(p.id)
                    }
                }
                Divider()
                HStack {
                    Button {
                        var p = Personality()
                        p.name = "Personality \(model.settings.personalities.count)"
                        model.settings.personalities.append(p)
                        selectedID = p.id
                        model.saveSettings()
                    } label: { Image(systemName: "plus") }
                    Button {
                        if let id = selectedID,
                           model.settings.personalities.count > 1,
                           let i = model.settings.personalities.firstIndex(where: { $0.id == id }) {
                            model.settings.personalities.remove(at: i)
                            if model.settings.dominantIndex >= model.settings.personalities.count {
                                model.settings.dominantIndex = 0
                            }
                            selectedID = nil
                            model.saveSettings()
                        }
                    } label: { Image(systemName: "minus") }
                    .disabled(selectedID == nil || model.settings.personalities.count <= 1)
                    Spacer()
                }
                .buttonStyle(.borderless)
                .padding(6)
            }
            .frame(width: 160)

            personalityForm
                .frame(minWidth: 380)
        }
        .onAppear {
            selectedID = model.settings.dominant.id
        }
    }

    @ViewBuilder
    private var personalityForm: some View {
        if let id = selectedID,
           let index = model.settings.personalities.firstIndex(where: { $0.id == id }) {
            let binding = Binding<Personality>(
                get: { model.settings.personalities[index] },
                set: { model.settings.personalities[index] = $0; model.saveSettings() })
            Form {
                Section("Identity") {
                    TextField("Personality name", text: binding.name)
                    TextField("Real name", text: binding.realName)
                    TextField("Email address", text: binding.emailAddress)
                    Toggle("Dominant personality",
                           isOn: Binding(
                               get: { model.settings.dominantIndex == index },
                               set: { on in
                                   if on { model.settings.dominantIndex = index }
                                   model.saveSettings()
                               }))
                    Toggle("Include in mail checks", isOn: binding.includeInChecks)
                }
                Section("Incoming Mail") {
                    Picker("Account type", selection: accountTypeBinding(binding)) {
                        ForEach(AccountType.allCases) { t in
                            Text(t.displayName).tag(t)
                        }
                    }
                    TextField("Server", text: binding.popHost)
                    TextField("Port", value: binding.popPort, format: .number.grouping(.never))
                    Picker("Security", selection: binding.popSecurity) {
                        ForEach(TransportSecurity.allCases) { s in
                            Text(s.displayName).tag(s)
                        }
                    }
                    checkingOptions(binding)
                }
                Section("Sending Mail (SMTP)") {
                    TextField("Server", text: binding.smtpHost)
                    TextField("Port", value: binding.smtpPort, format: .number.grouping(.never))
                    Picker("Security", selection: binding.smtpSecurity) {
                        ForEach(TransportSecurity.allCases) { s in
                            Text(s.displayName).tag(s)
                        }
                    }
                }
                Section("Signature") {
                    Toggle("Attach signature to new messages", isOn: binding.useSignature)
                    Picker("Signature", selection: binding.signatureName) {
                        Text("None").tag("")
                        ForEach(model.signatureNames(), id: \.self) { name in
                            Text(name).tag(name)
                        }
                    }
                    .disabled(!binding.wrappedValue.useSignature)
                }
                Section("Credentials") {
                    TextField("Username", text: binding.username)
                    SecureField("Password", text: binding.password)
                    Text("Stored in EudoraSettings.json in the mail folder.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .formStyle(.grouped)
        } else {
            Text("Select a personality.")
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    /// Switching account type swaps the port only while it still sits at the
    /// other type's default (995 ⇄ 993), preserving custom ports.
    private func accountTypeBinding(_ p: Binding<Personality>) -> Binding<AccountType> {
        Binding(get: { p.wrappedValue.accountType },
                set: { newType in
                    var v = p.wrappedValue
                    let oldType = v.accountType
                    if v.popPort == oldType.defaultPort {
                        v.popPort = newType.defaultPort
                    }
                    v.accountType = newType
                    p.wrappedValue = v
                })
    }

    /// The classic Checking Mail options, per personality (shared between
    /// the Checking Mail panel — dominant — and the Personalities form).
    @ViewBuilder
    private func checkingOptions(_ binding: Binding<Personality>) -> some View {
        Toggle("Leave mail on server", isOn: binding.leaveOnServer)
        Stepper(value: binding.leaveOnServerDays, in: 0...30) {
            HStack {
                Text("Delete from server after")
                Text(binding.wrappedValue.leaveOnServerDays == 0
                     ? "— never" : "\(binding.wrappedValue.leaveOnServerDays) day\(binding.wrappedValue.leaveOnServerDays == 1 ? "" : "s")")
                    .foregroundStyle(.secondary)
            }
        }
        .disabled(!binding.wrappedValue.leaveOnServer)
        Toggle("Skip messages over", isOn: binding.skipBigMessages)
        Stepper(value: binding.bigMessageLimitK, in: 1...1000, step: 10) {
            HStack {
                Text("Big-message limit")
                Text("\(binding.wrappedValue.bigMessageLimitK)K")
                    .foregroundStyle(.secondary)
            }
        }
        .disabled(!binding.wrappedValue.skipBigMessages)
        Toggle("Delete from server when emptied from Trash",
               isOn: binding.serverDeleteOnTrashEmpty)
        Text("The Trash option is stored but not yet honored.")
            .font(.caption)
            .foregroundStyle(.secondary)
    }

    // MARK: Checking Mail

    private var checkingMailPanel: some View {
        Form {
            Section("Automatic Checking") {
                Toggle("Check for mail every…", isOn: setting(\.autoCheck))
                Stepper(value: setting(\.checkIntervalMinutes), in: 1...999) {
                    HStack {
                        Text("Interval")
                        Text("\(model.settings.checkIntervalMinutes) minute\(model.settings.checkIntervalMinutes == 1 ? "" : "s")")
                            .foregroundStyle(.secondary)
                    }
                }
                .disabled(!model.settings.autoCheck)
                Toggle("Send on check", isOn: setting(\.sendOnCheck))
            }
            if let dom = dominantBinding {
                Section("Leaving Mail on the Server (\(dom.wrappedValue.name))") {
                    checkingOptions(dom)
                }
            }
        }
        .formStyle(.grouped)
    }

    // MARK: Sending Mail

    private var sendingMailPanel: some View {
        Form {
            if let dom = dominantBinding {
                Section("SMTP Server (\(dom.wrappedValue.name))") {
                    TextField("Server", text: dom.smtpHost)
                    TextField("Port", value: dom.smtpPort, format: .number.grouping(.never))
                    Picker("Security", selection: dom.smtpSecurity) {
                        ForEach(TransportSecurity.allCases) { s in
                            Text(s.displayName).tag(s)
                        }
                    }
                }
            }
            Section {
                Toggle("Send queued messages when checking mail",
                       isOn: setting(\.sendOnCheck))
            }
        }
        .formStyle(.grouped)
    }

    // MARK: Replying

    private var replyingPanel: some View {
        Form {
            Section("Reply to All") {
                Toggle("Include yourself", isOn: setting(\.replyAllIncludesSelf))
                Text("When off, your own addresses are removed from the recipient list of a Reply All.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Section("Quoting") {
                TextField("Quote prefix", text: setting(\.quotePrefix))
                TextField("Attribution", text: setting(\.attributionTemplate))
                Text("Placeholders: {from}, {date}, {time}, {subject}.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
    }

    // MARK: Signatures

    private var signaturesPanel: some View {
        HSplitView {
            VStack(spacing: 0) {
                List(selection: $selectedSignature) {
                    ForEach(model.signatureNames(), id: \.self) { name in
                        Text(name).tag(name)
                    }
                }
                Divider()
                HStack {
                    Button {
                        newSignatureName = ""
                        newSignaturePrompt = true
                    } label: { Image(systemName: "plus") }
                    Button {
                        if let name = selectedSignature {
                            model.deleteSignature(named: name)
                            selectedSignature = nil
                        }
                    } label: { Image(systemName: "minus") }
                    .disabled(selectedSignature == nil)
                    Spacer()
                }
                .buttonStyle(.borderless)
                .padding(6)
            }
            .frame(width: 160)

            Group {
                if let name = selectedSignature {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(name).font(.headline)
                        TextEditor(text: signatureBinding(name))
                            .font(.body.monospaced())
                        Text("Appended to outgoing messages after a \"-- \" separator, for personalities that use it.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .padding()
                } else {
                    Text("Select or create a signature.")
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
            .frame(minWidth: 380)
        }
        .alert("New Signature", isPresented: $newSignaturePrompt) {
            TextField("Name", text: $newSignatureName)
            Button("Create") {
                let name = newSignatureName.trimmingCharacters(in: .whitespaces)
                if !name.isEmpty {
                    model.saveSignature(named: name, text: "")
                    selectedSignature = name
                }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Creating a signature called:")
        }
    }

    /// Reads and writes the signature file directly; signatures are small.
    private func signatureBinding(_ name: String) -> Binding<String> {
        Binding(get: { model.signatureText(named: name) },
                set: { model.saveSignature(named: name, text: $0) })
    }

    // MARK: Getting Attention

    private var attentionPanel: some View {
        Form {
            Section("When New Mail Arrives") {
                Toggle("Put up an alert", isOn: setting(\.newMailAlert))
                Toggle("Play a sound", isOn: setting(\.newMailSound))
                Picker("New-mail sound", selection: setting(\.newMailSoundName)) {
                    ForEach(systemSoundNames, id: \.self) { name in
                        Text(name).tag(name)
                    }
                }
                .disabled(!model.settings.newMailSound)
                Toggle("Show unread count on the Dock icon",
                       isOn: setting(\.dockBadgeUnread))
                Toggle("Open the mailbox that got new mail",
                       isOn: setting(\.openMailboxOnNewMail))
            }
        }
        .formStyle(.grouped)
    }

    private var systemSoundNames: [String] {
        let files = (try? FileManager.default
            .contentsOfDirectory(atPath: "/System/Library/Sounds")) ?? []
        let names = files.filter { $0.hasSuffix(".aiff") }
            .map { String($0.dropLast(5)) }
            .sorted()
        return names.isEmpty ? ["Glass"] : names
    }

    // MARK: Junk Mail

    private var junkMailPanel: some View {
        Form {
            Section("Junk Scoring") {
                Slider(value: Binding(
                    get: { Double(model.settings.junkThreshold) },
                    set: { model.settings.junkThreshold = Int($0); model.saveSettings() }),
                       in: 1...100, step: 1) {
                    Text("Junk if score is at least \(model.settings.junkThreshold)")
                }
                Stepper(value: setting(\.junkXferScore), in: 0...100) {
                    HStack {
                        Text("Marking as Junk assigns a score of")
                        Text("\(model.settings.junkXferScore)")
                            .foregroundStyle(.secondary)
                    }
                }
            }
            Section("Junk Mailbox") {
                Stepper(value: setting(\.junkEmptyAfterDays), in: 0...365) {
                    HStack {
                        Text("Move junk to Trash after")
                        Text(model.settings.junkEmptyAfterDays == 0
                             ? "— never" : "\(model.settings.junkEmptyAfterDays) day\(model.settings.junkEmptyAfterDays == 1 ? "" : "s")")
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .formStyle(.grouped)
    }

    // MARK: Fonts & Display

    private var displayPanel: some View {
        Form {
            Section("Message Text") {
                Stepper(value: setting(\.displayFontSize), in: 7...127) {
                    HStack {
                        Text("Font size")
                        Text("\(model.settings.displayFontSize) pt")
                            .foregroundStyle(.secondary)
                    }
                }
                Text("Sample message text.")
                    .font(.system(size: CGFloat(model.settings.displayFontSize)).monospaced())
            }
        }
        .formStyle(.grouped)
    }

    // MARK: Labels

    private var labelsPanel: some View {
        Form {
            Section("Eudora Labels") {
                ForEach(1..<16, id: \.self) { i in
                    if model.settings.labels.indices.contains(i) {
                        HStack {
                            TextField("Label \(i)", text: labelNameBinding(i))
                            ColorPicker("", selection: labelColorBinding(i),
                                        supportsOpacity: false)
                                .labelsHidden()
                        }
                    }
                }
                Button("Restore Classic Labels") {
                    model.settings.labels = EudoraSettings.classicLabels
                    model.saveSettings()
                }
            }
        }
        .formStyle(.grouped)
    }

    private func labelNameBinding(_ i: Int) -> Binding<String> {
        Binding(get: {
            model.settings.labels.indices.contains(i)
                ? model.settings.labels[i].name : ""
        }, set: {
            guard model.settings.labels.indices.contains(i) else { return }
            model.settings.labels[i].name = $0
            model.saveSettings()
        })
    }

    private func labelColorBinding(_ i: Int) -> Binding<Color> {
        Binding(get: {
            guard model.settings.labels.indices.contains(i) else { return .gray }
            let l = model.settings.labels[i]
            return Color(red: l.r, green: l.g, blue: l.b)
        }, set: { color in
            guard model.settings.labels.indices.contains(i),
                  let ns = NSColor(color).usingColorSpace(.sRGB) else { return }
            model.settings.labels[i].r = Double(ns.redComponent)
            model.settings.labels[i].g = Double(ns.greenComponent)
            model.settings.labels[i].b = Double(ns.blueComponent)
            model.saveSettings()
        })
    }

    // MARK: Mail Folder

    private var mailFolderPanel: some View {
        Form {
            Section("Eudora Folder") {
                Text(model.mailFolder.path)
                    .font(.body.monospaced())
                    .textSelection(.enabled)
                Button("Choose…") { showFolderPicker = true }
                Text("Mailboxes, the filters file, the address book, signatures, and settings all live here — the classic Eudora Folder layout.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .fileImporter(isPresented: $showFolderPicker,
                      allowedContentTypes: [.folder]) { result in
            if case let .success(url) = result {
                model.setMailFolder(url)
            }
        }
    }
}

#endif // os(macOS)
