import CoreBluetooth
import Combine
import os

private let log = Logger(subsystem: "com.matt.ledsignbridge", category: "BLE")

class BLEManager: NSObject, ObservableObject {
    static let serviceUUID = CBUUID(string: "FFF0")
    static let writeUUID = CBUUID(string: "FFF2")
    static let notifyUUID = CBUUID(string: "FFF1")
    static let namePrefixes = ["I_TL", "YS", "TL"]

    enum State: String {
        case idle = "Idle"
        case scanning = "Scanning"
        case connecting = "Connecting"
        case discoveringServices = "Discovering services"
        case ready = "Ready"
        case error = "Error"
    }

    @Published var state: State = .idle
    @Published var statusMessage = ""
    @Published var deviceName = ""
    @Published var deviceInfo = ""
    @Published var rssi: Int = 0
    @Published var lastResponse: Data?
    @Published var commandCount: Int = 0
    @Published var diagnostics: [String] = []

    func addDiag(_ msg: String) {
        log.info("\(msg)")
        if Thread.isMainThread {
            diagnostics.append(msg)
        } else {
            DispatchQueue.main.async { [weak self] in self?.diagnostics.append(msg) }
        }
    }

    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var writeChar: CBCharacteristic?
    private var notifyChar: CBCharacteristic?
    private var sno: UInt16 = 0
    private var responseHandler: ((Data) -> Void)?
    private var reconnectTimer: Timer?
    private var mtuRetryCount = 0

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: .main)
    }

    func nextSno() -> UInt16 {
        sno &+= 1
        return sno
    }

    func startScanning() {
        guard centralManager.state == .poweredOn else {
            state = .error
            statusMessage = "Bluetooth not powered on"
            return
        }
        state = .scanning
        statusMessage = "Looking for LED sign..."
        centralManager.scanForPeripherals(withServices: nil, options: nil)
    }

    func disconnect() {
        reconnectTimer?.invalidate()
        reconnectTimer = nil
        mtuRetryCount = 0
        if let p = peripheral {
            centralManager.cancelPeripheralConnection(p)
        }
        peripheral = nil
        writeChar = nil
        notifyChar = nil
        state = .idle
        statusMessage = "Disconnected"
    }

    private let bleWriteChunkSize = 180

    func send(_ data: Data, responseHandler: ((Data) -> Void)? = nil) {
        guard let writeChar, let peripheral, state == .ready else {
            addDiag("Cannot send: not ready (state=\(self.state.rawValue))")
            return
        }
        self.responseHandler = responseHandler
        let hex = data.prefix(40).map { String(format: "%02x", $0) }.joined()
        addDiag("WRITE [\(data.count)B]: \(hex)\(data.count > 40 ? "..." : "")")

        if data.count <= bleWriteChunkSize {
            peripheral.writeValue(data, for: writeChar, type: .withoutResponse)
            commandCount += 1
        } else {
            addDiag("Chunking \(data.count)B into \(bleWriteChunkSize)B pieces")
            Task {
                await sendChunked(data)
                await MainActor.run { self.commandCount += 1 }
            }
        }
    }

    private func sendChunked(_ data: Data) async {
        guard let writeChar, let peripheral else { return }
        var offset = 0
        var chunkNum = 0
        while offset < data.count {
            let end = min(offset + bleWriteChunkSize, data.count)
            let chunk = data[offset..<end]

            while !peripheral.canSendWriteWithoutResponse {
                try? await Task.sleep(for: .milliseconds(10))
            }

            peripheral.writeValue(chunk, for: writeChar, type: .withoutResponse)
            chunkNum += 1
            offset = end
        }
        addDiag("Sent \(chunkNum) chunks for \(data.count)B packet")
    }

    func sendAsync(_ data: Data) async {
        guard let writeChar, let peripheral, state == .ready else { return }
        while !peripheral.canSendWriteWithoutResponse {
            try? await Task.sleep(for: .milliseconds(50))
        }
        peripheral.writeValue(data, for: writeChar, type: .withoutResponse)
        commandCount += 1
    }

    func sendAndWait(_ data: Data) async -> Data? {
        await withCheckedContinuation { continuation in
            var resumed = false
            send(data) { response in
                guard !resumed else { return }
                resumed = true
                continuation.resume(returning: response)
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 5) {
                guard !resumed else { return }
                resumed = true
                continuation.resume(returning: nil)
            }
        }
    }

    private func scheduleReconnect() {
        reconnectTimer?.invalidate()
        reconnectTimer = Timer.scheduledTimer(withTimeInterval: 3, repeats: false) { [weak self] _ in
            self?.startScanning()
        }
    }
}

extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            startScanning()
        } else {
            state = .error
            statusMessage = "Bluetooth state: \(central.state.rawValue)"
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = peripheral.name ?? ""
        guard Self.namePrefixes.contains(where: { name.contains($0) }) else { return }

        log.info("Found \(name)")
        central.stopScan()

        self.peripheral = peripheral
        self.deviceName = name
        self.rssi = RSSI.intValue
        peripheral.delegate = self
        state = .connecting
        statusMessage = "Connecting to \(name)..."
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        log.info("Connected to \(peripheral.name ?? "?")")
        let mtuWnR = peripheral.maximumWriteValueLength(for: .withoutResponse)
        let mtuW = peripheral.maximumWriteValueLength(for: .withResponse)
        addDiag("Connected. MTU: WnR=\(mtuWnR), W=\(mtuW)")
        state = .discoveringServices
        statusMessage = "Waiting for MTU negotiation..."

        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { [weak self] in
            guard let self, let peripheral = self.peripheral else { return }
            let newMtuWnR = peripheral.maximumWriteValueLength(for: .withoutResponse)
            let newMtuW = peripheral.maximumWriteValueLength(for: .withResponse)
            self.addDiag("MTU after 2s delay: WnR=\(newMtuWnR), W=\(newMtuW)")

            if newMtuWnR <= 20 && self.mtuRetryCount < 2 {
                self.mtuRetryCount += 1
                self.addDiag("MTU stuck at minimum, reconnecting (attempt \(self.mtuRetryCount))...")
                central.cancelPeripheralConnection(peripheral)
                return
            }

            if newMtuWnR <= 20 {
                self.addDiag("WARNING: MTU still 20 after retries. Sign may not respond.")
            }

            self.statusMessage = "Discovering services..."
            peripheral.discoverServices([Self.serviceUUID])
        }
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: (any Error)?) {
        log.error("Connection failed: \(error?.localizedDescription ?? "unknown")")
        state = .error
        statusMessage = "Connection failed"
        scheduleReconnect()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: (any Error)?) {
        log.warning("Disconnected: \(error?.localizedDescription ?? "clean")")
        writeChar = nil
        notifyChar = nil
        state = .error
        statusMessage = "Disconnected — reconnecting..."
        scheduleReconnect()
    }
}

extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        let uuids = invalidatedServices.map { $0.uuid.uuidString }.joined(separator: ", ")
        addDiag("SERVICES CHANGED! Invalidated: \(uuids)")
        addDiag("Re-discovering services after Service Changed indication...")
        peripheral.discoverServices([Self.serviceUUID])
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: (any Error)?) {
        if let error {
            log.error("Service discovery error: \(error.localizedDescription)")
            state = .error
            statusMessage = "Service discovery failed: \(error.localizedDescription)"
            return
        }

        let services = peripheral.services ?? []
        addDiag("Found \(services.count) services")
        for svc in services {
            addDiag("  svc: \(svc.uuid)")
        }

        guard let service = services.first(where: { $0.uuid == Self.serviceUUID }) else {
            let uuids = services.map { $0.uuid.uuidString }.joined(separator: ", ")
            log.error("FFF0 not found among: \(uuids)")
            state = .error
            statusMessage = "FFF0 not found. Got: \(uuids)"
            scheduleReconnect()
            return
        }

        log.info("FFF0 service found, discovering characteristics...")
        peripheral.discoverCharacteristics(nil, for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: (any Error)?) {
        if let error {
            log.error("Characteristic discovery error: \(error.localizedDescription)")
            state = .error
            statusMessage = "Characteristic discovery failed"
            return
        }

        for char in service.characteristics ?? [] {
            let props = char.properties
            var propStr: [String] = []
            if props.contains(.read) { propStr.append("R") }
            if props.contains(.write) { propStr.append("W") }
            if props.contains(.writeWithoutResponse) { propStr.append("WnR") }
            if props.contains(.notify) { propStr.append("N") }
            if props.contains(.indicate) { propStr.append("I") }
            addDiag("  char: \(char.uuid) [\(propStr.joined(separator: ","))]")

            if char.uuid == Self.writeUUID {
                writeChar = char
                addDiag("FFF2 write char found, props=\(propStr)")
            } else if char.uuid == Self.notifyUUID {
                notifyChar = char
                peripheral.setNotifyValue(true, for: char)
                addDiag("FFF1 notify char found, enabling notifications")
            }
        }

        if writeChar != nil && notifyChar != nil {
            state = .ready
            statusMessage = "Connected — running init sequence..."
            log.info("BLE bridge ready, sending LOY PLAY init sequence")

            let mtuWnR = peripheral.maximumWriteValueLength(for: .withoutResponse)
            let mtuW = peripheral.maximumWriteValueLength(for: .withResponse)
            addDiag("Post-discovery MTU: WnR=\(mtuWnR), W=\(mtuW)")

            Task {
                try? await Task.sleep(for: .milliseconds(1500))

                let devQuery = AA55Protocol.buildPacket(
                    sno: nextSno(),
                    payload: Data([0x0A, 0x00, 0x1B, 0x00, 0x04, 0x00, 0x06, 0x00]),
                    cmdType: 3)
                addDiag("Init cmd 1 (param_dev) \(devQuery.count)B: \(devQuery.map { String(format: "%02x", $0) }.joined())")

                if let resp = await sendAndWait(devQuery) {
                    let hex = resp.map { String(format: "%02x", $0) }.joined()
                    addDiag("Got response: \(hex.prefix(80))")
                    await MainActor.run {
                        self.deviceInfo = AA55Protocol.parseDeviceInfo(resp) ?? hex.prefix(40).description
                    }
                } else {
                    addDiag("NO RESPONSE to param_dev query")
                    let mtu2 = peripheral.maximumWriteValueLength(for: .withoutResponse)
                    addDiag("Current WnR MTU: \(mtu2)")
                }

                try? await Task.sleep(for: .milliseconds(500))

                let configCmd = AA55Protocol.buildPacket(
                    sno: nextSno(),
                    payload: Data([0x05, 0x09, 0x1A, 0x00, 0x05, 0x13, 0x16, 0x16, 0x08, 0x19, 0x03]),
                    cmdType: 2)
                addDiag("Init cmd 2 (config) \(configCmd.count)B")
                if configCmd.count <= mtuWnR {
                    if let resp = await sendAndWait(configCmd) {
                        addDiag("Config resp: \(resp.map { String(format: "%02x", $0) }.joined())")
                    } else {
                        addDiag("NO RESPONSE to config")
                    }
                } else {
                    addDiag("SKIPPING config cmd: \(configCmd.count)B > MTU \(mtuWnR)")
                }

                try? await Task.sleep(for: .milliseconds(500))

                let fontQuery = AA55Protocol.buildPacket(
                    sno: nextSno(),
                    payload: Data([0x36, 0x00]),
                    cmdType: 3)
                addDiag("Init cmd 3 (fonts) \(fontQuery.count)B")
                if let resp = await sendAndWait(fontQuery) {
                    addDiag("Fonts resp: \(resp.prefix(30).map { String(format: "%02x", $0) }.joined())")
                } else {
                    addDiag("NO RESPONSE to font query")
                }

                await MainActor.run {
                    self.statusMessage = "Connected and ready"
                }
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: (any Error)?) {
        if let error {
            addDiag("Char update error: \(error.localizedDescription)")
            return
        }
        guard let data = characteristic.value else { return }

        let hex = data.prefix(30).map { String(format: "%02x", $0) }.joined()
        let aa55 = data.starts(with: [0xAA, 0x55]) ? " AA55!" : ""
        addDiag("NOTIFY \(characteristic.uuid) [\(data.count)B]: \(hex)\(aa55)")
        lastResponse = data

        if let handler = responseHandler {
            responseHandler = nil
            handler(data)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: (any Error)?) {
        if let error {
            addDiag("Write error on \(characteristic.uuid): \(error.localizedDescription)")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: (any Error)?) {
        if let error {
            addDiag("Notify state error on \(characteristic.uuid): \(error.localizedDescription)")
        } else {
            addDiag("Notifications \(characteristic.isNotifying ? "ON" : "OFF") for \(characteristic.uuid)")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didReadRSSI RSSI: NSNumber, error: (any Error)?) {
        rssi = RSSI.intValue
    }

    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        addDiag("Peripheral ready for WnR writes (flow control cleared)")
    }
}
