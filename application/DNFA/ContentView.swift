//
//  ContentView.swift
//  DNFA
//
//  Created by hansthirtytwo
//

import SwiftUI

struct ContentView: View {
    @StateObject private var bleManager = BLEManager()

    var body: some View {
        List {
            if bleManager.isConnected {
                Text("Device connected")
                Button("Test") {
                    bleManager.sendCommand("test")
                }

                if bleManager.lastMessage != "Connected" {
                    Text(bleManager.lastMessage)
                }
            } else {
                Text("No devices connected")

                if !bleManager.lastMessage.isEmpty {
                    Text(bleManager.lastMessage)
                }
            }

            Text("BT state: \(bleManager.bluetoothState.description)")

            if bleManager.isScanning {
                Button("Stop Scanning") {
                    bleManager.stopScanning()
                }
            } else {
                Button("Start Scanning") {
                    bleManager.startScanning()
                }
            }

            ForEach(bleManager.foundDevices) { device in
                Button("\(device.name): Connect") {
                    bleManager.connect(to: device.identifier)
                }
            }
        }
    }
}

#Preview {
    ContentView()
}
