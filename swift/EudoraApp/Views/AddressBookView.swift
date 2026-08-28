// The Address Book window: nickname list on the left, addresses and
// notes on the right (the classic Nicknames window layout).

#if os(macOS)

import EudoraKit
import SwiftUI

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
            ToolbarItem {
                Button("Save") { save() }
                    .disabled(!dirty)
                    .keyboardShortcut("s", modifiers: [.command])
            }
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
                    Text(entry.name).tag(entry.name)
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
