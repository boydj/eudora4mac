// Seeds for the composition window.  Reply / Reply All / Forward /
// Redirect / Send Again all open a composition pre-filled from an existing
// message; the seed travels through openWindow(value:), so each action
// gets a fresh window with fresh @State.

#if os(macOS)

import Foundation

struct ComposeSeed: Hashable, Codable, Identifiable {
    // Distinct per action, so two identical seeds still open two windows.
    var id = UUID()

    var to = ""
    var cc = ""
    var bcc = ""
    var subject = ""
    var body = ""
    var priority = 3

    /// Redirect's "orig (by way of me)" From line; nil uses the account.
    var fromName: String? = nil
    var fromAddress: String? = nil

    /// Extra RFC 822 headers (e.g. In-Reply-To).
    var extraHeaders: [ExtraHeader] = []

    /// The message this composition answers; marked when queued or sent.
    var original: OriginalRef? = nil

    struct ExtraHeader: Hashable, Codable {
        var name: String
        var value: String
    }

    struct OriginalRef: Hashable, Codable {
        var mailbox: String
        var serial: Int
        var markState: UInt8 // replied / forwarded / redistributed
    }
}

/// A stored message, for opening in its own window.  The serial number
/// keeps the reference valid when transfers shift indices.
struct MessageRef: Hashable, Codable {
    var mailbox: String
    var index: Int
    var serial: Int
}

#endif // os(macOS)
