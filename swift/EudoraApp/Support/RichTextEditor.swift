// The composition body editor, backed by an NSTextView so it gets the
// system spelling/grammar checker and, in styled mode, real rich-text
// formatting (bold / italic / underline / fonts / color) the classic
// Eudora "styled text" composer offered.  A bare SwiftUI TextEditor
// exposes neither the attributed content nor the checker controls.

#if os(macOS)

import AppKit
import SwiftUI

/// Bridges the SwiftUI formatting toolbar to the live NSTextView; the editor
/// registers the view here and the toolbar buttons drive the selection.
final class RichTextController: ObservableObject {
    weak var textView: NSTextView?

    func toggleUnderline() { textView?.underline(nil) }
    func showFonts() { NSFontManager.shared.orderFrontFontPanel(nil) }
    func toggleBold() { applyTrait(.boldFontMask) }
    func toggleItalic() { applyTrait(.italicFontMask) }

    /// Adds `trait` to the selection's font(s), or removes it when every run
    /// already has it (the Bold/Italic toggle in the classic composer).
    private func applyTrait(_ trait: NSFontTraitMask) {
        guard let tv = textView, let storage = tv.textStorage else { return }
        let fm = NSFontManager.shared
        let ranges = tv.selectedRanges.map(\.rangeValue).filter { $0.length > 0 }

        // If every run in the selection already carries the trait, toggle off.
        var allHaveTrait = !ranges.isEmpty
        for range in ranges {
            storage.enumerateAttribute(.font, in: range) { value, _, stop in
                let font = (value as? NSFont) ?? tv.font ?? .systemFont(ofSize: 12)
                if !fm.traits(of: font).contains(trait) {
                    allHaveTrait = false
                    stop.pointee = true
                }
            }
            if !allHaveTrait { break }
        }

        let convert: (NSFont) -> NSFont = { font in
            allHaveTrait ? fm.convert(font, toNotHaveTrait: trait)
                         : fm.convert(font, toHaveTrait: trait)
        }

        if ranges.isEmpty {
            // No selection: change the typing attributes for what's typed next.
            let base = (tv.typingAttributes[.font] as? NSFont)
                ?? tv.font ?? .systemFont(ofSize: 12)
            tv.typingAttributes[.font] = convert(base)
            return
        }

        storage.beginEditing()
        for range in ranges {
            storage.enumerateAttribute(.font, in: range) { value, sub, _ in
                let font = (value as? NSFont) ?? tv.font ?? .systemFont(ofSize: 12)
                storage.addAttribute(.font, value: convert(font), range: sub)
            }
        }
        storage.endEditing()
        tv.didChangeText() // fire the delegate so the binding updates
    }
}

struct RichTextEditor: NSViewRepresentable {
    @Binding var attributed: NSAttributedString
    /// When false the toolbar is hidden and the view rejects styling, so the
    /// body is effectively plain text (Eudora's default send format).
    var isRichText: Bool
    /// Continuous spell + grammar checking, as the classic composer had.
    var checkSpelling: Bool = true
    /// Optional controller the formatting toolbar drives.
    var controller: RichTextController? = nil

    func makeNSView(context: Context) -> NSScrollView {
        let scroll = NSTextView.scrollableTextView()
        guard let textView = scroll.documentView as? NSTextView else { return scroll }
        textView.delegate = context.coordinator
        textView.allowsUndo = true
        textView.isRichText = isRichText
        textView.isContinuousSpellCheckingEnabled = checkSpelling
        textView.isGrammarCheckingEnabled = checkSpelling
        // Email bodies should keep the characters the user typed; the smart
        // substitutions belong to prose editors, not wire text.
        textView.isAutomaticQuoteSubstitutionEnabled = false
        textView.isAutomaticDashSubstitutionEnabled = false
        textView.isAutomaticTextReplacementEnabled = false
        textView.font = .userFixedPitchFont(ofSize: 12)
        textView.textContainerInset = NSSize(width: 4, height: 6)
        textView.textStorage?.setAttributedString(attributed)
        controller?.textView = textView
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        guard let textView = scroll.documentView as? NSTextView else { return }
        textView.isRichText = isRichText
        textView.isContinuousSpellCheckingEnabled = checkSpelling
        textView.isGrammarCheckingEnabled = checkSpelling
        controller?.textView = textView
        // Only overwrite when the model changed out from under the view (a
        // programmatic edit), never while the user is the one typing.
        if !context.coordinator.isEditing,
           !(textView.textStorage?.isEqual(attributed) ?? true) {
            let selected = textView.selectedRange()
            textView.textStorage?.setAttributedString(attributed)
            textView.setSelectedRange(
                NSRange(location: min(selected.location,
                                      textView.string.utf16.count), length: 0))
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    final class Coordinator: NSObject, NSTextViewDelegate {
        private let parent: RichTextEditor
        var isEditing = false

        init(_ parent: RichTextEditor) { self.parent = parent }

        func textDidChange(_ notification: Notification) {
            guard let textView = notification.object as? NSTextView,
                  let storage = textView.textStorage else { return }
            isEditing = true
            parent.attributed = NSAttributedString(attributedString: storage)
            isEditing = false
        }
    }
}

extension NSAttributedString {
    /// A `text/html` rendering of the styled body for the composer's
    /// alternative part; nil if the conversion fails.
    var htmlBody: String? {
        let range = NSRange(location: 0, length: length)
        let attrs: [NSAttributedString.DocumentAttributeKey: Any] = [
            .documentType: NSAttributedString.DocumentType.html,
            .characterEncoding: String.Encoding.utf8.rawValue,
        ]
        guard let data = try? data(from: range, documentAttributes: attrs) else {
            return nil
        }
        return String(data: data, encoding: .utf8)
    }

    /// Whether the body carries any styling worth an HTML alternative (a bold,
    /// italic, or underlined run).
    var hasStyling: Bool {
        var styled = false
        let whole = NSRange(location: 0, length: length)
        enumerateAttribute(.font, in: whole) { value, _, stop in
            if let font = value as? NSFont {
                let traits = font.fontDescriptor.symbolicTraits
                if traits.contains(.bold) || traits.contains(.italic) {
                    styled = true
                    stop.pointee = true
                }
            }
        }
        if styled { return true }
        enumerateAttribute(.underlineStyle, in: whole) { value, _, stop in
            if let n = value as? Int, n != 0 { styled = true; stop.pointee = true }
        }
        return styled
    }
}

#endif // os(macOS)
