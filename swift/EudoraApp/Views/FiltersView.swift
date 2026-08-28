// The Filters window: the classic two-pane editor — filter list on the
// left (with Up/Down/New/Remove), Match and Action panes on the right
// (filtwin.c's layout, minus the Toolbox).

#if os(macOS)

import EudoraKit
import SwiftUI

struct FiltersView: View {
    @EnvironmentObject var model: AppModel

    @State private var filters: FilterSet?
    @State private var records: [FilterRecord] = []
    @State private var selection: Int?
    @State private var actions: [FilterActionRecord] = []
    @State private var dirty = false

    private let headerChoices = [
        "to:", "from:", "subject:", "cc:", "reply-to:", "«any header»", "«body»",
    ]
    private let conjunctions = ["ignore", "and", "or", "unless"]

    var body: some View {
        HSplitView {
            filterList
                .frame(minWidth: 180, maxWidth: 260)
            editor
                .frame(minWidth: 380)
        }
        .frame(minWidth: 620, minHeight: 420)
        .onAppear(perform: load)
        .onChange(of: selection) { _ in reloadActions() }
        .toolbar {
            ToolbarItem {
                Button("Save") { save() }
                    .disabled(!dirty)
                    .keyboardShortcut("s", modifiers: [.command])
            }
        }
        .navigationTitle("Filters")
    }

    // MARK: list pane

    private var filterList: some View {
        VStack(spacing: 0) {
            List(selection: $selection) {
                ForEach(records.indices, id: \.self) { i in
                    Text(records[i].name).tag(i)
                }
            }
            Divider()
            HStack(spacing: 12) {
                Button {
                    guard let fs = filters else { return }
                    let i = fs.add(name: "Untitled")
                    reloadRecords()
                    selection = i
                    dirty = true
                } label: { Image(systemName: "plus") }

                Button {
                    guard let fs = filters, let i = selection else { return }
                    fs.remove(at: i)
                    reloadRecords()
                    selection = nil
                    dirty = true
                } label: { Image(systemName: "minus") }
                .disabled(selection == nil)

                Spacer()

                Button {
                    move(-1)
                } label: { Image(systemName: "arrow.up") }
                .disabled(selection == nil || selection == 0)

                Button {
                    move(1)
                } label: { Image(systemName: "arrow.down") }
                .disabled(selection == nil || selection == records.count - 1)
            }
            .buttonStyle(.borderless)
            .padding(6)
        }
    }

    private func move(_ delta: Int) {
        guard let fs = filters, let i = selection else { return }
        let j = i + delta
        guard j >= 0 && j < records.count else { return }
        fs.move(from: i, to: j)
        reloadRecords()
        selection = j
        dirty = true
    }

    // MARK: editor pane

    @ViewBuilder
    private var editor: some View {
        if let i = selection, i < records.count {
            let binding = Binding<FilterRecord>(
                get: { records[i] },
                set: { newValue in
                    records[i] = newValue
                    filters?.update(newValue, at: i)
                    dirty = true
                })
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    matchSection(binding)
                    Divider()
                    actionSection(filterIndex: i)
                }
                .padding(12)
            }
        } else {
            VStack {
                Spacer()
                Text("Select a filter, or click + to make a new one.")
                    .foregroundStyle(.secondary)
                Spacer()
            }
            .frame(maxWidth: .infinity)
        }
    }

    private func matchSection(_ record: Binding<FilterRecord>) -> some View {
        GroupBox("Match") {
            VStack(alignment: .leading, spacing: 8) {
                TextField("Filter name", text: record.name)

                HStack(spacing: 16) {
                    Toggle("Incoming", isOn: record.incoming)
                    Toggle("Outgoing", isOn: record.outgoing)
                    Toggle("Manual", isOn: record.manual)
                }

                termRow("Header:", record.term1)

                Picker("", selection: record.conjunction) {
                    ForEach(conjunctions, id: \.self) { Text($0) }
                }
                .frame(width: 130)

                if record.wrappedValue.conjunction != "ignore" {
                    termRow("Header:", record.term2)
                }
            }
            .padding(6)
        }
    }

    private func termRow(_ label: String, _ term: Binding<FilterTerm>) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(label).frame(width: 60, alignment: .trailing)
                ComboField(choices: headerChoices, text: term.header)
                    .frame(width: 170)
                Picker("", selection: term.verb) {
                    ForEach(FilterTerm.verbs, id: \.raw) { verb in
                        Text(verb.display).tag(verb.raw)
                    }
                }
                .labelsHidden()
                .frame(width: 200)
            }
            HStack {
                Spacer().frame(width: 68)
                TextField("value", text: term.value)
            }
        }
    }

    private func actionSection(filterIndex: Int) -> some View {
        GroupBox("Action") {
            VStack(alignment: .leading, spacing: 6) {
                ForEach(actions.indices, id: \.self) { slot in
                    HStack {
                        Picker("", selection: Binding(
                            get: { actions[slot].keyword },
                            set: { newValue in
                                actions[slot].keyword = newValue
                                filters?.setAction(actions[slot], at: filterIndex,
                                                   slot: slot)
                                dirty = true
                            })) {
                            ForEach(FilterActionRecord.keywords, id: \.raw) { kw in
                                Text(kw.display).tag(kw.raw)
                            }
                        }
                        .labelsHidden()
                        .frame(width: 170)

                        TextField("parameter", text: Binding(
                            get: { actions[slot].value },
                            set: { newValue in
                                actions[slot].value = newValue
                                filters?.setAction(actions[slot], at: filterIndex,
                                                   slot: slot)
                                dirty = true
                            }))

                        Button {
                            filters?.removeAction(at: filterIndex, slot: slot)
                            reloadActions()
                            dirty = true
                        } label: { Image(systemName: "minus.circle") }
                        .buttonStyle(.borderless)
                    }
                }
                // The classic editor allowed five actions per filter.
                if actions.count < 5 {
                    Button {
                        let action = FilterActionRecord(keyword: "transfer", value: "")
                        filters?.addAction(action, at: filterIndex)
                        reloadActions()
                        dirty = true
                    } label: {
                        Label("Add Action", systemImage: "plus.circle")
                    }
                    .buttonStyle(.borderless)
                }
            }
            .padding(6)
        }
    }

    // MARK: persistence

    private func load() {
        let fs = (try? FilterSet(path: model.filtersURL.path)) ?? FilterSet()
        filters = fs
        reloadRecords()
        dirty = false
    }

    private func reloadRecords() {
        records = filters?.records ?? []
        reloadActions()
    }

    private func reloadActions() {
        if let i = selection, let fs = filters, i < fs.count {
            actions = fs.actions(at: i)
        } else {
            actions = []
        }
    }

    private func save() {
        guard let fs = filters else { return }
        do {
            try fs.save(to: model.filtersURL.path)
            dirty = false
            model.statusText = "Filters saved."
        } catch {
            model.statusText = "Cannot save filters: \(error)"
        }
    }
}

/// An editable text field with a menu of common choices (the classic
/// header popup + type-in combo).
struct ComboField: View {
    let choices: [String]
    @Binding var text: String

    var body: some View {
        HStack(spacing: 2) {
            TextField("header", text: $text)
            Menu {
                ForEach(choices, id: \.self) { choice in
                    Button(choice) { text = choice }
                }
            } label: {
                Image(systemName: "chevron.down")
            }
            .menuStyle(.borderlessButton)
            .frame(width: 18)
        }
    }
}

#endif // os(macOS)
