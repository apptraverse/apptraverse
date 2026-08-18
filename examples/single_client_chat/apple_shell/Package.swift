// swift-tools-version: 5.9

import PackageDescription

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
        .executable(
            name: "AppTraverseChatMacDemo",
            targets: ["AppTraverseChatMacDemo"]
        ),
    ],
    targets: [
        .target(
            name: "AppTraverseChatAppleUI"
        ),
        .executableTarget(
            name: "AppTraverseChatMacDemo",
            dependencies: ["AppTraverseChatAppleUI"]
        ),
    ]
)
