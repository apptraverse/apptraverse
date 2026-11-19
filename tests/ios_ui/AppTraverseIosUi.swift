import ImageIO
import XCTest

final class AppTraverseIosUi: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
        XCUIDevice.shared.orientation = .portrait
    }

    private func assertPage(_ title: String, in app: XCUIApplication) {
        XCTAssertTrue(app.navigationBars[title].waitForExistence(timeout: 20))
    }

    private func row(_ label: String, in app: XCUIApplication) -> XCUIElement {
        app.descendants(matching: .any).matching(NSPredicate(
            format: "identifier BEGINSWITH %@ AND label CONTAINS %@", "row-", label)).firstMatch
    }

    private func brightness(of element: XCUIElement) -> Double? {
        let png = element.screenshot().pngRepresentation
        guard let source = CGImageSourceCreateWithData(png as CFData, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil),
              let sample = image.cropping(to: CGRect(x: image.width / 8, y: image.height / 2,
                                                    width: 1, height: 1)) else { return nil }
        var pixel = [UInt8](repeating: 0, count: 4)
        return pixel.withUnsafeMutableBytes { bytes in
            guard let context = CGContext(data: bytes.baseAddress, width: 1, height: 1,
                                          bitsPerComponent: 8, bytesPerRow: 4,
                                          space: CGColorSpaceCreateDeviceRGB(),
                                          bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue) else { return nil }
            context.draw(sample, in: CGRect(x: 0, y: 0, width: 1, height: 1))
            return (0..<3).map { Double(bytes[$0]) }.reduce(0, +) / 765
        }
    }

    func testSettingsThemeAppliesWhileOpen() throws {
        let app = XCUIApplication(bundleIdentifier: "org.apptraverse.WindowCatalog.ios")
        app.launch()
        assertPage("Window Catalog", in: app)
        app.buttons["catalog-settings"].tap()
        assertPage("Settings", in: app)

        let bar = app.navigationBars["Settings"]
        let systemIsDark = try XCTUnwrap(brightness(of: bar)) < 0.5
        for (theme, isDark) in [("Light", false), ("Dark", true), ("From system", systemIsDark)] {
            app.buttons[theme].tap()
            let applied = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
                guard bar.exists, let brightness = self.brightness(of: bar) else { return false }
                return isDark ? brightness < 0.3 : brightness > 0.7
            }, object: nil)
            XCTAssertEqual(XCTWaiter.wait(for: [applied], timeout: 8), .completed, theme)
        }
    }

    func testWindowCatalogPagesAndRotation() {
        let app = XCUIApplication(bundleIdentifier: "org.apptraverse.WindowCatalog.ios")
        app.launch()
        assertPage("Window Catalog", in: app)
        let settings = app.buttons["catalog-settings"]
        settings.tap()
        assertPage("Settings", in: app)
        XCTAssertTrue(app.staticTexts["Theme"].exists)
        XCTAssertTrue(app.staticTexts["Language"].exists)
        app.buttons["settings-done"].tap()
        assertPage("Window Catalog", in: app)
        let add = app.buttons["Add"]
        XCTAssertTrue(add.isHittable)
        let portraitFrame = add.frame
        XCUIDevice.shared.orientation = .landscapeLeft
        let moved = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            app.frame.width > app.frame.height && add.frame.minX > portraitFrame.minX + 80
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [moved], timeout: 20), .completed)
        XCUIDevice.shared.orientation = .portrait

        add.tap()
        assertPage("Window 1", in: app)
        XCTAssertFalse(settings.exists)
        app.buttons["mobile-page-0"].tap()
        assertPage("Window Catalog", in: app)
        add.tap()
        assertPage("Window 2", in: app)

        app.swipeLeft()
        assertPage("Window Catalog", in: app)
        app.swipeLeft()
        assertPage("Window 1", in: app)
        app.swipeLeft()
        assertPage("Window 2", in: app)
        app.swipeRight()
        assertPage("Window 1", in: app)
        app.swipeUp()
        assertPage("Window Catalog", in: app)
        app.swipeRight()
        assertPage("Window 2", in: app)
        app.swipeUp()
        assertPage("Window Catalog", in: app)

        let firstRow = row("Window 1", in: app)
        XCTAssertTrue(firstRow.waitForExistence(timeout: 10))
        firstRow.tap()
        let selected = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            firstRow.isSelected
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [selected], timeout: 10), .completed)
        firstRow.doubleTap()
        assertPage("Window 1", in: app)

        app.buttons["Close"].tap()
        app.buttons["mobile-page-0"].tap()
        assertPage("Window Catalog", in: app)
        XCTAssertTrue(row("Closed", in: app).exists)
        app.buttons["Open"].tap()
        assertPage("Window 1", in: app)
        app.buttons["mobile-page-0"].tap()
        app.buttons["Delete"].tap()
        let deleted = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            !firstRow.exists
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [deleted], timeout: 10), .completed)
    }

    func testMinimalAppControlsAndDetail() {
        let app = XCUIApplication(bundleIdentifier: "com.apptraverse.minimalapp.ios")
        app.launch()
        assertPage("Minimal App", in: app)

        let rowB = row("Row B", in: app)
        XCTAssertTrue(rowB.waitForExistence(timeout: 10))
        rowB.doubleTap()
        assertPage("Activated Row B", in: app)
        app.buttons["Apply"].tap()
        assertPage("Applied", in: app)
        XCTAssertFalse(app.buttons["Apply"].isEnabled)

        app.buttons["Open Detail"].tap()
        assertPage("Detail Panel", in: app)
        XCTAssertTrue(app.buttons["Apply"].isEnabled)
        XCTAssertTrue(app.buttons["Close Detail"].isHittable)
        app.buttons["Close Detail"].tap()
        assertPage("Applied", in: app)
        XCTAssertFalse(app.buttons["Apply"].isEnabled)
    }
}
