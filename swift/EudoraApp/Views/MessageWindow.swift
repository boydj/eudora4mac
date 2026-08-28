// A message in its own window (the classic double-click-to-open), reusing
// the preview pane's header strip and body.

#if os(macOS)

import EudoraKit
import SwiftUI

struct MessageWindow: View {
    @EnvironmentObject var model: AppModel

    let reference: MessageRef

    private var resolvedIndex: Int? {
        guard let mb = model.mailbox(named: reference.mailbox) else { return nil }
        if let idx = mb.findBySerial(reference.serial) { return idx }
        return reference.index < mb.count ? reference.index : nil
    }

    var body: some View {
        PreviewPane(mailboxName: reference.mailbox, messageIndex: resolvedIndex)
            .frame(minWidth: 520, minHeight: 380)
            .navigationTitle(windowTitle)
            .onAppear(perform: markRead)
    }

    private var windowTitle: String {
        guard let idx = resolvedIndex,
              let sum = model.mailbox(named: reference.mailbox)?.summary(at: idx)
        else { return "Message" }
        return sum.subject.isEmpty ? "No Subject" : sum.subject
    }

    private func markRead() {
        guard let idx = resolvedIndex,
              let mb = model.mailbox(named: reference.mailbox),
              mb.summary(at: idx)?.state == .unread
        else { return }
        mb.setState(.read, at: idx)
        try? mb.save()
        model.mailboxGeneration += 1
    }
}

#endif // os(macOS)
