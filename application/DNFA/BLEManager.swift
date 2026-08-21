//
//  BLEManager.swift
//  DNFA
//
//  Created by hansthirtytwo
//

import Foundation
import CoreBluetooth
internal import Combine


struct BLEDeviceResult: Identifiable {
    let id = UUID()
    let identifier: UUID
    let name: String
    let rssi: Int
}

extension CBManagerState {
    var description: String {
        switch self {
        case .unknown: return "unknown"
        case .resetting: return "resetting"
        case .unsupported: return "unsupported"
        case .unauthorized: return "unauthorized" 
        case .poweredOff: return "poweredOff"
        case .poweredOn: return "poweredOn"
        @unknown default: return "unknown-future-case"
        }
    }
}


class BLEManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {

    @Published var isConnected: Bool = false
    @Published var isScanning: Bool = false
    @Published var lastMessage: String = ""    

    let serviceUUID = CBUUID(string: "68ABF545-7BC8-49F0-BCD0-37E32B52E0AB")
    let controlCharUUID = CBUUID(string: "68ABF545-7BC8-49F0-BCD0-37E32B52E0AC") // phone -> ESP32
    let telemetryCharUUID = CBUUID(string: "68ABF545-7BC8-49F0-BCD0-37E32B52E0AD") // ESP32 -> phone

    private var centralManager: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var controlChar: CBCharacteristic?
    private var telemetryChar: CBCharacteristic?
    private var rejectedIdentifier: UUID?
    private var rejectCooldownUntil = Date.distantPast

    @Published var foundDevices: [BLEDeviceResult] = []
    private var discoveredPeripherals: [UUID: CBPeripheral] = [:]
    private var manualConnectID: UUID?

    @Published var bluetoothState: CBManagerState = .unknown   
    
    @Published var wifi_scanResults: [WifiNetwork] = []
    
    
    override init() {
        super.init()
        // Do not touch CoreBluetooth hardware inside Xcode previews — creating
        // a CBCentralManager there blocks/deadlocks the preview thunk build.
        if ProcessInfo.isInsideXcodeCanvas {
            return
        }
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }


    deinit {
        // CoreBluetooth keeps a strong reference to its delegate; if this
        // object is torn down (e.g. the Xcode preview re-creating ContentView),
        // the stale delegate causes a SIGABRT. Release it explicitly.
        centralManager?.stopScan()
        centralManager?.delegate = nil
        peripheral?.delegate = nil
    }


    // MARK: - Scanning

    func startScanning() {
        guard let central = centralManager, central.state == .poweredOn else { return }
        guard !isScanning else { return }
        foundDevices.removeAll()
        discoveredPeripherals.removeAll()
        isScanning = true
        central.scanForPeripherals(withServices: [serviceUUID], options: nil)
    }

    func stopScanning() {
        centralManager?.stopScan()
        isScanning = false
    }

    /// Manually connect to a specific device found via `foundDevices`.
    func connect(to identifier: UUID) {
        manualConnectID = identifier
        if let peripheral = discoveredPeripherals[identifier] {
            centralManager?.stopScan()
            isScanning = false
            self.peripheral = peripheral
            peripheral.delegate = self
            manualConnectID = nil
            centralManager?.connect(peripheral, options: nil)
        }
        // else: still scanning, didDiscover will connect once it's seen.
    }


    // MARK: - CBCentralManagerDelegate
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        bluetoothState = central.state

        switch central.state {
        case .poweredOn:
            guard !isConnected else { return }
            startScanning()
        case .poweredOff, .unauthorized, .unsupported, .resetting, .unknown:
            isConnected = false
            isScanning = false
            controlChar = nil
            telemetryChar = nil
        @unknown default:
            break
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                         advertisementData: [String: Any], rssi RSSI: NSNumber) {
        // Skip a recently-rejected device to avoid a connect/cancel loop.
        if peripheral.identifier == rejectedIdentifier, Date() < rejectCooldownUntil {
            return
        }

        discoveredPeripherals[peripheral.identifier] = peripheral
        let name = peripheral.name
            ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String)
            ?? "DnFA-C5"
        let rssiVal = RSSI.intValue
        if !foundDevices.contains(where: { $0.identifier == peripheral.identifier }) {
            foundDevices.append(BLEDeviceResult(identifier: peripheral.identifier,
                                               name: name, rssi: rssiVal))
        }

        // Manual pick: only connect when it matches the requested device.
        if let want = manualConnectID, peripheral.identifier != want {
            return
        }
        self.peripheral = peripheral
        peripheral.delegate = self
        central.stopScan()
        isScanning = false
        manualConnectID = nil
        central.connect(peripheral, options: nil)
    }


    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        isConnected = false
        controlChar = nil
        telemetryChar = nil
        lastMessage = "Discovering device services…"
        peripheral.delegate = self
        // Discover every primary service so a UUID mismatch is visible in the
        // on-screen connection status instead of appearing as a generic failure.
        peripheral.discoverServices(nil)
    }


    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        DispatchQueue.main.async {
            self.isConnected = false
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                guard !self.isConnected, self.centralManager?.state == .poweredOn else { return }
                self.isScanning = false
                self.startScanning()
            }
        }
    }


    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        DispatchQueue.main.async {
            self.isConnected = false
            self.controlChar = nil
            self.telemetryChar = nil
            // Auto-reconnect to the DnFA-C5, throttled to avoid a scan storm.
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                guard !self.isConnected, self.centralManager?.state == .poweredOn else { return }
                self.isScanning = false
                self.startScanning()
            }
        }
    }


    // MARK: - CBPeripheralDelegate


    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil else {
            reject(peripheral, message: "error:service_discovery_failed")
            return
        }

        let services = peripheral.services ?? []
        guard let service = services.first(where: { $0.uuid == serviceUUID }) else {
            let discoveredUUIDs = services.map { $0.uuid.uuidString }.joined(separator: ", ")
            let status = discoveredUUIDs.isEmpty
                ? "error:service_not_found (no services returned)"
                : "error:service_not_found (found: \(discoveredUUIDs))"
            reject(peripheral, message: status)
            return
        }

        peripheral.discoverCharacteristics([controlCharUUID, telemetryCharUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard service.uuid == serviceUUID,
              error == nil,
              let characteristics = service.characteristics else {
            reject(peripheral, message: "error:characteristic_discovery_failed")
            return
        }

        controlChar = characteristics.first(where: { $0.uuid == controlCharUUID })
        telemetryChar = characteristics.first(where: { $0.uuid == telemetryCharUUID })

        guard controlChar != nil, let telemetryChar else {
            reject(peripheral, message: "error:wrong_device")
            return
        }

        guard telemetryChar.properties.contains(.notify) || telemetryChar.properties.contains(.indicate) else {
            reject(peripheral, message: "error:telemetry_not_notifiable")
            return
        }

        lastMessage = "Subscribing to telemetry…"
        peripheral.setNotifyValue(true, for: telemetryChar)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard characteristic.uuid == telemetryCharUUID else { return }

        guard error == nil, characteristic.isNotifying else {
            reject(peripheral, message: "error:telemetry_subscription_failed")
            return
        }

        isConnected = true
        lastMessage = "Connected"
    }

    private func reject(_ peripheral: CBPeripheral, message: String) {
        isConnected = false
        lastMessage = message
        rejectedIdentifier = peripheral.identifier
        rejectCooldownUntil = Date().addingTimeInterval(5)
        centralManager?.cancelPeripheralConnection(peripheral)
    }


    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == telemetryCharUUID else { return }
        if let error = error {
            print("BLE notification error: \(error)")
            return
        }
        guard let data = characteristic.value else {
            print("BLE notification: nil data")
            return
        }
        guard let string = String(data: data, encoding: .utf8) else {
            print("BLE notification: not valid UTF-8 (\(data.count) bytes)")
            return
        }

        print("BLE recv \(data.count) bytes: \(string.prefix(80))")

        DispatchQueue.main.async {
            self.lastMessage = string
            self.handleTelemetry(string)
        }
    }


    // MARK: - Sending commands to the ESP32

    /// Write a command string to the control characteristic (0xAC).
    /// Matches the firmware's `cmd_chr_access_cb`, which expects a
    /// null-terminated ASCII/UTF-8 command up to 31 bytes.
    func sendCommand(_ command: String) {
        guard let peripheral = peripheral, let controlChar = controlChar,
              let data = command.data(using: .utf8) else { return }
        print("Sending command: '\(command)' (\(data.count) bytes)") // add this

        let type: CBCharacteristicWriteType = controlChar.properties.contains(.write)
            ? .withResponse : .withoutResponse
        peripheral.writeValue(data, for: controlChar, type: type)
    }


    
    // MARK: - Telemetry parsing


    private var telemetryBuffer = ""

    private func handleTelemetry(_ chunk: String) {
        if chunk == "__END__" {
            defer { telemetryBuffer = "" }
            print("TELEMETRY __END__ — buffer has \(telemetryBuffer.utf8.count) bytes")
            guard let data = telemetryBuffer.data(using: .utf8) else { return }
            do {
                let response = try JSONDecoder().decode(ScanResponse.self, from: data)
                print("Decoded \(response.rows.count) networks")
                
                
                // exclude duplicate networks
                
                wifi_scanResults =  Array(Set(response.rows))
            } catch {
                print("Telemetry decode failed (\(telemetryBuffer.utf8.count) bytes): \(error)")
                print("Raw: \(telemetryBuffer)")
            }
            return
        }

        print("TELEMETRY chunk (\(chunk.count) bytes), buffer now \(telemetryBuffer.utf8.count + chunk.utf8.count)")
        telemetryBuffer += chunk
    }
}
