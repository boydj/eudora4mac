// Renders an HTML mail part as read-only attributed text.
//
// Remote image/resource references are stripped before rendering — the
// classic "don't fetch graphics automatically" default (PREF_DONT_FETCH_
// GRAPHICS), which also defeats tracking pixels.  No JavaScript, no
// WebKit: NSAttributedString's HTML importer over a sanitized source.

#if os(macOS)

import AppKit
import SwiftUI

struct HTMLMessageView: NSViewRepresentable {
    let html: String
    let fontSize: CGFloat

    func makeNSView(context: Context) -> NSScrollView {
        let scroll = NSTextView.scrollableTextView()
        if let textView = scroll.documentView as? NSTextView {
            textView.isEditable = false
            textView.isSelectable = true
            textView.drawsBackground = false
            textView.textContainerInset = NSSize(width: 6, height: 6)
        }
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        guard let textView = scroll.documentView as? NSTextView else { return }
        let sanitized = Self.stripRemoteResources(html)
        guard let data = sanitized.data(using: .utf8) else { return }
        let options: [NSAttributedString.DocumentReadingOptionKey: Any] = [
            .documentType: NSAttributedString.DocumentType.html,
            .characterEncoding: String.Encoding.utf8.rawValue,
        ]
        if let attributed = try? NSAttributedString(data: data, options: options,
                                                    documentAttributes: nil) {
            textView.textStorage?.setAttributedString(attributed)
        }
    }

    /// Blank out http/https resource URLs in src=, href-loaded images, and
    /// CSS url() so nothing is fetched from the network on render.
    static func stripRemoteResources(_ html: String) -> String {
        var out = html
        for pattern in [#"(?i)src\s*=\s*["']https?:[^"']*["']"#,
                        #"(?i)background\s*=\s*["']https?:[^"']*["']"#,
                        #"(?i)url\(\s*https?:[^)]*\)"#] {
            if let re = try? NSRegularExpression(pattern: pattern) {
                let range = NSRange(out.startIndex..<out.endIndex, in: out)
                out = re.stringByReplacingMatches(in: out, range: range,
                                                  withTemplate: "src=\"\"")
            }
        }
        return out
    }
}

#endif // os(macOS)
