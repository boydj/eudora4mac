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

    // MARK: network operations

    private let checkMailCancel = CancelFlag()

    func checkMail() {
        guard !isCheckingMail else { return }
        let account = settings.dominant
        guard !account.popHost.isEmpty else {
            statusText = "Set up a personality in Settings first."
            return
        }
        isCheckingMail = true
        statusText = "Checking mail at \(account.popHost)…"
        let inboxPath = mailFolder.appendingPathComponent("In").path
        let host = account.popHost
        checkMailCancel.reset()
        let cancel = checkMailCancel

        Task.detached {
            let result: Result<Int, Error>
            do {
                let tls: TLSMode = {
                    switch account.popSecurity {
                    case .none: return .none
                    case .startTLS: return .startTLS
                    case .immediateTLS: return .immediate
                    }
                }()
                let n = try pop3Fetch(host: account.popHost, port: account.popPort,
                                      tls: tls, user: account.username,
                                      password: account.password,
                                      mailboxPath: inboxPath,
                                      deleteFromServer: !account.leaveOnServer,
                                      progress: { stage, done, total in
                    let text: String
                    switch stage {
                    case "connect": text = "Connecting to \(host)…"
                    case "auth": text = "Logging in…"
                    case "list": text = "Looking for new mail…"
                    case "retr":
                        if total == 0 {
                            text = "No new mail on the server."
                        } else if done >= total {
                            text = "Retrieved \(total) message\(total == 1 ? "" : "s")."
                        } else {
                            text = "Retrieving message \(done + 1) of \(total)…"
                        }
                    default: text = "Checking mail at \(host)…"
                    }
                    Task { @MainActor [weak self] in
                        self?.statusText = text
                    }
                    return !cancel.isCancelled
                })
                result = .success(n)
            } catch {
                result = .failure(error)
            }
            let wasStopped = cancel.isCancelled
            await MainActor.run { [weak self] in
                guard let self else { return }
                self.isCheckingMail = false
                self.refreshMailbox(named: "In")
                switch result {
                case .success(let n):
                    if wasStopped {
                        self.statusText = n == 0 ? "Mail check stopped."
                            : "Mail check stopped after \(n) message\(n == 1 ? "" : "s")."
                    } else {
                        self.statusText = n == 0 ? "You have no new mail."
                            : "You have \(n) new message\(n == 1 ? "" : "s")."
                    }
                case .failure(let error):
                    self.statusText = "Check failed: \(error)"
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
    func sendQueuedMessages() {
        let account = settings.dominant
        guard !account.smtpHost.isEmpty else {
            statusText = "Set up an SMTP server in Settings first."
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
            statusText = "No queued messages."
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

    /// Queue a composed message into Out.
    func queue(message: String) {
        guard let out = mailbox(named: "Out") else { return }
        if (try? out.append(message: message, state: .queued)) != nil {
            try? out.save()
            refreshMailbox(named: "Out")
            statusText = "Message queued."
        }
    }

    /// Run incoming filters over a message (used after Check Mail and from
    /// the Special menu's Filter Messages).
    func runFilters(on mailboxName: String) {
        guard let filters = try? FilterSet(path: filtersURL.path),
              let mb = mailbox(named: mailboxName)
        else { return }
        let book = try? AddressBook(path: nicknamesURL.path)

        var transfers: [(index: Int, target: String)] = []
        for i in 0..<mb.count {
            guard let raw = try? mb.rawMessage(at: i) else { continue }
            let fired = filters.run(on: raw, event: .incoming, addressBook: book)
            for action in fired {
                switch action.keyword {
                case "transfer":
                    transfers.append((i, action.value))
                case "junk":
                    mb.setState(.read, at: i)
                case "label":
                    mb.setLabel(Int(action.value) ?? 0, at: i)
                case "status":
                    break // states are engine-scanned; leave as-is
                default:
                    break
                }
            }
        }
        // Apply transfers last, highest index first.
        for t in transfers.sorted(by: { $0.index > $1.index }) {
            transfer(messageAt: t.index, from: mailboxName, to: t.target)
        }
        try? mb.save()
        mailboxGeneration += 1
        statusText = "Filters run on \(mailboxName)."
    }

    func saveSettings() {
        settings.save(to: settingsURL)
    }
}

#endif // os(macOS)
