import Foundation
import SwiftUI

#if os(macOS)
import AppTraverseMacBridge
#elseif os(iOS)
import AppTraverseIosBridge
#endif

struct AppleControlID: LayoutValueKey {
    static let defaultValue: UInt32 = 0
}

struct AppleConstraintLayout: Layout {
    let definition: ATAppleWindowLayout
    let viewport: CGSize

    func makeCache(subviews: Subviews) -> ATAppleLayoutResult? { nil }

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews,
                      cache: inout ATAppleLayoutResult?) -> CGSize {
        let result = solve(subviews)
        cache = result
        return result.contentSize
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize,
                       subviews: Subviews, cache: inout ATAppleLayoutResult?) {
        let result = cache ?? solve(subviews)
        for view in subviews {
            let frame = result.frame(forControl: view[AppleControlID.self])
            view.place(at: CGPoint(x: bounds.minX + frame.minX, y: bounds.minY + frame.minY),
                       anchor: .topLeading,
                       proposal: ProposedViewSize(width: frame.width, height: frame.height))
        }
    }

    private func solve(_ subviews: Subviews) -> ATAppleLayoutResult {
        let controls = subviews.map { (id: $0[AppleControlID.self], view: $0) }
        return definition.solveViewport(viewport, controlIDs: controls.map { NSNumber(value: $0.id) }) {
            id, width in
            let view = controls.first { $0.id == id }!.view
            let proposal = width < 0 ? ProposedViewSize.unspecified :
                ProposedViewSize(width: width, height: nil)
            return view.sizeThatFits(proposal)
        }
    }
}
