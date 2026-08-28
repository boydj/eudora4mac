// The modern form of Eudora's settings: personalities (accounts) plus the
// global preferences that the classic Settings dialog kept in panels
// (Checking Mail, Sending Mail, Replying, Getting Attention, Junk Mail,
// Fonts & Display, Labels…).
//
// The legacy app kept these in the resource-fork "Eudora Settings" file;
// the modern app stores them as JSON in the mail folder
// ("EudoraSettings.json").  Passwords live in the same file for now; a
// production build should move them to the Keychain.
//
// CODABLE CONVENTION (important): these structs grow over time, and a
// synthesized decoder throws on any missing key — which would silently
// reset a user's whole settings file on upgrade.  Every struct here
// therefore decodes each field with `decodeIfPresent ?? its default`
// via the `value(_:default:)` helper.  When adding a field, ALWAYS
// mirror it in init(from:).

#if os(macOS)

import EudoraKit
import Foundation
import SwiftUI

// Tolerant decode: a missing key, explicit null, or wrong type falls back
// to the default instead of failing the whole settings file.  (try? of an
// Optional-returning call flattens, so one ?? covers both cases.)
private extension KeyedDecodingContainer {
    func value<T: Decodable>(_ key: Key, default def: T) -> T {
        (try? decodeIfPresent(T.self, forKey: key)) ?? def
    }
}

enum TransportSecurity: String, Codable, CaseIterable, Identifiable {
    case none
    case startTLS
    case immediateTLS

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .none: return "None"
        case .startTLS: return "STARTTLS"
        case .immediateTLS: return "SSL/TLS"
        }
    }
}

enum AccountType: String, Codable, CaseIterable, Identifiable {
    case pop3
    case imap

    var id: String { rawValue }
    var displayName: String { self == .pop3 ? "POP3" : "IMAP" }

    /// The conventional TLS port, used to auto-swap when switching type.
    var defaultPort: UInt16 { self == .pop3 ? 995 : 993 }
}

struct Personality: Codable, Identifiable, Equatable {
    var id: UUID = UUID()
    var name: String = "Dominant"
    var realName: String = ""
    var emailAddress: String = ""
    var username: String = ""
    var password: String = ""

    // The JSON keys keep their original "pop" names for compatibility;
    // for an IMAP personality they are simply the incoming server.
    var popHost: String = ""
    var popPort: UInt16 = 995
    var popSecurity: TransportSecurity = .immediateTLS
    var leaveOnServer: Bool = true

    var smtpHost: String = ""
    var smtpPort: UInt16 = 587
    var smtpSecurity: TransportSecurity = .startTLS

    var accountType: AccountType = .pop3
    var includeInChecks: Bool = true             // checked by Check Mail
    var leaveOnServerDays: Int = 0               // PREF_LMOS_XDAYS; 0 = forever
    var skipBigMessages: Bool = false            // PREF_NO_BIGGIES
    var bigMessageLimitK: Int = 40               // big-message limit, KB
    var serverDeleteOnTrashEmpty: Bool = false   // PREF_SERVER_DEL
    var useSignature: Bool = false               // PREF_SIG
    var signatureName: String = ""               // PREF_SIGFILE (file in the Signature Folder)

    init() {}

    enum CodingKeys: String, CodingKey {
        case id, name, realName, emailAddress, username, password
        case popHost, popPort, popSecurity, leaveOnServer
        case smtpHost, smtpPort, smtpSecurity
        case accountType, includeInChecks, leaveOnServerDays
        case skipBigMessages, bigMessageLimitK, serverDeleteOnTrashEmpty
        case useSignature, signatureName
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.value(.id, default: UUID())
        name = c.value(.name, default: "Dominant")
        realName = c.value(.realName, default: "")
        emailAddress = c.value(.emailAddress, default: "")
        username = c.value(.username, default: "")
        password = c.value(.password, default: "")
        popHost = c.value(.popHost, default: "")
        popPort = c.value(.popPort, default: 995)
        popSecurity = c.value(.popSecurity, default: .immediateTLS)
        leaveOnServer = c.value(.leaveOnServer, default: true)
        smtpHost = c.value(.smtpHost, default: "")
        smtpPort = c.value(.smtpPort, default: 587)
        smtpSecurity = c.value(.smtpSecurity, default: .startTLS)
        accountType = c.value(.accountType, default: .pop3)
        includeInChecks = c.value(.includeInChecks, default: true)
        leaveOnServerDays = c.value(.leaveOnServerDays, default: 0)
        skipBigMessages = c.value(.skipBigMessages, default: false)
        bigMessageLimitK = c.value(.bigMessageLimitK, default: 40)
        serverDeleteOnTrashEmpty = c.value(.serverDeleteOnTrashEmpty, default: false)
        useSignature = c.value(.useSignature, default: false)
        signatureName = c.value(.signatureName, default: "")
    }
}

/// One of the sixteen classic message labels: a name and a color.
struct LabelSetting: Codable, Equatable {
    var name: String = ""
    var r: Double = 0
    var g: Double = 0
    var b: Double = 0

    init(name: String, r: Double, g: Double, b: Double) {
        self.name = name
        self.r = r
        self.g = g
        self.b = b
    }

    enum CodingKeys: String, CodingKey { case name, r, g, b }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = c.value(.name, default: "")
        r = c.value(.r, default: 0)
        g = c.value(.g, default: 0)
        b = c.value(.b, default: 0)
    }
}

struct EudoraSettings: Codable, Equatable {
    var personalities: [Personality] = [Personality()]
    var dominantIndex: Int = 0

    // Checking Mail (global; per-account options live on Personality)
    var autoCheck: Bool = false                  // PREF_AUTO_CHECK
    var checkIntervalMinutes: Int = 15           // PREF_INTERVAL
    var sendOnCheck: Bool = true                 // PREF_SEND_CHECK

    // Replying
    var replyAllIncludesSelf: Bool = false       // PREF_NOT_ME, un-reversed
    var quotePrefix: String = ">"                // QUOTE_PREFIX
    var attributionTemplate: String = "At {time} {date}, {from} wrote:" // ATTRIBUTION

    // Getting Attention
    var newMailAlert: Bool = false               // PREF_NEW_ALERT
    var newMailSound: Bool = true                // PREF_NEW_SOUND
    var newMailSoundName: String = "Glass"       // PREF_NEWMAIL_SOUND
    var dockBadgeUnread: Bool = true             // the menu-bar flash, modernized
    var openMailboxOnNewMail: Bool = true        // PREF_NO_OPEN_IN, un-reversed

    // Junk Mail
    var junkThreshold: Int = 50                  // JUNK_MAILBOX_THRESHHOLD (1-100)
    var junkXferScore: Int = 100                 // JUNK_XFER_SCORE
    var junkEmptyAfterDays: Int = 30             // JUNK_MAILBOX_EMPTY_DAYS (0 = never)

    // Fonts & Display
    var displayFontSize: Int = 13                // PREF_FONT_SIZE (7-127)

    // Eudora Labels (16 entries; index 0 is "None")
    var labels: [LabelSetting] = EudoraSettings.classicLabels

    init() {}

    enum CodingKeys: String, CodingKey {
        case personalities, dominantIndex
        case autoCheck, checkIntervalMinutes, sendOnCheck
        case replyAllIncludesSelf, quotePrefix, attributionTemplate
        case newMailAlert, newMailSound, newMailSoundName
        case dockBadgeUnread, openMailboxOnNewMail
        case junkThreshold, junkXferScore, junkEmptyAfterDays
        case displayFontSize, labels
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        personalities = c.value(.personalities, default: [Personality()])
        if personalities.isEmpty { personalities = [Personality()] }
        dominantIndex = c.value(.dominantIndex, default: 0)
        autoCheck = c.value(.autoCheck, default: false)
        checkIntervalMinutes = c.value(.checkIntervalMinutes, default: 15)
        sendOnCheck = c.value(.sendOnCheck, default: true)
        replyAllIncludesSelf = c.value(.replyAllIncludesSelf, default: false)
        quotePrefix = c.value(.quotePrefix, default: ">")
        attributionTemplate = c.value(
            .attributionTemplate, default: "At {time} {date}, {from} wrote:")
        newMailAlert = c.value(.newMailAlert, default: false)
        newMailSound = c.value(.newMailSound, default: true)
        newMailSoundName = c.value(.newMailSoundName, default: "Glass")
        dockBadgeUnread = c.value(.dockBadgeUnread, default: true)
        openMailboxOnNewMail = c.value(.openMailboxOnNewMail, default: true)
        junkThreshold = c.value(.junkThreshold, default: 50)
        junkXferScore = c.value(.junkXferScore, default: 100)
        junkEmptyAfterDays = c.value(.junkEmptyAfterDays, default: 30)
        displayFontSize = c.value(.displayFontSize, default: 13)
        // A short or over-long stored label list is padded/trimmed to 16.
        var stored = c.value(.labels, default: EudoraSettings.classicLabels)
        if stored.count < EudoraSettings.classicLabels.count {
            stored += EudoraSettings.classicLabels[stored.count...]
        }
        labels = Array(stored.prefix(EudoraSettings.classicLabels.count))
    }

    var dominant: Personality {
        personalities.indices.contains(dominantIndex)
            ? personalities[dominantIndex] : Personality()
    }

    /// The classic label names and colors (System 7 labels 1-7 plus the
    /// PrivColors 8-15), as shipped in EudoraKit.
    static let classicLabels: [LabelSetting] =
        zip(MessageLabel.names, MessageLabel.colors).map {
            LabelSetting(name: $0.0, r: $0.1.r, g: $0.1.g, b: $0.1.b)
        }

    func labelName(_ index: Int) -> String {
        guard labels.indices.contains(index) else {
            return MessageLabel.names.indices.contains(index)
                ? MessageLabel.names[index] : ""
        }
        return labels[index].name
    }

    /// The label's color; nil for index 0 ("None") or out of range.
    func labelColor(_ index: Int) -> Color? {
        guard index > 0, labels.indices.contains(index) else { return nil }
        let l = labels[index]
        return Color(red: l.r, green: l.g, blue: l.b)
    }

    static func load(from url: URL) -> EudoraSettings {
        guard let data = try? Data(contentsOf: url),
              let settings = try? JSONDecoder().decode(EudoraSettings.self, from: data)
        else {
            return EudoraSettings()
        }
        return settings
    }

    func save(to url: URL) {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        if let data = try? encoder.encode(self) {
            try? data.write(to: url, options: .atomic)
        }
    }
}

#endif // os(macOS)
