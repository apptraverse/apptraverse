import XCTest

final class SharedChatUi: XCTestCase {
    let app = XCUIApplication(bundleIdentifier: "org.apptraverse.SharedNodeChat.ios")
    func capture(_ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name; attachment.lifetime = .keepAlways; add(attachment)
    }
    func visible(_ text: String, timeout: TimeInterval = 90) {
        XCTAssertTrue(app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", text)).firstMatch.waitForExistence(timeout: timeout), text)
    }
    func send(_ text: String) {
        let field = app.descendants(matching: .any).matching(identifier: "chat.input").firstMatch
        XCTAssertTrue(field.waitForExistence(timeout: 15))
        let enabled = XCTNSPredicateExpectation(predicate: NSPredicate(format: "enabled == true"), object: field)
        XCTAssertEqual(XCTWaiter.wait(for: [enabled], timeout: 180), .completed)
        field.tap(); field.typeText(text)
        app.buttons["chat.send"].tap()
    }
    func testJoinDeliveryBackgroundAndColdRestore() throws {
        continueAfterFailure = false
        executionTimeAllowance = 600
        let env = ProcessInfo.processInfo.environment
        let host = try XCTUnwrap(env["CHAT_HOST_UID"])
        let profile = try XCTUnwrap(env["CHAT_PROFILE"])
        let token = try XCTUnwrap(env["CHAT_TOKEN"])
        app.launchArguments = ["--client", "--profile", profile, "--name", "iPhoneTest"]
        app.launch()
        let invite = app.textFields["chat.invite"]
        XCTAssertTrue(invite.waitForExistence(timeout: 20))
        invite.tap(); invite.typeText(host)
        let join = app.buttons["chat.join"]
        XCTAssertEqual(XCTWaiter.wait(for: [XCTNSPredicateExpectation(predicate: NSPredicate(format: "enabled == true"), object: join)], timeout: 180), .completed)
        join.tap()
        send(token + " phone-first")
        visible(token + " Mac reply: Привет 🌍 你好")
        capture("iphone-unicode-delivery")
        app.buttons["chat.participants"].tap()
        visible("MacHost", timeout: 15); visible("iPhoneTest", timeout: 15)
        app.buttons["Done"].tap()
        XCUIDevice.shared.orientation = .landscapeLeft
        capture("iphone-landscape")
        XCUIDevice.shared.orientation = .portrait
        send(token + " before-background")
        XCUIDevice.shared.press(.home)
        XCTAssertTrue(app.wait(for: .runningBackground, timeout: 15))
        // Gives the host time to send while the iOS scene is inactive.
        let foregroundDelay = expectation(description: "background interval")
        DispatchQueue.main.asyncAfter(deadline: .now() + 6) { foregroundDelay.fulfill() }
        wait(for: [foregroundDelay], timeout: 10)
        app.activate()
        visible(token + " sent while phone backgrounded")
        visible(token + " phone-first", timeout: 10)
        send(token + " phone-after-background")
        visible(token + " Mac confirms background")
        capture("iphone-after-background")
        app.terminate()
        app.launch()
        visible(token + " phone-first")
        visible(token + " sent while phone backgrounded")
        XCTAssertFalse(app.buttons["chat.join"].isEnabled)
        send(token + " phone-after-restart")
        visible(token + " Mac confirms restart")
        capture("iphone-cold-restore")
        app.terminate()
    }
}
