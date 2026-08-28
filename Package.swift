// swift-tools-version:5.9
//
// SwiftPM manifest for EudoraCore: lets a SwiftUI app depend on this
// repository directly.  The C++ core builds as the CEudoraCore target
// (exposing only the C interface, per core/include/module.modulemap) and
// EudoraKit layers idiomatic Swift types over it.
//
// TLS: on macOS the core uses the Security framework (SecureTransport
// decorator, net/apple_tls_transport.*) with no external dependency; the
// OpenSSL decorator (net/tls_transport.cpp) is used by CMake builds that
// find OpenSSL and compiles empty here.

import PackageDescription

let package = Package(
    name: "EudoraCore",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .library(name: "EudoraKit", targets: ["EudoraKit"]),
        .library(name: "CEudoraCore", targets: ["CEudoraCore"]),
        .executable(name: "EudoraApp", targets: ["EudoraApp"]),
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
            ],
            linkerSettings: [
                // TLS via the Security framework (SecureTransport decorator)
                // on Apple platforms — no OpenSSL needed.
                .linkedFramework("Security", .when(platforms: [.macOS])),
            ]
        ),
        .target(
            name: "EudoraKit",
            dependencies: ["CEudoraCore"],
            path: "swift/EudoraKit"
        ),
        .executableTarget(
            name: "EudoraApp",
            dependencies: ["EudoraKit"],
            path: "swift/EudoraApp"
        ),
    ],
    cxxLanguageStandard: .cxx20
)
