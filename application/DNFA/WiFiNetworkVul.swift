//
//  NetworkVulnerabilityRow.swift
//  DNFA
//
//  Created by Hans Camacho on 8/20/26.
//


struct NetworkVulnerabilityRow: View {
    let network: WifiNetwork
    
    var body: some View {
        let info = SecInfo(security: network.sec)
        
        DisclosureGroup {
            HStack {
                Spacer()
                Image(info.imageName)
                    .resizable()
                    .frame(width: 70, height: 70)
                VStack(alignment: .leading) {
                    Text(info.title)
                        .font(.title.bold())
                    Text("\(info.rating) • \(network.sec)")
                }
                Spacer()
            }
            
            Text(info.description)
                .padding(.top, 4)
        } label: {
            Text(network.ssid)
        }
        .listRowBackground(info.backgroundColor)
        .foregroundColor(.white)
    }
}