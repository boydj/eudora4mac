// Personalities — the modern form of Eudora's account settings.
//
// The legacy app kept these in the resource-fork "Eudora Settings" file;
// the modern app stores them as JSON in the mail folder
// ("EudoraSettings.json").  Passwords live in the same file for now; a
// production build should move them to the Keychain.

#if os(macOS)

import Foundation

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

struct Personality: Codable, Identifiable, Equatable {
    var id: UUID = UUID()
    var name: String = "Dominant"
    var realName: String = ""
    var emailAddress: String = ""
    var username: String = ""
    var password: String = ""

    var popHost: String = ""
    var popPort: UInt16 = 995
    var popSecurity: TransportSecurity = .immediateTLS
    var leaveOnServer: Bool = true

    var smtpHost: String = ""
    var smtpPort: UInt16 = 587
    var smtpSecurity: TransportSecurity = .startTLS
}

struct EudoraSettings: Codable, Equatable {
    var personalities: [Personality] = [Personality()]
    var dominantIndex: Int = 0

    var dominant: Personality {
        personalities.indices.contains(dominantIndex)
            ? personalities[dominantIndex] : Personality()
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
