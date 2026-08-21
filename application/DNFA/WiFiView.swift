//
//  WiFiView.swift
//  DNFA
//
//  Created by Hans Camacho on 8/20/26.
//

import SwiftUI



struct WiFiView: View {
    
    @StateObject var bleManager: BLEManager
    var body: some View {
        List {
            
            
            NavigationLink("Scan Networks") {
                List {
                    Button("Scan Wi-Fi (gonna take a couple secs once u press it)") {
                        bleManager.sendCommand("scan_wifi")
                    }
                    
                   
                    

                    ForEach(bleManager.wifi_scanResults) { network in
                        DisclosureGroup("\(network.ssid)") {
                            LazyVGrid(columns: [GridItem(.flexible()),GridItem(.flexible())]) {
                                
                                VStack {
                                    Text("DBM")
                                        .font(.caption.bold())
                                        .foregroundStyle(.secondary)
                                    Text("\(network.rssi)")
                                        .font(.title3)
                                        .bold()
                                }
                                VStack {
                                    Text("SECURITY")
                                        .font(.caption.bold())
                                        .foregroundStyle(.secondary)
                                    Text("\(network.sec)")
                                        .font(.title3)
                                        .bold()
                                }
                                
                            }
                            HStack {
                                Text("MAC")
                                Spacer()
                                Text("\(network.mac)")
                            }
                        }
                    }
                }
            }
            
            NavigationLink("Network Vulnerabilities") {
                List {
                    Button("Scan Wi-Fi (gonna take a couple secs once u press it)") {
                        bleManager.sendCommand("scan_wifi")
                    }
                    
                    
                    
                    
                    
                    
                    ForEach(bleManager.wifi_scanResults) { network in
                        
                        
                        WiFiNetworkVul(network: network)
                        
                    }
                }
                
                
                
            }
            
            
        }
        .navigationTitle("Wi-Fi")
    }
}

#Preview {
    @Previewable @StateObject var bleManager = BLEManager()
    NavigationStack {
        WiFiView(bleManager: bleManager)
    }
}
