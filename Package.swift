// swift-tools-version:5.9
//
// SwiftPM manifest for EudoraCore: lets a SwiftUI app depend on this
// repository directly.  The C++ core builds as the CEudoraCore target
// (exposing only the C interface, per core/include/module.modulemap) and
// EudoraKit layers idiomatic Swift types over it.
//
// The TLS transport needs OpenSSL and is disabled in the SwiftPM build
// (net/tls_transport.cpp compiles empty without EUDORA_HAVE_TLS); use the
// CMake build with OpenSSL for TLS, or add a define + linker settings here.

import PackageDescription

let package = Package(
    name: "EudoraCore",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .library(name: "EudoraKit", targets: ["EudoraKit"]),
        .library(name: "CEudoraCore", targets: ["CEudoraCore"]),
    ],
    targets: [
        .target(
            name: "CEudoraCore",
            path: "core",
            exclude: [
                "tests",
                "CMakeLists.txt",
            ],
            sources: [
                "compat",
                "mailstore",
                "mail",
                "net",
                "protocols",
                "filters",
                "addressbook",
                "api",
            ],
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("."),
                .headerSearchPath("include"),
            ]
        ),
        .target(
            name: "EudoraKit",
            dependencies: ["CEudoraCore"],
            path: "swift/EudoraKit"
        ),
    ],
    cxxLanguageStandard: .cxx20
)
