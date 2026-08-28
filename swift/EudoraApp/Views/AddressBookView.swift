// The Address Book window: nickname list on the left, addresses and
// notes on the right (the classic Nicknames window layout).

#if os(macOS)

import EudoraKit
import SwiftUI
import UniformTypeIdentifiers

struct AddressBookView: View {
    @EnvironmentObject var model: AppModel

    @State private var book: AddressBook?
    @State private var entries: [AddressBook.Entry] = []
    @State private var selection: String?
    @State private var editedAddresses = ""
    @State private var editedNotes = ""
    @State private var newNamePrompt = false
    @State private var newName = ""
    @State private var dirty = false
    @State private var showImporter = false

    var body: some View {
        HSplitView {
            nicknameList
                .frame(minWidth: 170, maxWidth: 240)
            editorPane
                .frame(minWidth: 320)
        }
        .frame(minWidth: 540, minHeight: 360)
        .onAppear(perform: load)
        .onChange(of: selection) { _ in loadSelection() }
        .toolbar {
            ToolbarItemGroup {
                Button("Import…") { showImporter = true }
                    .help("Import contacts from a text, CSV, or tab-delimited file")
                Button("Save") { save() }
                    .disabled(!dirty)
                    .keyboardShortcut("s", modifiers: [.command])
            }
        }
        .fileImporter(isPresented: $showImporter,
                      allowedContentTypes: [.plainText, .commaSeparatedText,
                                            .text, .data],
                      allowsMultipleSelection: false) { result in
            importContacts(result)
        }
        .navigationTitle("Address Book")
        .alert("New Nickname", isPresented: $newNamePrompt) {
            TextField("Nickname", text: $newName)
            Button("Create") {
                guard !newName.isEmpty else { return }
                book?.set(name: newName, addresses: "", notes: "")
                reload()
                selection = newName
                newName = ""
                dirty = true
            }
            Button("Cancel", role: .cancel) { newName = "" }
        }
    }

    private var nicknameList: some View {
        VStack(spacing: 0) {
            List(selection: $selection) {
                ForEach(entries, id: \.name) { entry in
                    Label {
                        Text(entry.name)
                    } icon: {
                        Image(systemName: entry.addresses.contains(",")
                            ? "person.2" : "person")
                            .foregroundStyle(.secondary)
                    }
                    .tag(entry.name)
                }
            }
            Divider()
            HStack {
                Button {
                    newNamePrompt = true
                } label: { Image(systemName: "plus") }
                Button {
                    if let name = selection {
                        commitEdits()
                        book?.remove(name: name)
                        reload()
                        selection = nil
                        dirty = true
                    }
                } label: { Image(systemName: "minus") }
                .disabled(selection == nil)
                Spacer()
            }
            .buttonStyle(.borderless)
            .padding(6)
        }
    }

    @ViewBuilder
    private var editorPane: some View {
        if let name = selection {
            VStack(alignment: .leading, spacing: 10) {
                Text(name).font(.title3.bold())

                Text("Addresses").font(.headline)
                TextEditor(text: Binding(
                    get: { editedAddresses },
                    set: { editedAddresses = $0; dirty = true }))
                    .font(.body.monospaced())
                    .frame(minHeight: 80)
                    .border(.quaternary)

                Text("Notes").font(.headline)
                TextEditor(text: Binding(
                    get: { editedNotes },
                    set: { editedNotes = $0; dirty = true }))
                    .frame(minHeight: 80)
                    .border(.quaternary)

                if !editedAddresses.isEmpty, let book {
                    let expanded = book.expand(quotedName(name))
                    if expanded.count > 1 {
                        Text("Expands to: \(expanded.joined(separator: ", "))")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .padding(12)
        } else {
            VStack {
                Spacer()
                Text("Select a nickname.")
                    .foregroundStyle(.secondary)
                Spacer()
            }
            .frame(maxWidth: .infinity)
        }
    }

    private func quotedName(_ name: String) -> String {
        name.contains(" ") ? "\"\(name)\"" : name
    }

    private func importContacts(_ result: Result<[URL], Error>) {
        guard case let .success(urls) = result, let url = urls.first,
              let book else { return }
        let didAccess = url.startAccessingSecurityScopedResource()
        defer { if didAccess { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url) else {
            model.statusText = "Cannot read \(url.lastPathComponent)."
            return
        }
        // Contact exports are usually UTF-8; fall back to Latin-1 so no byte
        // is rejected.
        let text = String(data: data, encoding: .utf8)
            ?? String(data: data, encoding: .isoLatin1) ?? ""
        let added = book.importContacts(text, overwrite: false)
        commitEdits()
        reload()
        dirty = true
        model.statusText = added == 0
            ? "No new contacts imported."
            : "Imported \(added) contact\(added == 1 ? "" : "s")."
    }

    private func load() {
        book = (try? AddressBook(path: model.nicknamesURL.path))
            ?? (try? AddressBook(text: ""))
        reload()
        dirty = false
    }

    private func reload() {
        entries = book?.entries ?? []
    }

    private func loadSelection() {
        commitPreviousSelection()
        guard let name = selection,
              let entry = entries.first(where: { $0.name == name }) else {
            editedAddresses = ""
            editedNotes = ""
            previousSelection = nil
            return
        }
        editedAddresses = entry.addresses
        editedNotes = entry.notes
        previousSelection = name
    }

    @State private var previousSelection: String?

    private func commitPreviousSelection() {
        if let previous = previousSelection,
           entries.contains(where: { $0.name == previous }) {
            book?.set(name: previous, addresses: editedAddresses, notes: editedNotes)
        }
    }

    private func commitEdits() {
        if let name = selection {
            book?.set(name: name, addresses: editedAddresses, notes: editedNotes)
            reload()
        }
    }

    private func save() {
        commitEdits()
        do {
            try book?.save(to: model.nicknamesURL.path)
            dirty = false
            model.statusText = "Address book saved."
        } catch {
            model.statusText = "Cannot save address book: \(error)"
        }
    }
}

#endif // os(macOS)
