//
//  ContentView.swift
//  DNFA
//
//  Created by hansthirtytwo
//


import SwiftUI

struct WifiNetwork: Codable, Identifiable, Hashable {
    var id: String { ssid }
    let ssid: String
    let rssi: Int
    let sec: String
    let channel: String   // was Int — firmware sends it quoted
    let mac: String
}

struct ScanResponse: Codable {
    let rows: [WifiNetwork]
}

struct ContentView: View {
    @StateObject private var bleManager = BLEManager()
    @State private var path = NavigationPath()
    
    

    var body: some View {
        if bleManager.isConnected || ProcessInfo.isInsideXcodeCanvas {
            NavigationStack(path: $path) {
                List {
                    NavigationLink("Wi-Fi") {
                        WiFiView(bleManager: bleManager)
                    }
                }
                .navigationTitle("DNFA")
            }
        } else {
            List {
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
}

#Preview {
    ContentView()
}
