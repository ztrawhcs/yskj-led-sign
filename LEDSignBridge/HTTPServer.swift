import Foundation
import Network
import os

private let log = Logger(subsystem: "com.matt.ledsignbridge", category: "HTTP")

class HTTPServer: ObservableObject {
    @Published var isRunning = false
    @Published var requestCount = 0
    @Published var port: UInt16 = 8080
    @Published var lastRequest = ""

    private var listener: NWListener?
    private weak var ble: BLEManager?

    func start(ble: BLEManager, port: UInt16 = 8080) {
        self.ble = ble
        self.port = port

        do {
            let params = NWParameters.tcp
            params.allowLocalEndpointReuse = true
            listener = try NWListener(using: params, on: NWEndpoint.Port(rawValue: port)!)
        } catch {
            log.error("Failed to create listener: \(error.localizedDescription)")
            return
        }

        listener?.newConnectionHandler = { [weak self] conn in
            self?.handleConnection(conn)
        }
        listener?.stateUpdateHandler = { [weak self] state in
            DispatchQueue.main.async {
                switch state {
                case .ready:
                    self?.isRunning = true
                    log.info("HTTP server listening on port \(port)")
                case .failed(let error):
                    self?.isRunning = false
                    log.error("Server failed: \(error.localizedDescription)")
                default:
                    break
                }
            }
        }
        listener?.start(queue: .main)
    }

    func stop() {
        listener?.cancel()
        listener = nil
        isRunning = false
    }

    private func handleConnection(_ conn: NWConnection) {
        conn.start(queue: .main)
        conn.receive(minimumIncompleteLength: 1, maximumLength: 4096) { [weak self] data, _, _, error in
            guard let self, let data, error == nil else {
                conn.cancel()
                return
            }
            guard let request = String(data: data, encoding: .utf8) else {
                self.respond(conn, status: 400, body: ["error": "Invalid request"])
                return
            }

            let lines = request.split(separator: "\r\n")
            guard let firstLine = lines.first else {
                self.respond(conn, status: 400, body: ["error": "Empty request"])
                return
            }

            let parts = firstLine.split(separator: " ")
            guard parts.count >= 2 else {
                self.respond(conn, status: 400, body: ["error": "Malformed request line"])
                return
            }

            let method = String(parts[0])
            let path = String(parts[1])

            DispatchQueue.main.async {
                self.requestCount += 1
                self.lastRequest = "\(method) \(path)"
            }

            log.info("\(method) \(path)")
            self.route(conn, method: method, path: path)
        }
    }

    private func route(_ conn: NWConnection, method: String, path: String) {
        guard let ble else {
            respond(conn, status: 503, body: ["error": "BLE not initialized"])
            return
        }

        let segments = path.split(separator: "/").map(String.init)

        switch (method, segments) {
        case ("GET", ["status"]):
            let lastResp = ble.lastResponse.map { $0.map { String(format: "%02x", $0) }.joined() } ?? "none"
            let diags = Array(ble.diagnostics.suffix(30))
            respond(conn, status: 200, body: [
                "state": ble.state.rawValue,
                "device": ble.deviceName,
                "info": ble.deviceInfo,
                "commands_sent": ble.commandCount,
                "http_requests": requestCount,
                "last_response": lastResp,
                "diagnostics": diags,
            ])

        case ("POST", ["power", "on"]):
            guard ble.state == .ready else {
                respond(conn, status: 503, body: ["error": "Not connected"])
                return
            }
            ble.send(AA55Protocol.powerOn(sno: ble.nextSno()))
            respond(conn, status: 200, body: ["ok": true, "action": "power_on"])

        case ("POST", ["power", "off"]):
            guard ble.state == .ready else {
                respond(conn, status: 503, body: ["error": "Not connected"])
                return
            }
            ble.send(AA55Protocol.powerOff(sno: ble.nextSno()))
            respond(conn, status: 200, body: ["ok": true, "action": "power_off"])

        case ("POST", _) where segments.count == 2 && segments[0] == "brightness":
            guard ble.state == .ready else {
                respond(conn, status: 503, body: ["error": "Not connected"])
                return
            }
            guard let n = UInt8(segments[1]), n <= 15 else {
                respond(conn, status: 400, body: ["error": "Brightness must be 0-15"])
                return
            }
            ble.send(AA55Protocol.setBrightness(sno: ble.nextSno(), level: n))
            respond(conn, status: 200, body: ["ok": true, "action": "brightness", "level": n])

        case ("POST", ["delete"]):
            guard ble.state == .ready else {
                respond(conn, status: 503, body: ["error": "Not connected"])
                return
            }
            ble.send(AA55Protocol.deleteAll(sno: ble.nextSno()))
            respond(conn, status: 200, body: ["ok": true, "action": "delete_all"])

        case ("GET", ["info"]):
            guard ble.state == .ready else {
                respond(conn, status: 503, body: ["error": "Not connected"])
                return
            }
            Task {
                let resp = await ble.sendAndWait(AA55Protocol.getDeviceInfo(sno: ble.nextSno()))
                let info = resp.flatMap { AA55Protocol.parseDeviceInfo($0) } ?? "no response"
                self.respond(conn, status: 200, body: ["info": info])
            }

        case ("POST", _) where segments.count == 2 && segments[0] == "raw":
            guard ble.state == .ready else {
                respond(conn, status: 503, body: ["error": "Not connected"])
                return
            }
            if let data = Data(hexString: segments[1]) {
                ble.send(data)
                respond(conn, status: 200, body: ["ok": true, "action": "raw", "bytes": data.count])
            } else {
                respond(conn, status: 400, body: ["error": "Usage: POST /raw/{hex} e.g. /raw/aa55ffff..."])
            }

        case ("POST", ["reconnect"]):
            ble.disconnect()
            ble.startScanning()
            respond(conn, status: 200, body: ["ok": true, "action": "reconnect"])

        default:
            respond(conn, status: 200, body: [
                "endpoints": [
                    "GET /status",
                    "GET /info",
                    "POST /power/on",
                    "POST /power/off",
                    "POST /brightness/{0-15}",
                    "POST /delete",
                    "POST /raw?hex=aa55...",
                    "POST /reconnect",
                ]
            ])
        }
    }

    private func respond(_ conn: NWConnection, status: Int, body: Any) {
        let statusText: String
        switch status {
        case 200: statusText = "OK"
        case 400: statusText = "Bad Request"
        case 503: statusText = "Service Unavailable"
        default: statusText = "Error"
        }

        let jsonData = (try? JSONSerialization.data(withJSONObject: body, options: [.prettyPrinted, .sortedKeys])) ?? Data()
        let json = String(data: jsonData, encoding: .utf8) ?? "{}"

        let response = """
        HTTP/1.1 \(status) \(statusText)\r
        Content-Type: application/json\r
        Content-Length: \(json.utf8.count)\r
        Access-Control-Allow-Origin: *\r
        Connection: close\r
        \r
        \(json)
        """

        conn.send(content: response.data(using: .utf8), contentContext: .finalMessage, isComplete: true, completion: .contentProcessed { _ in
            conn.cancel()
        })
    }
}

extension Data {
    init?(hexString: String) {
        let hex = hexString.replacingOccurrences(of: " ", with: "")
        guard hex.count % 2 == 0 else { return nil }
        var data = Data(capacity: hex.count / 2)
        var index = hex.startIndex
        while index < hex.endIndex {
            let nextIndex = hex.index(index, offsetBy: 2)
            guard let byte = UInt8(hex[index..<nextIndex], radix: 16) else { return nil }
            data.append(byte)
            index = nextIndex
        }
        self = data
    }
}
