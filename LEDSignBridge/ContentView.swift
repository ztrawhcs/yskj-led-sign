import SwiftUI

struct ContentView: View {
    @StateObject private var ble = BLEManager()
    @StateObject private var server = HTTPServer()
    @State private var brightness: Double = 7

    var statusColor: Color {
        switch ble.state {
        case .ready: .green
        case .scanning, .connecting, .discoveringServices: .orange
        case .error: .red
        case .idle: .gray
        }
    }

    var body: some View {
        NavigationStack {
            List {
                Section("Connection") {
                    HStack {
                        Circle()
                            .fill(statusColor)
                            .frame(width: 12, height: 12)
                        Text(ble.state.rawValue)
                            .font(.headline)
                    }
                    if !ble.statusMessage.isEmpty {
                        Text(ble.statusMessage)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    if !ble.deviceName.isEmpty {
                        LabeledContent("Device", value: ble.deviceName)
                    }
                    if !ble.deviceInfo.isEmpty {
                        LabeledContent("Info", value: ble.deviceInfo)
                            .font(.caption)
                    }

                    if ble.state == .idle || ble.state == .error {
                        Button("Connect") {
                            ble.startScanning()
                        }
                    } else if ble.state == .ready {
                        Button("Disconnect", role: .destructive) {
                            ble.disconnect()
                        }
                    }
                }

                if ble.state == .ready {
                    Section("Power") {
                        HStack(spacing: 16) {
                            Button {
                                ble.send(AA55Protocol.powerOn(sno: ble.nextSno()))
                            } label: {
                                Label("ON", systemImage: "power")
                                    .frame(maxWidth: .infinity)
                            }
                            .buttonStyle(.borderedProminent)
                            .tint(.green)

                            Button {
                                ble.send(AA55Protocol.powerOff(sno: ble.nextSno()))
                            } label: {
                                Label("OFF", systemImage: "power")
                                    .frame(maxWidth: .infinity)
                            }
                            .buttonStyle(.borderedProminent)
                            .tint(.red)
                        }
                        .listRowInsets(EdgeInsets(top: 8, leading: 16, bottom: 8, trailing: 16))
                    }

                    Section("Brightness") {
                        VStack {
                            HStack {
                                Image(systemName: "sun.min")
                                Slider(value: $brightness, in: 0...15, step: 1) { editing in
                                    if !editing {
                                        ble.send(AA55Protocol.setBrightness(
                                            sno: ble.nextSno(),
                                            level: UInt8(brightness)
                                        ))
                                    }
                                }
                                Image(systemName: "sun.max.fill")
                            }
                            Text("Level \(Int(brightness))")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }

                    Section("Actions") {
                        Button("Delete All Content", role: .destructive) {
                            ble.send(AA55Protocol.deleteAll(sno: ble.nextSno()))
                        }
                    }
                }

                Section("HTTP Bridge") {
                    HStack {
                        Circle()
                            .fill(server.isRunning ? .green : .gray)
                            .frame(width: 12, height: 12)
                        Text(server.isRunning ? "Running on port \(server.port)" : "Stopped")
                    }
                    if server.isRunning {
                        LabeledContent("Requests", value: "\(server.requestCount)")
                        if !server.lastRequest.isEmpty {
                            LabeledContent("Last", value: server.lastRequest)
                                .font(.caption)
                        }
                    }
                }

                Section("Stats") {
                    LabeledContent("Commands sent", value: "\(ble.commandCount)")
                    if let resp = ble.lastResponse {
                        LabeledContent("Last response") {
                            Text(resp.prefix(20).map { String(format: "%02x", $0) }.joined(separator: " "))
                                .font(.caption.monospaced())
                        }
                    }
                }

                if !ble.diagnostics.isEmpty {
                    Section("Diagnostics (\(ble.diagnostics.count))") {
                        ForEach(Array(ble.diagnostics.suffix(50).enumerated()), id: \.offset) { _, msg in
                            Text(msg)
                                .font(.caption2.monospaced())
                                .foregroundStyle(.secondary)
                        }
                    }
                }
            }
            .navigationTitle("LED Sign Bridge")
        }
        .onAppear {
            server.start(ble: ble)
        }
    }
}
