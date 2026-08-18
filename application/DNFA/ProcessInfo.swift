//
//  ProcessInfo.swift
//  DnFA
//
//  Created by hansthirtytwo
//

import Foundation

extension ProcessInfo {
    static var isInsideXcodeCanvas: Bool {
        return processInfo.environment["XCODE_RUNNING_FOR_PREVIEWS"] == "1"
    }
}
