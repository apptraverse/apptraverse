// swift-tools-version: 5.9

import PackageDescription

// UI-only Swift sources. This package is not the product application.
// The real macOS/iOS apps are built by CMake/xcodebuild and always use
// AppleChatBackend → Objective-C++ → AppleChatRuntime.

let package = Package(
    name: "AppTraverseChatAppleShell",
    platforms: [
        .macOS(.v13),
        .iOS(.v16),
    ],
    products: [
        .library(
            name: "AppTraverseChatAppleUI",
            targets: ["AppTraverseChatAppleUI"]
        ),
    ],
    targets: [
        .target(
            name: "AppTraverseChatAppleUI",
            exclude: ["AppleChatBackend.swift"]
        ),
    ]
)
