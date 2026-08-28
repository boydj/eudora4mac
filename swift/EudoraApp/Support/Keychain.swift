// Account passwords belong in the macOS Keychain, not the settings JSON.
// A thin wrapper over the SecItem generic-password API keyed by a stable
// per-account identifier (the personality's UUID).

#if os(macOS)

import Foundation
import Security

enum Keychain {
    /// One service groups all of the app's account passwords.
    static let service = "com.eudora.mac.accounts"

    /// Stores (or updates) the password for an account key.  An empty
    /// password deletes the item so a cleared field leaves nothing behind.
    static func set(_ password: String, for account: String) {
        guard !password.isEmpty else { remove(account); return }
        guard let data = password.data(using: .utf8) else { return }
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        let attrs: [String: Any] = [
            kSecValueData as String: data,
            // Available after first unlock, this device only — not synced to
            // iCloud, not exportable in a backup.
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
        ]
        let status = SecItemUpdate(query as CFDictionary, attrs as CFDictionary)
        if status == errSecItemNotFound {
            var add = query
            add.merge(attrs) { _, new in new }
            SecItemAdd(add as CFDictionary, nil)
        }
    }

    /// The password for an account key, or "" if none is stored.
    static func get(_ account: String) -> String {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var item: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess,
              let data = item as? Data,
              let password = String(data: data, encoding: .utf8)
        else { return "" }
        return password
    }

    static func remove(_ account: String) {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        SecItemDelete(query as CFDictionary)
    }
}

#endif // os(macOS)
