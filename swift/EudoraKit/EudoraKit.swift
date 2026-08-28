// EudoraKit — idiomatic Swift wrappers over the EudoraCore C interface.
//
// This is the attachment point for a SwiftUI frontend: mailboxes,
// messages, the address book, filters, composition, and one-call
// POP3 fetch / SMTP send.
//
// All C handle types are opaque structs, which Swift imports as
// OpaquePointer; the classes below own one handle each.

import CEudoraCore
import Foundation

public enum EudoraError: Error, CustomStringConvertible {
    case failure(String)

    public var description: String {
        if case let .failure(message) = self { return message }
        return "unknown error"
    }

    static func fromLast() -> EudoraError {
        .failure(String(cString: eudora_last_error()))
    }
}

private func takeString(_ p: UnsafeMutablePointer<CChar>?) -> String? {
    guard let p else { return nil }
    defer { eudora_string_free(p) }
    return String(cString: p)
}

private func takeStringArray(
    _ arr: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?
) -> [String] {
    guard let arr else { return [] }
    defer { eudora_addresses_free(arr) }
    var out: [String] = []
    var i = 0
    while let p = arr[i] {
        out.append(String(cString: p))
        i += 1
    }
    return out
}

// MARK: - Mailboxes

public enum MessageState: UInt8 {
    case unread = 1, read = 2, replied = 3, sendable = 6, queued = 7
    case forwarded = 8, sent = 9, rebuilt = 14
    case other = 0
}

public struct MessageSummary {
    public let index: Int
    public let from: String
    public let subject: String
    public let date: Date
    public let state: MessageState
    public let priorityDisplay: Int
    public let spamScore: Int
    public let length: Int
    public let serialNumber: Int
}

/// A classic Eudora mailbox: the mbox file plus its ".toc" sidecar.
public final class Mailbox {
    private let handle: OpaquePointer

    public init(path: String) throws {
        guard let h = eudora_mailbox_open(path) else { throw EudoraError.fromLast() }
        self.handle = h
    }

    deinit { eudora_mailbox_close(handle) }

    public var count: Int { Int(eudora_mailbox_count(handle)) }

    public func summary(at index: Int) -> MessageSummary? {
        var s = eudora_summary()
        guard eudora_mailbox_summary(handle, Int32(index), &s) == 1 else { return nil }
        return MessageSummary(
            index: index,
            from: s.from.map { String(cString: $0) } ?? "",
            subject: s.subject.map { String(cString: $0) } ?? "",
            date: Date(timeIntervalSince1970: TimeInterval(s.date_unix)),
            state: MessageState(rawValue: s.state) ?? .other,
            priorityDisplay: Int(s.priority_display),
            spamScore: Int(s.spam_score),
            length: Int(s.length),
            serialNumber: Int(s.serial_num))
    }

    public var summaries: [MessageSummary] {
        (0..<count).compactMap { summary(at: $0) }
    }

    /// Raw message text (headers + body) as stored in the mbox.
    public func rawMessage(at index: Int) throws -> String {
        guard let text = takeString(eudora_mailbox_read_message(handle, Int32(index))) else {
            throw EudoraError.fromLast()
        }
        return text
    }

    public func message(at index: Int) throws -> ParsedMessage {
        try ParsedMessage(raw: rawMessage(at: index))
    }

    public func setState(_ state: MessageState, at index: Int) {
        _ = eudora_mailbox_set_state(handle, Int32(index), state.rawValue)
    }

    @discardableResult
    public func delete(at index: Int) -> Bool {
        eudora_mailbox_delete(handle, Int32(index)) == 1
    }

    /// Rewrites the mbox dropping deleted bytes and saves the TOC.
    public func compact() throws {
        guard eudora_mailbox_compact(handle) == 1 else { throw EudoraError.fromLast() }
    }

    /// Persists the ".toc" sidecar.
    public func save() throws {
        guard eudora_mailbox_save(handle) == 1 else { throw EudoraError.fromLast() }
    }
}

// MARK: - Messages

public final class ParsedMessage {
    private let handle: OpaquePointer

    public init(raw: String) throws {
        let h = raw.withCString { cstr in
            eudora_message_parse(cstr, strlen(cstr))
        }
        guard let h else { throw EudoraError.fromLast() }
        self.handle = h
    }

    deinit { eudora_message_free(handle) }

    public func header(_ name: String) -> String? {
        takeString(eudora_message_header(handle, name))
    }

    /// RFC 2047-decoded header value.
    public func decodedHeader(_ name: String) -> String {
        takeString(eudora_message_header_decoded(handle, name)) ?? ""
    }

    public var body: String { String(cString: eudora_message_body(handle)) }
    public var contentType: String { String(cString: eudora_message_content_type(handle)) }
    public var contentSubtype: String { String(cString: eudora_message_content_subtype(handle)) }
    public var boundary: String { String(cString: eudora_message_boundary(handle)) }
    public var attachmentFilename: String { String(cString: eudora_message_filename(handle)) }
}

public func parseAddresses(_ headerValue: String) -> [String] {
    takeStringArray(eudora_parse_addresses(headerValue))
}

// MARK: - Address book

public final class AddressBook {
    let handle: OpaquePointer

    public init(path: String) throws {
        guard let h = eudora_addressbook_load(path) else { throw EudoraError.fromLast() }
        self.handle = h
    }

    public init(text: String) throws {
        guard let h = eudora_addressbook_parse(text) else { throw EudoraError.fromLast() }
        self.handle = h
    }

    deinit { eudora_addressbook_free(handle) }

    public var count: Int { Int(eudora_addressbook_count(handle)) }

    public struct Entry {
        public let name: String
        public let addresses: String
        public let notes: String
    }

    public func entry(at index: Int) -> Entry? {
        guard let n = eudora_addressbook_name(handle, Int32(index)) else { return nil }
        return Entry(
            name: String(cString: n),
            addresses: eudora_addressbook_addresses(handle, Int32(index))
                .map { String(cString: $0) } ?? "",
            notes: eudora_addressbook_notes(handle, Int32(index))
                .map { String(cString: $0) } ?? "")
    }

    public var entries: [Entry] { (0..<count).compactMap { entry(at: $0) } }

    public func set(name: String, addresses: String, notes: String = "") {
        _ = eudora_addressbook_set(handle, name, addresses, notes)
    }

    @discardableResult
    public func remove(name: String) -> Bool {
        eudora_addressbook_remove(handle, name) == 1
    }

    /// Recursively expands nicknames into bare addresses.
    public func expand(_ addressList: String) -> [String] {
        takeStringArray(eudora_addressbook_expand(handle, addressList))
    }

    public func contains(address: String) -> Bool {
        eudora_addressbook_contains(handle, address) == 1
    }

    public func save(to path: String) throws {
        guard eudora_addressbook_save(handle, path) == 1 else { throw EudoraError.fromLast() }
    }
}

// MARK: - Filters

public struct FiredAction {
    public let filterName: String
    public let keyword: String
    public let value: String
}

public enum FilterEvent: Int32 {
    case incoming = 0, outgoing = 1, manual = 2
}

public final class FilterSet {
    private let handle: OpaquePointer

    public init(path: String) throws {
        guard let h = eudora_filters_load(path) else { throw EudoraError.fromLast() }
        self.handle = h
    }

    public init(text: String) throws {
        guard let h = eudora_filters_parse(text) else { throw EudoraError.fromLast() }
        self.handle = h
    }

    deinit { eudora_filters_free(handle) }

    public var count: Int { Int(eudora_filters_count(handle)) }

    public func save(to path: String) throws {
        guard eudora_filters_save(handle, path) == 1 else { throw EudoraError.fromLast() }
    }

    /// Evaluates the filters against a raw message; actions come back in
    /// the engine's execution order.
    public func run(on rawMessage: String, event: FilterEvent,
                    addressBook: AddressBook? = nil) -> [FiredAction] {
        var count: Int32 = 0
        let fired = rawMessage.withCString { cstr in
            eudora_filters_run_with_book(handle, event.rawValue, cstr, strlen(cstr),
                                         addressBook?.handle, &count)
        }
        guard let fired else { return [] }
        defer { eudora_fired_actions_free(fired, count) }
        return (0..<Int(count)).map { i in
            FiredAction(
                filterName: fired[i].filter_name.map { String(cString: $0) } ?? "",
                keyword: fired[i].keyword.map { String(cString: $0) } ?? "",
                value: fired[i].value.map { String(cString: $0) } ?? "")
        }
    }
}

// MARK: - Composition

public final class Composer {
    private let handle: OpaquePointer

    public init() {
        self.handle = eudora_composer_new()
    }

    deinit { eudora_composer_free(handle) }

    @discardableResult public func from(name: String, address: String) -> Composer {
        eudora_composer_from(handle, name, address); return self
    }
    @discardableResult public func to(_ list: String) -> Composer {
        eudora_composer_to(handle, list); return self
    }
    @discardableResult public func cc(_ list: String) -> Composer {
        eudora_composer_cc(handle, list); return self
    }
    @discardableResult public func bcc(_ list: String) -> Composer {
        eudora_composer_bcc(handle, list); return self
    }
    @discardableResult public func subject(_ text: String) -> Composer {
        eudora_composer_subject(handle, text); return self
    }
    @discardableResult public func body(_ text: String) -> Composer {
        eudora_composer_body(handle, text); return self
    }
    @discardableResult public func header(_ name: String, _ value: String) -> Composer {
        eudora_composer_header(handle, name, value); return self
    }
    @discardableResult public func priority(_ display: Int) -> Composer {
        eudora_composer_priority(handle, Int32(display)); return self
    }
    @discardableResult public func attach(path: String, contentType: String? = nil,
                                          filename: String? = nil) -> Composer {
        eudora_composer_attach(handle, path, contentType, filename); return self
    }

    /// The complete RFC 822 message, ready for `smtpSend`.
    public func build() throws -> String {
        guard let msg = takeString(eudora_composer_build(handle)) else {
            throw EudoraError.fromLast()
        }
        return msg
    }

    public var sender: String { takeString(eudora_composer_sender(handle)) ?? "" }
    public var recipients: String { takeString(eudora_composer_recipients(handle)) ?? "" }
}

// MARK: - Network operations

public enum TLSMode: Int32 {
    case none = 0        // plaintext
    case startTLS = 1    // STLS/STARTTLS upgrade
    case immediate = 2   // TLS from connect (995/993/465)
}

/// Fetches all messages from a POP3 server into the mailbox at `mailboxPath`
/// (creating it if needed) and updates its ".toc".  Returns the number of
/// messages fetched.
public func pop3Fetch(host: String, port: UInt16, tls: TLSMode = .none,
                      user: String, password: String,
                      mailboxPath: String, deleteFromServer: Bool = false) throws -> Int {
    let n = eudora_pop3_fetch(host, port, tls.rawValue, user, password,
                              mailboxPath, deleteFromServer ? 1 : 0)
    guard n >= 0 else { throw EudoraError.fromLast() }
    return Int(n)
}

/// Sends a fully formed RFC 822 message.  Returns the final SMTP code.
@discardableResult
public func smtpSend(host: String, port: UInt16, tls: TLSMode = .none,
                     user: String? = nil, password: String? = nil,
                     from: String, recipients: String,
                     message: String) throws -> Int {
    let code = message.withCString { cstr in
        eudora_smtp_send(host, port, tls.rawValue, user, password,
                         from, recipients, cstr, strlen(cstr))
    }
    guard (200...299).contains(Int(code)) else {
        throw EudoraError.fromLast()
    }
    return Int(code)
}
