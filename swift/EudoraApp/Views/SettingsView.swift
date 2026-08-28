// Settings: personalities (the classic Getting Started / Hosts panels)
// and the mail folder location.

#if os(macOS)

import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var model: AppModel
    @State private var selectedID: UUID?
    @State private var showFolderPicker = false

    var body: some View {
        TabView {
            personalitiesTab
                .tabItem { Label("Personalities", systemImage: "person.2") }
            folderTab
                .tabItem { Label("Mail Folder", systemImage: "folder") }
        }
        .frame(width: 560, height: 460)
    }

    // MARK: personalities

    private var personalitiesTab: some View {
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
                .frame(minWidth: 340)
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
                }
                Section("Checking Mail (POP3)") {
                    TextField("Server", text: binding.popHost)
                    TextField("Port", value: binding.popPort, format: .number.grouping(.never))
                    Picker("Security", selection: binding.popSecurity) {
                        ForEach(TransportSecurity.allCases) { s in
                            Text(s.displayName).tag(s)
                        }
                    }
                    Toggle("Leave mail on server", isOn: binding.leaveOnServer)
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

    // MARK: mail folder

    private var folderTab: some View {
        Form {
            Section("Eudora Folder") {
                Text(model.mailFolder.path)
                    .font(.body.monospaced())
                    .textSelection(.enabled)
                Button("Choose…") { showFolderPicker = true }
                Text("Mailboxes, the filters file, the address book, and settings all live here — the classic Eudora Folder layout.")
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
