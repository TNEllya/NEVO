// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "NevoClientLibs",
    platforms: [
        .iOS(.v16)
    ],
    products: [
        .library(name: "NevoClientLibs", targets: ["NevoClientLibs"]),
    ],
    dependencies: [
        .package(url: "https://github.com/jedisct1/swift-sodium.git", from: "0.9.0"),
        .package(url: "https://github.com/krzyzanowskim/OpenSSL.git", from: "3.2.0"),
    ],
    targets: [
        .target(
            name: "NevoClientLibs",
            dependencies: [
                .product(name: "Sodium", package: "swift-sodium"),
                .product(name: "OpenSSL", package: "OpenSSL"),
            ],
            path: "Sources"
        ),
        .systemLibrary(
            name: "Copus",
            path: "Copus",
            pkgConfig: "opus",
            providers: [
                .apt(["libopus-dev"]),
                .brew(["opus"])
            ]
        ),
    ]
)