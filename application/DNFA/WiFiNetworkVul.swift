//
//  NetworkVulnerabilityRow.swift
//  DNFA
//
//  Created by hansthirtytwo
//

import SwiftUI

struct SecInfo {
    let title: String
    let score: String
    let desc: String
    let img: String
    let bgCol: Color
    
    init(security: String) {
        switch security.uppercased() {
        case "OPEN":
            self.title = "Unsecured"
            self.score = "0/10 Security"
            self.desc = "Lacks any encryption/authentication, leaving all traffic exposed to packet sniffing, Evil Twin rogue access points, and deauthentication frame spam."
            self.img = "icon-lock2"
            self.bgCol = .red
            
        case "WEP":
            self.title = "Broken"
            self.score = "1/10 Security"
            self.desc = "Employs severely flawed RC4 encryption with short Initialisation Vectors. Vulnerable to ARP injection and deauth flooding, allowing network keys to be cracked quickly."
            self.img = "icon-lock2"
            self.bgCol = .red
            
        case "WPA":
            self.title = "Highly Vulnerable"
            self.score = "2/10 Security"
            self.desc = "Uses outdated TKIP encryption and weak integrity checks. Susceptible to packet decryption/injection attacks, deauthorization frame spamming, and 4-way handshake dictionary cracking."
            self.img = "icon-lock2"
            self.bgCol = .red
            
        case "WPA/WPA2":
            self.title = "Legacy / Weak"
            self.score = "3/10 Security"
            self.desc = "Mixed mode for legacy compatibility that falls back to TKIP ciphers. Attackers can actively downgrade client connections and execute deauth attacks to capture handshakes."
            self.img = "icon-lock1"
            self.bgCol = .orange
            
        case "WPA2":
            self.title = "Vulnerable"
            self.score = "5/10 Security"
            self.desc = "Uses strong AES-CCMP encryption, but lacks mandatory Management Frame Protection (PMF). Susceptible to MAC spoofing, deauth disconnect attacks, KRACK vulnerabilities, and offline dictionary cracking."
            self.img = "icon-lock1"
            self.bgCol = .orange
            
        case "WPA2/WPA3":
            self.title = "Moderately Secure"
            self.score = "6/10 Security"
            self.desc = "Transition mode allowing WPA3 clients to connect safely while maintaining legacy WPA2 access. Remains partially vulnerable to deauthentication attacks targeting WPA2."
            self.img = "icon-lock1"
            self.bgCol = .orange
            
        case "WPA2-ENT":
            self.title = "Secure"
            self.score = "7/10 Security"
            self.desc = "Uses 802.1X/EAP server authentication with dynamic keys per user. Still susceptible to deauth spam if PMF (802.11w) isn't explicitly enforced."
            self.img = "icon-lock1"
            self.bgCol = .orange
            
        case "WAPI":
            self.title = "Niche / Variable"
            self.score = "5/10 Security"
            self.desc = "Chinese national encryption standard using elliptic curve cryptography (SMS4/SM4). Resistant to standard WPA dictionary attacks, but relies on proprietary hardware implementations."
            self.img = "icon-lock1"
            self.bgCol = .orange
            
        case "WPA3":
            self.title = "Highly Secure"
            self.score = "9/10 Security"
            self.desc = "Features SAE authentication and mandatory Protected Management Frames (PMF). Blocks deauth attacks, prevents offline dictionary cracking, and ensures forward secrecy."
            self.img = "icon-lock0"
            self.bgCol = .green
            
        default:
            self.title = "Undetermined"
            self.score = "0/10 Security"
            self.desc = "Unrecognized or unsupported Wi-Fi authentication standard. Security parameters, encryption strength, and deauth resistance cannot be guaranteed."
            self.img = "icon-lock2"
            self.bgCol = .gray
        }
    }
}

struct WiFiNetworkVul: View {
    let network: WifiNetwork
    
    var body: some View {
        let info = SecInfo(security: network.sec)
        
        DisclosureGroup {
            HStack {
                Spacer()
                Image(info.img)
                    .resizable()
                    .frame(width: 70, height: 70)
                VStack(alignment: .leading) {
                    Text(info.title)
                        .font(.title.bold())
                    Text("\(info.score) • \(network.sec)")
                }
                Spacer()
            }
            
            Text(info.desc)
                .padding(.top, 4)
        } label: {
            Text(network.ssid)
        }
        .listRowBackground(info.bgCol)
        .foregroundColor(.white)
    }
}
