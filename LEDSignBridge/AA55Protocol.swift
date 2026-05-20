import Foundation

struct AA55Protocol {
    static func buildPacket(sno: UInt16, payload: Data, flags: UInt8 = 0xC1, cmdType: UInt8 = 2) -> Data {
        var body = Data()
        body.append(contentsOf: withUnsafeBytes(of: sno.littleEndian) { Array($0) })
        body.append(flags)
        body.append(cmdType)
        body.append(payload)

        let hasChecksum = flags & 0x80 != 0
        let length = UInt16(body.count + (hasChecksum ? 2 : 0))
        var pkt = Data([0xAA, 0x55, 0xFF, 0xFF])
        pkt.append(contentsOf: withUnsafeBytes(of: length.littleEndian) { Array($0) })
        pkt.append(body)

        if hasChecksum {
            let checksum = pkt.reduce(0) { UInt16($0) &+ UInt16($1) }
            pkt.append(contentsOf: withUnsafeBytes(of: checksum.littleEndian) { Array($0) })
        }
        return pkt
    }

    static func powerOn(sno: UInt16) -> Data {
        buildPacket(sno: sno, payload: Data([0x04, 0x02, 0x00, 0x01]))
    }

    static func powerOff(sno: UInt16) -> Data {
        buildPacket(sno: sno, payload: Data([0x04, 0x02, 0x00, 0x00]))
    }

    static func setBrightness(sno: UInt16, level: UInt8) -> Data {
        let clamped = min(level, 15)
        return buildPacket(sno: sno, payload: Data([0x06, 0x02, 0x00, clamped]))
    }

    static func getDeviceInfo(sno: UInt16) -> Data {
        buildPacket(sno: sno, payload: Data([0x03, 0x01, 0x00]), cmdType: 3)
    }

    static func getPowerState(sno: UInt16) -> Data {
        buildPacket(sno: sno, payload: Data([0x03, 0x01, 0x01]), cmdType: 3)
    }

    static func getBrightness(sno: UInt16) -> Data {
        buildPacket(sno: sno, payload: Data([0x03, 0x01, 0x02]), cmdType: 3)
    }

    static func deleteAll(sno: UInt16) -> Data {
        buildPacket(sno: sno, payload: Data([0x08, 0x02, 0x00, 0xFF]))
    }

    static func showDevice(sno: UInt16, width: UInt16 = 96, height: UInt16 = 22) -> Data {
        var payload = Data([0x1B, 0x05, 0x01])
        payload.append(contentsOf: withUnsafeBytes(of: width.littleEndian) { Array($0) })
        payload.append(contentsOf: withUnsafeBytes(of: height.littleEndian) { Array($0) })
        return buildPacket(sno: sno, payload: payload)
    }

    static func parseDeviceInfo(_ data: Data) -> String? {
        guard data.count > 12, data[0] == 0xAA, data[1] == 0x55 else { return nil }
        let strlen = Int(data[11])
        guard data.count >= 12 + strlen else { return nil }
        return String(data: data[12..<(12 + strlen)], encoding: .ascii)
    }
}
