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
    case unread = 1, read = 2, replied = 3, redistributed = 4, unsendable = 5
    case sendable = 6, queued = 7, forwarded = 8, sent = 9, unsent = 10
    case timed = 11, rebuilt = 14
    case other = 0
}

public struct MessageSummary {
    public let index: Int
    public let from: String
    public let subject: String
    public let date: Date
    public let arrival: Date
    public let state: MessageState
    public let priorityDisplay: Int
    public let spamScore: Int
    public let length: Int
    public let serialNumber: Int
    public let flags: UInt32
    public let opts: UInt32

    /// FLAG_HAS_ATT (bit 13).
    public var hasAttachments: Bool { flags & (1 << 13) != 0 }
    /// Classic label color index, 0 (none) through 15 (flags bits 14-17).
    public var labelIndex: Int { Int((flags >> 14) & 0xF) }
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
            arrival: Date(timeIntervalSince1970: TimeInterval(s.arrival_unix)),
            state: MessageState(rawValue: s.state) ?? .other,
            priorityDisplay: Int(s.priority_display),
            spamScore: Int(s.spam_score),
            length: Int(s.length),
            serialNumber: Int(s.serial_num),
            flags: s.flags,
            opts: s.opts)
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

    public func setLabel(_ label: Int, at index: Int) {
        _ = eudora_mailbox_set_label(handle, Int32(index), Int32(label))
    }

    /// Display priority 1 (highest) through 5 (lowest); 3 is normal.
    public func setPriority(display: Int, at index: Int) {
        _ = eudora_mailbox_set_priority(handle, Int32(index), Int32(display))
    }

    /// Junk score 0-100 (clamped by the engine).
    public func setSpamScore(_ score: Int, at index: Int) {
        _ = eudora_mailbox_set_spam_score(handle, Int32(index), Int32(score))
    }

    /// Replace the summary's subject (the stored message is unchanged).
    public func setSubject(_ subject: String, at index: Int) {
        _ = eudora_mailbox_set_subject(handle, Int32(index), subject)
    }

    /// Index of the message with the given serial number, or nil.
    public func findBySerial(_ serial: Int) -> Int? {
        let idx = eudora_mailbox_find_by_serial(handle, Int32(serial))
        return idx >= 0 ? Int(idx) : nil
    }

    /// Appends a complete RFC 822 message (any line-end convention) with a
    /// proper envelope and summary; returns the new index.  Call `save()`
    /// to persist the TOC.
    @discardableResult
    public func append(message: String, state: MessageState? = nil) throws -> Int {
        let idx = message.withCString { cstr in
            eudora_mailbox_append_message(handle, cstr, strlen(cstr),
                                          state?.rawValue ?? 0)
        }
        guard idx >= 0 else { throw EudoraError.fromLast() }
        return Int(idx)
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

/// The classic 15-label palette: 1-7 are the System 7 Finder label colors
/// the original inherited; 8-15 are Eudora's private labels (PrivColors,
/// STR# 27900, 16-bit RGB).  Index 0 means no label.
public enum MessageLabel {
    public static let names: [String] = [
        "None", "Essential", "Hot", "In Progress", "Cool", "Personal",
        "Project 1", "Project 2",
        "Label 8", "Label 9", "Label 10", "Label 11", "Label 12",
        "Label 13", "Label 14", "Label 15",
    ]

    /// RGB components 0...1, indexed by label (0 = no color).
    public static let colors: [(r: Double, g: Double, b: Double)] = [
        (0, 0, 0),                    // 0: none
        (1.00, 0.40, 0.00),           // 1: orange
        (0.87, 0.13, 0.13),           // 2: red
        (0.94, 0.35, 0.87),           // 3: pink
        (0.40, 0.87, 0.94),           // 4: light blue
        (0.00, 0.00, 0.87),           // 5: dark blue
        (0.00, 0.60, 0.13),           // 6: green
        (0.60, 0.40, 0.20),           // 7: brown
        (1.000, 0.200, 0.200),        // 8:  65535,13107,13107
        (1.000, 0.200, 0.800),        // 9:  65535,13107,52428
        (0.400, 0.200, 1.000),        // 10: 26214,13107,65535
        (0.200, 0.600, 1.000),        // 11: 13107,39321,65535
        (0.200, 1.000, 0.800),        // 12: 13107,65535,52428
        (0.400, 1.000, 0.200),        // 13: 26214,65535,13107
        (1.000, 1.000, 0.400),        // 14: 65535,65535,26214
        (0.357, 0.357, 0.200),        // 15: 23409,23409,13107
    ]
}

// MARK: - Messages

public final class ParsedMessage {
    fileprivate let handle: OpaquePointer

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

/// One leaf MIME part of a parsed message.  Holds the message alive.
public struct MessagePart {
    private let owner: ParsedMessage
    public let index: Int
    public let type: String
    public let subtype: String
    public let filename: String
    public let isAttachment: Bool
    public let depth: Int
    /// Encoded body length in bytes (an upper bound on the decoded size).
    public let size: Int

    fileprivate init?(owner: ParsedMessage, index: Int) {
        var info = eudora_part_info()
        guard eudora_message_part_info(owner.handle, Int32(index), &info) == 1
        else { return nil }
        self.owner = owner
        self.index = index
        self.type = info.type.map { String(cString: $0) } ?? ""
        self.subtype = info.subtype.map { String(cString: $0) } ?? ""
        self.filename = info.filename.map { String(cString: $0) } ?? ""
        self.isAttachment = info.is_attachment != 0
        self.depth = Int(info.depth)
        self.size = Int(info.size)
    }

    /// The part's body with its transfer encoding undone.  Binary-safe:
    /// the bytes travel by pointer+length, never as a C string.
    public func decode() -> Data {
        var len = 0
        guard let p = eudora_message_part_decode(owner.handle, Int32(index),
                                                 &len)
        else { return Data() }
        defer { eudora_string_free(p) }
        return Data(bytes: p, count: len)
    }
}

extension ParsedMessage {
    /// Leaf MIME parts in document order; one part for a plain message.
    public var parts: [MessagePart] {
        (0..<Int(eudora_message_part_count(handle))).compactMap {
            MessagePart(owner: self, index: $0)
        }
    }

    public var attachments: [MessagePart] { parts.filter(\.isAttachment) }

    /// The most readable body text: for a multipart message, the first
    /// text/plain (else any text) leaf, decoded; otherwise the decoded
    /// whole body.
    public var bestBodyText: String {
        let all = parts
        if all.count > 1,
           let text = all.first(where: { $0.type == "text" && $0.subtype == "plain" })
               ?? all.first(where: { $0.type == "text" }) {
            return String(decoding: text.decode(), as: UTF8.self)
        }
        return decodedBody
    }
}

public func parseAddresses(_ headerValue: String) -> [String] {
    takeStringArray(eudora_parse_addresses(headerValue))
}

public enum BodyEncoding: Int32 {
    case none = 0, quotedPrintable = 1, base64 = 2
}

/// Decodes a quoted-printable or base64 body part.
public func decodeBody(_ data: String, encoding: BodyEncoding) -> String {
    var outLen = 0
    let decoded = data.withCString { cstr in
        eudora_decode_body(cstr, strlen(cstr), encoding.rawValue, &outLen)
    }
    guard let decoded else { return data }
    defer { eudora_string_free(decoded) }
    return String(cString: decoded)
}

extension ParsedMessage {
    /// 0 = plain/7bit/8bit, 1 = quoted-printable, 2 = base64, 3 = other.
    public var transferEncoding: Int {
        Int(eudora_message_transfer_encoding(handle))
    }

    /// The body with its Content-Transfer-Encoding undone (QP/base64).
    public var decodedBody: String {
        switch transferEncoding {
        case 1: return decodeBody(body, encoding: .quotedPrintable)
        case 2: return decodeBody(body, encoding: .base64)
        default: return body
        }
    }
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

public struct FilterTerm: Equatable {
    public var header: String
    public var verb: String // on-disk verb: "contains", "!is", "regex", ...
    public var value: String

    public init(header: String = "", verb: String = "contains", value: String = "") {
        self.header = header
        self.verb = verb
        self.value = value
    }

    /// On-disk verb names in MatchEnum order, paired with the classic
    /// editor's display names (FiltVerbPrint, STR# 28700).
    public static let verbs: [(raw: String, display: String)] = [
        ("contains", "contains"),
        ("!contains", "does not contain"),
        ("is", "is"),
        ("!is", "is not"),
        ("starts", "starts with"),
        ("ends", "ends with"),
        ("appears", "appears"),
        ("!appears", "does not appear"),
        ("intersects", "intersects nickname"),
        ("disjoint", "does not intersect nickname"),
        ("intersectsFile", "intersects address book"),
        ("disjointFile", "does not intersect address book"),
        ("regex", "matches regular expression"),
        ("less", "is less than"),
        ("greater", "is greater than"),
    ]
}

public struct FilterRecord: Equatable {
    public var name: String
    public var id: Int
    public var incoming: Bool
    public var outgoing: Bool
    public var manual: Bool
    public var term1: FilterTerm
    public var conjunction: String // "ignore", "and", "or", "unless"
    public var term2: FilterTerm

    public init(name: String = "Untitled", id: Int = 0, incoming: Bool = true,
                outgoing: Bool = false, manual: Bool = false,
                term1: FilterTerm = FilterTerm(), conjunction: String = "ignore",
                term2: FilterTerm = FilterTerm()) {
        self.name = name
        self.id = id
        self.incoming = incoming
        self.outgoing = outgoing
        self.manual = manual
        self.term1 = term1
        self.conjunction = conjunction
        self.term2 = term2
    }
}

public struct FilterActionRecord: Equatable {
    public var keyword: String // "transfer", "junk", "stop", ...
    public var value: String

    public init(keyword: String, value: String = "") {
        self.keyword = keyword
        self.value = value
    }

    /// Action keywords the engine executes, with editor display names.
    public static let keywords: [(raw: String, display: String)] = [
        ("status", "Make Status"), ("priority", "Make Priority"),
        ("label", "Make Label"), ("personality", "Make Personality"),
        ("subject", "Make Subject"), ("sound", "Play Sound"),
        ("speak", "Speak"), ("open", "Open"), ("print", "Print"),
        ("notifyUser", "Notify User"), ("forward", "Forward To"),
        ("redirect", "Redirect To"), ("reply", "Reply With"),
        ("copy", "Copy To"), ("transfer", "Transfer To"),
        ("junk", "Junk Score"), ("stop", "Skip Rest"),
    ]
}

public final class FilterSet {
    private let handle: OpaquePointer

    public init() {
        self.handle = eudora_filters_new()
    }

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

    // MARK: editing

    public func record(at index: Int) -> FilterRecord? {
        var info = eudora_filter_info()
        guard eudora_filters_get(handle, Int32(index), &info) == 1 else { return nil }
        func str(_ p: UnsafePointer<CChar>?) -> String {
            p.map { String(cString: $0) } ?? ""
        }
        return FilterRecord(
            name: str(info.name), id: Int(info.id),
            incoming: info.incoming != 0, outgoing: info.outgoing != 0,
            manual: info.manual != 0,
            term1: FilterTerm(header: str(info.header1), verb: str(info.verb1),
                              value: str(info.value1)),
            conjunction: str(info.conjunction),
            term2: FilterTerm(header: str(info.header2), verb: str(info.verb2),
                              value: str(info.value2)))
    }

    public var records: [FilterRecord] {
        (0..<count).compactMap { record(at: $0) }
    }

    public func update(_ record: FilterRecord, at index: Int) {
        record.name.withCString { name in
            record.term1.header.withCString { h1 in
                record.term1.verb.withCString { v1 in
                    record.term1.value.withCString { val1 in
                        record.conjunction.withCString { conj in
                            record.term2.header.withCString { h2 in
                                record.term2.verb.withCString { v2 in
                                    record.term2.value.withCString { val2 in
                                        var info = eudora_filter_info()
                                        info.name = name
                                        info.incoming = record.incoming ? 1 : 0
                                        info.outgoing = record.outgoing ? 1 : 0
                                        info.manual = record.manual ? 1 : 0
                                        info.header1 = h1
                                        info.verb1 = v1
                                        info.value1 = val1
                                        info.conjunction = conj
                                        info.header2 = h2
                                        info.verb2 = v2
                                        info.value2 = val2
                                        _ = eudora_filters_set(handle, Int32(index), &info)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /// Appends a blank incoming filter; returns its index.
    @discardableResult
    public func add(name: String = "Untitled") -> Int {
        Int(eudora_filters_add(handle, name))
    }

    @discardableResult
    public func remove(at index: Int) -> Bool {
        eudora_filters_remove(handle, Int32(index)) == 1
    }

    @discardableResult
    public func move(from: Int, to: Int) -> Bool {
        eudora_filters_move(handle, Int32(from), Int32(to)) == 1
    }

    public func actions(at index: Int) -> [FilterActionRecord] {
        let n = Int(eudora_filter_action_count(handle, Int32(index)))
        return (0..<n).compactMap { j in
            var kw: UnsafePointer<CChar>?
            var val: UnsafePointer<CChar>?
            guard eudora_filter_action_get(handle, Int32(index), Int32(j), &kw, &val) == 1
            else { return nil }
            return FilterActionRecord(keyword: kw.map { String(cString: $0) } ?? "",
                                      value: val.map { String(cString: $0) } ?? "")
        }
    }

    @discardableResult
    public func addAction(_ action: FilterActionRecord, at index: Int) -> Bool {
        eudora_filter_action_add(handle, Int32(index), action.keyword, action.value) == 1
    }

    @discardableResult
    public func setAction(_ action: FilterActionRecord, at index: Int, slot: Int) -> Bool {
        eudora_filter_action_set(handle, Int32(index), Int32(slot),
                                 action.keyword, action.value) == 1
    }

    @discardableResult
    public func removeAction(at index: Int, slot: Int) -> Bool {
        eudora_filter_action_remove(handle, Int32(index), Int32(slot)) == 1
    }

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
    @discardableResult public func replyTo(_ address: String) -> Composer {
        eudora_composer_reply_to(handle, address); return self
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

/// Progress reported during `pop3Fetch`: the stage ("connect", "auth",
/// "list", or "retr"), the number of messages stored so far, and the total
/// that will be fetched (both counts are 0 outside the "retr" stage).
/// Return `false` to stop the fetch: messages already stored are kept and
/// the mailbox's table of contents is still written.
public typealias FetchProgress = (_ stage: String, _ done: Int, _ total: Int) -> Bool

private final class FetchProgressBox {
    let callback: FetchProgress
    init(_ callback: @escaping FetchProgress) { self.callback = callback }
}

/// Fetches new messages from a POP3 server into the mailbox at `mailboxPath`
/// (creating it if needed) and updates its ".toc".  Messages fetched on an
/// earlier check are recognized by their UIDL and skipped.  Returns the
/// number of messages fetched (a stopped fetch returns the count stored
/// before the stop, without throwing).
///
/// `maxMessageK` skips (leaves on the server) messages over that many KB;
/// `leaveOnServerDays` deletes already-fetched messages from the server
/// once their local arrival is older than that many days (0 = keep forever).
public func pop3Fetch(host: String, port: UInt16, tls: TLSMode = .none,
                      user: String, password: String,
                      mailboxPath: String, deleteFromServer: Bool = false,
                      maxMessageK: Int = 0, leaveOnServerDays: Int = 0,
                      progress: FetchProgress? = nil) throws -> Int {
    var opts = eudora_pop3_options()
    opts.delete_from_server = deleteFromServer ? 1 : 0
    opts.leave_on_server_days = Int32(leaveOnServerDays)
    opts.max_message_k = Int32(maxMessageK)

    let n: Int32
    if let progress {
        let box = FetchProgressBox(progress)
        n = withExtendedLifetime(box) {
            eudora_pop3_fetch_opts(
                host, port, tls.rawValue, user, password, mailboxPath, &opts,
                { ctx, stage, done, total in
                    guard let ctx, let stage else { return 0 }
                    let box = Unmanaged<FetchProgressBox>.fromOpaque(ctx)
                        .takeUnretainedValue()
                    return box.callback(String(cString: stage),
                                        Int(done), Int(total)) ? 0 : 1
                },
                Unmanaged.passUnretained(box).toOpaque())
        }
    } else {
        n = eudora_pop3_fetch_opts(host, port, tls.rawValue, user, password,
                                   mailboxPath, &opts, nil, nil)
    }
    guard n >= 0 else { throw EudoraError.fromLast() }
    return Int(n)
}

/// Fetches new messages from an IMAP mailbox (default INBOX) into the local
/// mailbox at `mailboxPath`.  Messages already fetched are recognized by
/// their UIDVALIDITY/UID and skipped; server flags choose the initial state
/// (\Seen -> read).  deleteFromServer flags fetched messages \Deleted and
/// expunges.  Progress and cancellation exactly as `pop3Fetch`.
public func imapFetch(host: String, port: UInt16, tls: TLSMode = .none,
                      user: String, password: String,
                      imapMailbox: String = "INBOX",
                      mailboxPath: String, deleteFromServer: Bool = false,
                      progress: FetchProgress? = nil) throws -> Int {
    let n: Int32
    if let progress {
        let box = FetchProgressBox(progress)
        n = withExtendedLifetime(box) {
            eudora_imap_fetch_ex(
                host, port, tls.rawValue, user, password, imapMailbox,
                mailboxPath, deleteFromServer ? 1 : 0,
                { ctx, stage, done, total in
                    guard let ctx, let stage else { return 0 }
                    let box = Unmanaged<FetchProgressBox>.fromOpaque(ctx)
                        .takeUnretainedValue()
                    return box.callback(String(cString: stage),
                                        Int(done), Int(total)) ? 0 : 1
                },
                Unmanaged.passUnretained(box).toOpaque())
        }
    } else {
        n = eudora_imap_fetch_ex(host, port, tls.rawValue, user, password,
                                 imapMailbox, mailboxPath,
                                 deleteFromServer ? 1 : 0, nil, nil)
    }
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
