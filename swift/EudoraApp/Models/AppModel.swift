// The application model: the mail folder, its mailboxes, account settings,
// and the check-mail / send-queue operations.
//
// Layout follows the classic Eudora Folder: mailbox files (In, Out, Trash,
// user mailboxes) with ".toc" sidecars, "Eudora Filters", and
// "Eudora Nicknames", all inside one folder.

#if os(macOS)

import EudoraKit
import Foundation
import SwiftUI

/// Thread-safe cancellation flag shared between the UI (Stop button) and the
/// background fetch, which polls it from its progress callback.
final class CancelFlag: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    func cancel() { lock.lock(); value = true; lock.unlock() }
    func reset() { lock.lock(); value = false; lock.unlock() }
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return value }
}

@MainActor
final class AppModel: ObservableObject {
    // MARK: mail folder

    @AppStorage("mailFolderPath") private var storedMailFolderPath: String = ""

    var mailFolder: URL {
        if !storedMailFolderPath.isEmpty {
            return URL(fileURLWithPath: storedMailFolderPath, isDirectory: true)
        }
        let docs = FileManager.default.urls(for: .documentDirectory,
                                            in: .userDomainMask).first!
        return docs.appendingPathComponent("Eudora Folder", isDirectory: true)
    }

    func setMailFolder(_ url: URL) {
        storedMailFolderPath = url.path
        bootstrapMailFolder()
        reloadMailboxes()
        settings = EudoraSettings.load(from: settingsURL)
        refreshAutoCheck()
        updateDockBadge()
    }

    var settingsURL: URL { mailFolder.appendingPathComponent("EudoraSettings.json") }
    var filtersURL: URL { mailFolder.appendingPathComponent("Eudora Filters") }
    var nicknamesURL: URL { mailFolder.appendingPathComponent("Eudora Nicknames") }
    /// Signature files live in the classic "Signature Folder".
    var signatureFolderURL: URL {
        mailFolder.appendingPathComponent("Signature Folder", isDirectory: true)
    }

    // MARK: signatures (plain text files, one per signature)

    func signatureNames() -> [String] {
        let fm = FileManager.default
        try? fm.createDirectory(at: signatureFolderURL,
                                withIntermediateDirectories: true)
        let names = (try? fm.contentsOfDirectory(atPath: signatureFolderURL.path)) ?? []
        return names.filter { !$0.hasPrefix(".") }
            .sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }
    }

    func signatureText(named name: String) -> String {
        guard !name.isEmpty else { return "" }
        let url = signatureFolderURL.appendingPathComponent(name)
        return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    }

    func saveSignature(named name: String, text: String) {
        guard !name.isEmpty else { return }
        let fm = FileManager.default
        try? fm.createDirectory(at: signatureFolderURL,
                                withIntermediateDirectories: true)
        try? text.write(to: signatureFolderURL.appendingPathComponent(name),
                        atomically: true, encoding: .utf8)
    }

    func deleteSignature(named name: String) {
        guard !name.isEmpty else { return }
        try? FileManager.default.removeItem(
            at: signatureFolderURL.appendingPathComponent(name))
    }

    // MARK: published state

    @Published var mailboxNames: [String] = []
    @Published var selectedMailbox: String? = "In"
    /// Selected row in the current mailbox's table (menu commands act on it).
    @Published var selectedMessage: Int?
    @Published var statusText: String = "Welcome to Eudora."
    @Published var isCheckingMail = false
    @Published var settings = EudoraSettings()

    /// Bumps whenever mailbox contents change so views re-query summaries.
    @Published var mailboxGeneration = 0

    private var openMailboxes: [String: Mailbox] = [:]

    static let specialMailboxes = ["In", "Out", "Trash", "Junk"]

    init() {
        bootstrapMailFolder()
        settings = EudoraSettings.load(from: settingsURL)
        reloadMailboxes()
        refreshAutoCheck()
        updateDockBadge()
    }

    // MARK: mailbox management

    func bootstrapMailFolder() {
        let fm = FileManager.default
        try? fm.createDirectory(at: mailFolder, withIntermediateDirectories: true)
        for name in Self.specialMailboxes {
            let url = mailFolder.appendingPathComponent(name)
            if !fm.fileExists(atPath: url.path) {
                fm.createFile(atPath: url.path, contents: Data())
            }
        }
    }

    func reloadMailboxes() {
        let fm = FileManager.default
        let contents = (try? fm.contentsOfDirectory(atPath: mailFolder.path)) ?? []
        var names = contents.filter { name in
            guard !name.hasSuffix(".toc") && !name.hasSuffix(".json") &&
                !name.hasPrefix(".") && name != "Eudora Filters" &&
                name != "Eudora Nicknames" && !name.hasSuffix(".temp") &&
                !name.hasSuffix(".tmp")
            else { return false }
            // Directories (like the Signature Folder) are not mailboxes.
            var isDir: ObjCBool = false
            fm.fileExists(atPath: mailFolder.appendingPathComponent(name).path,
                          isDirectory: &isDir)
            return !isDir.boolValue
        }
        // Classic ordering: In, Out, Trash, Junk first, then alphabetical.
        names.sort {
            let ia = Self.specialMailboxes.firstIndex(of: $0) ?? Int.max
            let ib = Self.specialMailboxes.firstIndex(of: $1) ?? Int.max
            return ia == ib ? $0.localizedCaseInsensitiveCompare($1) == .orderedAscending
                            : ia < ib
        }
        mailboxNames = names
        openMailboxes.removeAll()
        mailboxGeneration += 1
    }

    func mailbox(named name: String) -> Mailbox? {
        if let open = openMailboxes[name] { return open }
        let url = mailFolder.appendingPathComponent(name)
        guard let mb = try? Mailbox(path: url.path) else { return nil }
        openMailboxes[name] = mb
        return mb
    }

    /// Drops the cached handle so the next access re-reads mbox + TOC.
    func refreshMailbox(named name: String) {
        openMailboxes[name] = nil
        mailboxGeneration += 1
        if name == "In" {
            updateDockBadge()
        }
    }

    func newMailbox(named name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else { return }
        let url = mailFolder.appendingPathComponent(trimmed)
        if !FileManager.default.fileExists(atPath: url.path) {
            FileManager.default.createFile(atPath: url.path, contents: Data())
        }
        reloadMailboxes()
        selectedMailbox = trimmed
    }

    // MARK: message operations

    /// Transfer a message to another mailbox (the classic Transfer menu):
    /// append there, then remove the summary here.
    func transfer(messageAt index: Int, from source: String, to target: String) {
        guard source != target,
              let src = mailbox(named: source),
              let dst = mailbox(named: target),
              let raw = try? src.rawMessage(at: index),
              let summary = src.summary(at: index)
        else { return }
        if (try? dst.append(message: raw, state: summary.state)) != nil {
            try? dst.save()
            src.delete(at: index)
            try? src.save()
            mailboxGeneration += 1
            statusText = "Transferred to \(target)."
        }
    }

    func delete(messageAt index: Int, from source: String) {
        if source == "Trash" {
            guard let mb = mailbox(named: source) else { return }
            mb.delete(at: index)
            try? mb.save()
            mailboxGeneration += 1
            statusText = "Message deleted."
        } else {
            transfer(messageAt: index, from: source, to: "Trash")
        }
    }

    func emptyTrash() {
        guard let trash = mailbox(named: "Trash") else { return }
        while trash.count > 0 {
            trash.delete(at: trash.count - 1)
        }
        try? trash.compact()
        refreshMailbox(named: "Trash")
        statusText = "Trash emptied."
    }

    func compact(mailboxNamed name: String) {
        guard let mb = mailbox(named: name) else { return }
        try? mb.compact()
        refreshMailbox(named: name)
        statusText = "Compacted \(name)."
    }

    // MARK: message actions (Reply / Forward / Redirect / Send Again)

    enum ComposeActionKind {
        case reply, replyAll, forward, redirect, sendAgain
    }

    /// Builds the composition seed for a message action, or nil when the
    /// message can't be read.
    func composeSeed(_ kind: ComposeActionKind, mailbox boxName: String,
                     index: Int) -> ComposeSeed? {
        guard let mb = mailbox(named: boxName),
              let sum = mb.summary(at: index),
              let raw = try? mb.rawMessage(at: index),
              let msg = try? ParsedMessage(raw: raw)
        else { return nil }

        var seed = ComposeSeed()
        seed.priority = sum.priorityDisplay // PREF_NO_XF_PRIOR default
        let subject = msg.decodedHeader("Subject")

        switch kind {
        case .reply, .replyAll:
            seed.subject = subject.lowercased().hasPrefix("re:")
                ? subject : "Re: " + subject
            seed.body = quoted(msg, summary: sum)
            if let msgID = msg.header("Message-Id") {
                seed.extraHeaders.append(.init(name: "In-Reply-To", value: msgID))
            }
            if kind == .reply {
                seed.to = msg.header("Reply-To") ?? msg.header("From") ?? sum.from
            } else {
                let (to, cc) = replyAllRecipients(of: msg)
                seed.to = to
                seed.cc = cc
            }
            seed.original = .init(mailbox: boxName, serial: sum.serialNumber,
                                  markState: MessageState.replied.rawValue)

        case .forward:
            seed.subject = "Fwd: " + subject
            seed.body = quoted(msg, summary: sum)
            seed.original = .init(mailbox: boxName, serial: sum.serialNumber,
                                  markState: MessageState.forwarded.rawValue)

        case .redirect:
            // The classic "(by way of)" From; body passes through verbatim.
            seed.subject = subject
            seed.body = normalizedBody(of: msg)
            let account = settings.dominant
            let me = account.realName.isEmpty ? account.emailAddress
                                              : account.realName
            let origName = displayName(ofFrom: msg) ?? sum.from
            seed.fromName = "\(origName) (by way of \(me))"
            seed.fromAddress =
                bareAddresses(of: msg, header: "From").first ?? account.emailAddress
            seed.original = .init(mailbox: boxName, serial: sum.serialNumber,
                                  markState: MessageState.redistributed.rawValue)

        case .sendAgain:
            seed.to = msg.header("To") ?? ""
            seed.cc = msg.header("Cc") ?? ""
            seed.subject = subject
            seed.body = normalizedBody(of: msg)
        }
        return seed
    }

    /// Marks the message a composition answered (replied/forwarded/…),
    /// finding it by serial so transfers don't misfile the mark.
    func markOriginal(_ ref: ComposeSeed.OriginalRef) {
        guard let mb = mailbox(named: ref.mailbox),
              let idx = mb.findBySerial(ref.serial) else { return }
        mb.setState(MessageState(rawValue: ref.markState) ?? .replied, at: idx)
        try? mb.save()
        mailboxGeneration += 1
    }

    /// Attribution line + quote-prefixed body (QuoteLines + ATTRIBUTION).
    private func quoted(_ msg: ParsedMessage, summary: MessageSummary) -> String {
        let dateFmt = DateFormatter()
        dateFmt.dateStyle = .medium
        dateFmt.timeStyle = .none
        let timeFmt = DateFormatter()
        timeFmt.dateStyle = .none
        timeFmt.timeStyle = .short
        let from = msg.decodedHeader("From")
        var attribution = settings.attributionTemplate
        for (key, value) in [("{from}", from.isEmpty ? summary.from : from),
                             ("{date}", dateFmt.string(from: summary.date)),
                             ("{time}", timeFmt.string(from: summary.date)),
                             ("{subject}", msg.decodedHeader("Subject"))] {
            attribution = attribution.replacingOccurrences(of: key, with: value)
        }
        let prefix = settings.quotePrefix.isEmpty ? ">" : settings.quotePrefix
        var body = normalizedBody(of: msg)
        if body.hasSuffix("\n") { body.removeLast() }
        let quotedLines = body
            .split(separator: "\n", omittingEmptySubsequences: false)
            .map { prefix + String($0) }
            .joined(separator: "\n")
        return attribution + "\n" + quotedLines + "\n"
    }

    private func normalizedBody(of msg: ParsedMessage) -> String {
        msg.decodedBody
            .replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
    }

    private func bareAddresses(of msg: ParsedMessage, header: String) -> [String] {
        parseAddresses(msg.header(header) ?? "")
            .filter { !$0.isEmpty && $0 != ";" && !$0.hasSuffix(":") }
    }

    private func displayName(ofFrom msg: ParsedMessage) -> String? {
        var name = msg.decodedHeader("From")
        if let lt = name.firstIndex(of: "<") { name = String(name[..<lt]) }
        name = name.trimmingCharacters(in: CharacterSet(charactersIn: " \t\""))
        return name.isEmpty ? nil : name
    }

    /// Reply-All recipients: sender to To, everyone else to Cc, deduped,
    /// dropping our own addresses unless the setting keeps them
    /// (PREF_NOT_ME semantics).
    private func replyAllRecipients(of msg: ParsedMessage) -> (to: String, cc: String) {
        var toAddrs = bareAddresses(of: msg, header: "Reply-To")
        if toAddrs.isEmpty { toAddrs = bareAddresses(of: msg, header: "From") }
        var ccAddrs = bareAddresses(of: msg, header: "To") +
            bareAddresses(of: msg, header: "Cc")
        if !settings.replyAllIncludesSelf {
            let own = Set(settings.personalities.map { $0.emailAddress.lowercased() }
                .filter { !$0.isEmpty })
            ccAddrs.removeAll { own.contains($0.lowercased()) }
        }
        var seen = Set(toAddrs.map { $0.lowercased() })
        let cc = ccAddrs.filter { seen.insert($0.lowercased()).inserted }
        return (toAddrs.joined(separator: ", "), cc.joined(separator: ", "))
    }

    /// Change a stored message's display priority (the Change > Priority menu).
    func setPriority(_ display: Int, messageAt index: Int, in boxName: String) {
        guard let mb = mailbox(named: boxName) else { return }
        mb.setPriority(display: display, at: index)
        try? mb.save()
        mailboxGeneration += 1
    }

    /// Junk / Not Junk: assign the transfer score (or clear it) and move the
    /// message to Junk (or back to In).  The score is stamped on the
    /// destination copy — transfers rewrite the summary.
    func markJunk(messageAt index: Int, from source: String, junk: Bool) {
        let score = junk ? settings.junkXferScore : 0
        let target = junk ? "Junk" : "In"
        guard let src = mailbox(named: source) else { return }
        if source == target {
            src.setSpamScore(score, at: index)
            try? src.save()
            mailboxGeneration += 1
            return
        }
        guard let dst = mailbox(named: target),
              let raw = try? src.rawMessage(at: index),
              let summary = src.summary(at: index),
              let newIndex = try? dst.append(message: raw, state: summary.state)
        else { return }
        dst.setSpamScore(score, at: newIndex)
        try? dst.save()
        src.delete(at: index)
        try? src.save()
        mailboxGeneration += 1
        statusText = junk ? "Marked as junk." : "Marked as not junk."
    }

    /// The classic Make Address Book Entry: file the sender as a nickname.
    func makeAddressBookEntry(mailbox boxName: String, index: Int) {
        guard let mb = mailbox(named: boxName),
              let raw = try? mb.rawMessage(at: index),
              let msg = try? ParsedMessage(raw: raw),
              let address = bareAddresses(of: msg, header: "From").first
        else { return }
        var nickname = displayName(ofFrom: msg) ?? ""
        if nickname.isEmpty || nickname.contains("@") {
            nickname = String(address.prefix(while: { $0 != "@" }))
        }
        guard !nickname.isEmpty,
              let book = (try? AddressBook(path: nicknamesURL.path))
                  ?? (try? AddressBook(text: ""))
        else { return }
        book.set(name: nickname, addresses: address)
        try? book.save(to: nicknamesURL.path)
        statusText = "Added \(nickname) to the Address Book."
    }

    // MARK: network operations

    private let checkMailCancel = CancelFlag()

    /// Check every personality marked include-in-checks, POP3 or IMAP,
    /// delivering into In.
    func checkMail(auto: Bool = false) {
        guard !isCheckingMail else { return }
        let accounts = settings.personalities.filter {
            $0.includeInChecks && !$0.popHost.isEmpty
        }
        guard !accounts.isEmpty else {
            if !auto { statusText = "Set up a personality in Settings first." }
            return
        }
        // PREF_SEND_CHECK: queued mail goes out with every check.
        if settings.sendOnCheck {
            sendQueuedMessages(quiet: true)
        }
        isCheckingMail = true
        statusText = "Checking mail…"
        let inboxPath = mailFolder.appendingPathComponent("In").path
        checkMailCancel.reset()
        let cancel = checkMailCancel
        let multi = accounts.count > 1

        Task.detached {
            var total = 0
            var failure: String?
            for account in accounts {
                if cancel.isCancelled { break }
                let prefix = multi ? "\(account.name): " : ""
                let host = account.popHost
                let progress: FetchProgress = { stage, done, msgTotal in
                    let text: String
                    switch stage {
                    case "connect": text = "\(prefix)Connecting to \(host)…"
                    case "auth": text = "\(prefix)Logging in…"
                    case "list": text = "\(prefix)Looking for new mail…"
                    case "retr":
                        if msgTotal == 0 {
                            text = "\(prefix)No new mail on the server."
                        } else if done >= msgTotal {
                            text = "\(prefix)Retrieved \(msgTotal) message\(msgTotal == 1 ? "" : "s")."
                        } else {
                            text = "\(prefix)Retrieving message \(done + 1) of \(msgTotal)…"
                        }
                    default: text = "\(prefix)Checking mail at \(host)…"
                    }
                    Task { @MainActor [weak self] in
                        self?.statusText = text
                    }
                    return !cancel.isCancelled
                }
                let tls: TLSMode = {
                    switch account.popSecurity {
                    case .none: return .none
                    case .startTLS: return .startTLS
                    case .immediateTLS: return .immediate
                    }
                }()
                do {
                    let n: Int
                    switch account.accountType {
                    case .pop3:
                        n = try pop3Fetch(
                            host: host, port: account.popPort, tls: tls,
                            user: account.username,
                            password: account.password,
                            mailboxPath: inboxPath,
                            deleteFromServer: !account.leaveOnServer,
                            maxMessageK: account.skipBigMessages
                                ? account.bigMessageLimitK : 0,
                            leaveOnServerDays: account.leaveOnServer
                                ? account.leaveOnServerDays : 0,
                            progress: progress)
                    case .imap:
                        n = try imapFetch(
                            host: host, port: account.popPort, tls: tls,
                            user: account.username,
                            password: account.password,
                            mailboxPath: inboxPath,
                            deleteFromServer: !account.leaveOnServer,
                            progress: progress)
                    }
                    total += n
                } catch {
                    failure = "\(multi ? account.name + ": " : "")\(error)"
                    break
                }
            }
            let totalFinal = total
            let failureFinal = failure
            let wasStopped = cancel.isCancelled
            await MainActor.run { [weak self] in
                guard let self else { return }
                self.isCheckingMail = false
                self.postCheckPipeline(newCount: wasStopped ? 0 : totalFinal)
                if let failure = failureFinal {
                    self.statusText = "Check failed: \(failure)"
                } else if wasStopped {
                    self.statusText = totalFinal == 0 ? "Mail check stopped."
                        : "Mail check stopped after \(totalFinal) message\(totalFinal == 1 ? "" : "s")."
                } else {
                    self.statusText = totalFinal == 0 ? "You have no new mail."
                        : "You have \(totalFinal) new message\(totalFinal == 1 ? "" : "s")."
                }
            }
        }
    }

    /// After a successful check: refresh, filter, sweep junk, and get the
    /// user's attention — the classic end-of-POP pipeline.
    private func postCheckPipeline(newCount: Int) {
        refreshMailbox(named: "In")
        runFilters(on: "In", event: .incoming, quiet: true)
        junkSweep()
        junkAging()
        if newCount > 0 {
            NewMailAttention.notify(count: newCount, settings: settings)
            if settings.openMailboxOnNewMail {
                selectedMailbox = "In"
            }
        }
        updateDockBadge()
    }

    /// FilterJunk/MoveToJunk: everything in In scoring at or above the junk
    /// threshold moves to the Junk mailbox.
    private func junkSweep() {
        guard let inbox = mailbox(named: "In") else { return }
        let threshold = settings.junkThreshold
        let junky = inbox.summaries
            .filter { $0.spamScore >= threshold }
            .map(\.index)
        for index in junky.sorted(by: >) {
            transfer(messageAt: index, from: "In", to: "Junk")
        }
    }

    /// ArchiveJunk: junk older than the empty-after window goes to Trash.
    private func junkAging() {
        guard settings.junkEmptyAfterDays > 0,
              let junk = mailbox(named: "Junk") else { return }
        let cutoff = Date().addingTimeInterval(
            -Double(settings.junkEmptyAfterDays) * 86400)
        let old = junk.summaries
            .filter { $0.arrival < cutoff }
            .map(\.index)
        for index in old.sorted(by: >) {
            transfer(messageAt: index, from: "Junk", to: "Trash")
        }
    }

    /// The Dock badge shows the In unread count (Getting Attention).
    func updateDockBadge() {
        let unread = mailbox(named: "In")?.summaries
            .filter { $0.state == .unread }.count ?? 0
        NewMailAttention.updateDockBadge(unread: unread,
                                         enabled: settings.dockBadgeUnread)
    }

    // MARK: automatic checking (PREF_AUTO_CHECK / PREF_INTERVAL)

    private var autoCheckTask: Task<Void, Never>?
    private var autoCheckConfig: (enabled: Bool, minutes: Int) = (false, 0)

    /// (Re)arms the auto-check loop when its settings changed.
    func refreshAutoCheck() {
        let config = (settings.autoCheck, settings.checkIntervalMinutes)
        guard config != autoCheckConfig else { return }
        autoCheckConfig = config
        autoCheckTask?.cancel()
        autoCheckTask = nil
        guard settings.autoCheck, settings.checkIntervalMinutes > 0 else { return }
        let minutes = settings.checkIntervalMinutes
        autoCheckTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: UInt64(minutes) * 60_000_000_000)
                guard !Task.isCancelled, let self else { break }
                if !self.isCheckingMail {
                    self.checkMail(auto: true)
                }
            }
        }
    }

    /// The Stop button next to the status spinner.  The fetch notices at its
    /// next progress step; anything already downloaded stays in the mailbox.
    func stopCheckingMail() {
        guard isCheckingMail else { return }
        checkMailCancel.cancel()
        statusText = "Stopping…"
    }

    /// Send every QUEUED message in Out (the classic Send Queued Messages).
    /// quiet suppresses the nothing-to-do chatter for send-on-check.
    func sendQueuedMessages(quiet: Bool = false) {
        let account = settings.dominant
        guard !account.smtpHost.isEmpty else {
            if !quiet { statusText = "Set up an SMTP server in Settings first." }
            return
        }
        guard let out = mailbox(named: "Out") else { return }

        var queued: [(index: Int, raw: String, recipients: String, sender: String)] = []
        for i in 0..<out.count {
            guard let sum = out.summary(at: i), sum.state == .queued,
                  let raw = try? out.rawMessage(at: i),
                  let msg = try? ParsedMessage(raw: raw)
            else { continue }
            var rcpts: [String] = []
            for header in ["To", "Cc", "Bcc"] {
                if let v = msg.header(header) {
                    rcpts.append(contentsOf: parseAddresses(v)
                        .filter { !$0.isEmpty && $0 != ";" && !$0.hasSuffix(":") })
                }
            }
            let sender = msg.header("From").map { parseAddresses($0).first ?? "" } ?? ""
            queued.append((i, raw, rcpts.joined(separator: ", "), sender))
        }
        guard !queued.isEmpty else {
            if !quiet { statusText = "No queued messages." }
            return
        }

        statusText = "Sending \(queued.count) queued message\(queued.count == 1 ? "" : "s")…"
        let tls: TLSMode = {
            switch account.smtpSecurity {
            case .none: return .none
            case .startTLS: return .startTLS
            case .immediateTLS: return .immediate
            }
        }()

        Task.detached {
            var sent: [Int] = []
            var failure: String?
            for item in queued {
                do {
                    _ = try smtpSend(host: account.smtpHost, port: account.smtpPort,
                                     tls: tls, user: account.username,
                                     password: account.password,
                                     from: item.sender.isEmpty
                                         ? account.emailAddress : item.sender,
                                     recipients: item.recipients,
                                     message: item.raw)
                    sent.append(item.index)
                } catch {
                    failure = "\(error)"
                    break
                }
            }
            let sentFinal = sent
            let failureFinal = failure
            await MainActor.run { [weak self] in
                guard let self else { return }
                if let out = self.mailbox(named: "Out") {
                    // Mark sent, highest index first so indices stay valid.
                    for index in sentFinal.sorted(by: >) {
                        out.setState(.sent, at: index)
                    }
                    try? out.save()
                }
                self.refreshMailbox(named: "Out")
                self.statusText = failureFinal.map { "Send failed: \($0)" }
                    ?? "Sent \(sentFinal.count) message\(sentFinal.count == 1 ? "" : "s")."
            }
        }
    }

    /// Queue a composed message into Out, running outgoing filters over
    /// the queued copy (the classic on-queue filter pass).
    func queue(message: String) {
        guard let out = mailbox(named: "Out"),
              let index = try? out.append(message: message, state: .queued)
        else { return }
        if let filters = try? FilterSet(path: filtersURL.path) {
            let book = try? AddressBook(path: nicknamesURL.path)
            let fired = filters.run(on: message, event: .outgoing,
                                    addressBook: book)
            var transfers: [PendingTransfer] = []
            applyActions(fired, mailbox: out, index: index,
                         transfers: &transfers)
            applyTransfers(transfers, from: "Out")
        }
        try? out.save()
        refreshMailbox(named: "Out")
        statusText = "Message queued."
    }

    // MARK: filters

    private struct PendingTransfer {
        let index: Int
        let target: String
        let copy: Bool
    }

    /// Run filters over a mailbox.  Check Mail passes .incoming; the
    /// Filter Messages menu passes .manual (mark filters Manual to use ⌘J,
    /// exactly like the original).
    func runFilters(on mailboxName: String, event: FilterEvent = .manual,
                    quiet: Bool = false) {
        guard let filters = try? FilterSet(path: filtersURL.path),
              let mb = mailbox(named: mailboxName)
        else { return }
        let book = try? AddressBook(path: nicknamesURL.path)

        var transfers: [PendingTransfer] = []
        for i in 0..<mb.count {
            guard let raw = try? mb.rawMessage(at: i) else { continue }
            let fired = filters.run(on: raw, event: event, addressBook: book)
            applyActions(fired, mailbox: mb, index: i, transfers: &transfers)
        }
        applyTransfers(transfers, from: mailboxName)
        try? mb.save()
        mailboxGeneration += 1
        if !quiet {
            statusText = "Filters run on \(mailboxName)."
        }
    }

    /// Executes one message's fired actions ("stop" is already handled by
    /// the engine's ordering; transfers/copies are deferred so indices
    /// stay valid).  Unsupported classic actions (speak, open, print,
    /// forward, redirect, reply, personality) are silently skipped.
    private func applyActions(_ fired: [FiredAction], mailbox mb: Mailbox,
                              index: Int,
                              transfers: inout [PendingTransfer]) {
        for action in fired {
            switch action.keyword {
            case "transfer":
                transfers.append(PendingTransfer(index: index,
                                                 target: action.value,
                                                 copy: false))
            case "copy":
                transfers.append(PendingTransfer(index: index,
                                                 target: action.value,
                                                 copy: true))
            case "junk":
                mb.setSpamScore(Int(action.value) ?? settings.junkXferScore,
                                at: index)
            case "label":
                mb.setLabel(Int(action.value) ?? 0, at: index)
            case "status":
                if let v = UInt8(action.value),
                   let state = MessageState(rawValue: v) {
                    mb.setState(state, at: index)
                }
            case "priority":
                applyPriorityAction(action.value, mailbox: mb, index: index)
            case "subject":
                if !action.value.isEmpty {
                    mb.setSubject(action.value, at: index)
                }
            case "sound":
                NewMailAttention.playSound(named: action.value)
            case "notifyUser":
                statusText = "Filter \"\(action.filterName)\" matched."
            default:
                break
            }
        }
    }

    /// Priority action values: "1"-"5" set the display priority; the
    /// legacy Raise/Lower verbs are canonicalized to "7"/"8" at load.
    private func applyPriorityAction(_ value: String, mailbox mb: Mailbox,
                                     index: Int) {
        guard let sum = mb.summary(at: index) else { return }
        switch value {
        case "7":
            mb.setPriority(display: max(1, sum.priorityDisplay - 1), at: index)
        case "8":
            mb.setPriority(display: min(5, sum.priorityDisplay + 1), at: index)
        default:
            if let v = Int(value), (1...5).contains(v) {
                mb.setPriority(display: v, at: index)
            }
        }
    }

    private func applyTransfers(_ transfers: [PendingTransfer],
                                from mailboxName: String) {
        for t in transfers.sorted(by: { $0.index > $1.index }) {
            if t.copy {
                copyMessage(at: t.index, from: mailboxName, to: t.target)
            } else {
                transfer(messageAt: t.index, from: mailboxName, to: t.target)
            }
        }
    }

    /// The Copy To filter action: append without removing the original.
    func copyMessage(at index: Int, from source: String, to target: String) {
        guard source != target,
              let src = mailbox(named: source),
              let dst = mailbox(named: target),
              let raw = try? src.rawMessage(at: index),
              let summary = src.summary(at: index),
              (try? dst.append(message: raw, state: summary.state)) != nil
        else { return }
        try? dst.save()
        mailboxGeneration += 1
    }

    func saveSettings() {
        settings.save(to: settingsURL)
        refreshAutoCheck()
        updateDockBadge()
    }
}

#endif // os(macOS)
