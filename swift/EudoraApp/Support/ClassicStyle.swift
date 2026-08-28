// Classic Eudora presentation rules: the status column glyphs, priority
// arrows, label colors, and the mailbox window's date formatting.

#if os(macOS)

import EudoraKit
import Foundation
import SwiftUI

enum ClassicStyle {
    /// The status column: the classic single-character codes drawn in the
    /// leftmost mailbox column (boxact.c's status LDEF).
    static func statusGlyph(for state: MessageState) -> String {
        switch state {
        case .unread: return "•"
        case .read: return ""
        case .replied: return "R"
        case .redistributed: return "D"
        case .forwarded: return "F"
        case .sendable: return "·"
        case .unsendable, .unsent: return "-"
        case .queued: return "Q"
        case .sent: return "S"
        case .timed: return "T"
        case .rebuilt: return "?"
        case .other: return ""
        }
    }

    /// Priority column: 1-2 raise, 4-5 lower, 3 (normal) shows nothing —
    /// the classic double/single chevrons.
    static func priorityGlyph(_ display: Int) -> String {
        switch display {
        case 1: return "»"
        case 2: return "›"
        case 4: return "‹"
        case 5: return "«"
        default: return ""
        }
    }

    /// Classic summary dates: time for today, weekday for this week, short
    /// date otherwise (LOCAL_DATE_FMT / OLD_LOCAL_DATE_FMT behavior).
    static func summaryDate(_ date: Date) -> String {
        let cal = Calendar.current
        let formatter = DateFormatter()
        if cal.isDateInToday(date) {
            formatter.timeStyle = .short
            formatter.dateStyle = .none
            return formatter.string(from: date)
        }
        if let days = cal.dateComponents([.day], from: date, to: Date()).day,
           days < 7, days >= 0 {
            formatter.dateFormat = "EEE"
            let time = DateFormatter()
            time.timeStyle = .short
            time.dateStyle = .none
            return "\(time.string(from: date)) \(formatter.string(from: date))"
        }
        formatter.dateStyle = .short
        formatter.timeStyle = .none
        return formatter.string(from: date)
    }

    static func sizeK(_ bytes: Int) -> String {
        "\(max(1, (bytes + 1023) / 1024))K"
    }
}

#endif // os(macOS)
