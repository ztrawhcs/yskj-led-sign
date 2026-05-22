#!/usr/bin/env python3
"""Multi-color image test suite for the LED sign.
Run each method until one works."""

import sign
import time
import struct
import hashlib
from PIL import Image, ImageDraw


def make_test_image():
    """Red, green, blue rectangles on black — the standard color test."""
    img = Image.new('RGB', (sign.SIGN_W, sign.SIGN_H), (0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.rectangle([2, 2, 22, 13], fill=(255, 0, 0))
    draw.rectangle([25, 2, 45, 13], fill=(0, 255, 0))
    draw.rectangle([48, 2, 69, 13], fill=(0, 0, 255))
    return img


def delete_and_wait():
    pkt = sign.aa55_packet(sign.next_sno(), bytes([0x08, 0x02, 0x00, 0xFF]), cmd_type=2)
    sign.send_packet(pkt, sign.BRIDGE_URL)
    time.sleep(0.5)


def send_packets(packets):
    for i, pkt in enumerate(packets):
        r = sign.send_packet(pkt, sign.BRIDGE_URL)
        ok = r and r.get('ok')
        print(f'  pkt {i+1}/{len(packets)}: {"OK" if ok else r}')
        time.sleep(0.2)


def method_1_gamma_corrected():
    """Method 1: Gamma-corrected YSTP (explicit gamma=1.6)."""
    print("\n=== METHOD 1: Gamma-corrected YSTP (bpp=2, gamma=1.6) ===")
    img = make_test_image()
    ystp_data = sign.encode_ystp01(img, sign.SIGN_W, sign.SIGN_H, gamma=1.6)
    orig = sign.encode_ystp01
    sign.encode_ystp01 = lambda *a, **k: ystp_data
    delete_and_wait()
    packets = sign.build_image_program(img, scroll='static')
    sign.encode_ystp01 = orig
    print(f"  {len(packets)} packets")
    send_packets(packets)


def method_2_no_item_summary():
    """Method 2: Remove item summary from program header.
    The JS le() function receives the wrong arg type and does nothing,
    so the real JS program header has NO item summary block."""
    print("\n=== METHOD 2: No item summary in prog header ===")
    img = make_test_image()
    ystp_data = sign.encode_ystp01(img, sign.SIGN_W, sign.SIGN_H)
    ts = int(time.time())
    prog_id = 0

    res_str = f"pro1_img_{ts}"
    id_res = hashlib.sha1(res_str.encode()).digest()

    elements = []

    # Program header WITHOUT item summary
    prog_hdr = bytearray()
    prog_hdr += bytes([0x08, 0x02, 0x00, prog_id])
    prog_hdr += bytes([0x09, 0x01, 0x01])
    prog_hdr += bytes([0x0C, 0x01, prog_id])
    prog_hdr += bytes([0x1C, 0x06, 0x00, 0x00])
    prog_hdr += struct.pack("<H", 3)
    prog_hdr += struct.pack("<H", 3)
    prog_hdr += bytes([0x0D, 0x01, 0x00])
    prog_hdr += bytes([0x1D, 0x09])
    prog_hdr += struct.pack("<HHHH", 0, 0, sign.SIGN_W, sign.SIGN_H)
    prog_hdr += bytes([0x00])
    # NO item summary here
    elements.append(bytes(prog_hdr))

    # Item header
    item_hdr = bytearray()
    item_hdr += bytes([0x09, 0x01, 0x01])
    item_hdr += bytes([0x0C, 0x01, prog_id])
    item_hdr += bytes([0x0D, 0x01, 0x00])
    item_hdr += bytes([0x0E, 0x01, 0x00])
    item_hdr += bytes([0x14, 0x03, 0x01, 0x00, 0xFF])
    item_hdr += bytes([0x11, 0x04, 0x00, 0x01, 0x00, 0x0A])
    elements.append(bytes(item_hdr))

    # Chunks
    chunk_max = 960
    total_chunks = (len(ystp_data) + chunk_max - 1) // chunk_max
    for i in range(total_chunks):
        chunk_data = ystp_data[i * chunk_max:(i + 1) * chunk_max]
        chunk_elem = bytearray()
        chunk_elem += bytes([0x09, 0x01, 0x01])
        chunk_elem += bytes([0x0C, 0x01, prog_id])
        chunk_elem += bytes([0x0D, 0x01, 0x00])
        chunk_elem += bytes([0x0E, 0x01, 0x00])
        chunk_elem += bytes([0x12, 0x07])
        chunk_elem += struct.pack("<H", total_chunks)
        chunk_elem += struct.pack("<H", i)
        chunk_elem += struct.pack("<H", chunk_max)
        chunk_elem += bytes([0x00])
        chunk_elem += bytes([0x13]) + sign.var_len(len(chunk_data)) + chunk_data
        elements.append(bytes(chunk_elem))

    elements.append(bytes([0x18, 0x04, 0x02, prog_id, 0x00, 0xFF]))
    elements.append(bytes([0x35, 0x19, prog_id]) + id_res + struct.pack("<I", ts))

    # Build packets
    total_size = sum(len(e) for e in elements)
    wire_order = [elements[0], elements[1]]
    chunks = elements[2:2+total_chunks]
    wire_order.extend(reversed(chunks))
    wire_order.append(elements[2+total_chunks])
    wire_order.append(elements[2+total_chunks+1])

    if total_size > 997:
        packets = [sign.aa55_packet(sign.next_sno(), e, cmd_type=2) for e in wire_order]
    else:
        combined = b"".join(wire_order)
        packets = [sign.aa55_packet(sign.next_sno(), combined, cmd_type=2)]

    delete_and_wait()
    print(f"  {len(packets)} packets")
    send_packets(packets)


def method_3_no_gamma():
    """Method 3: Multi-color WITHOUT gamma correction.
    Maybe this sign model doesn't expect gamma."""
    print("\n=== METHOD 3: No gamma correction (bpp=2) ===")
    img = make_test_image()
    ystp_data = sign.encode_ystp01(img, sign.SIGN_W, sign.SIGN_H, gamma=1.0)

    # Monkey-patch to use our no-gamma data
    orig = sign.encode_ystp01
    sign.encode_ystp01 = lambda *a, **k: ystp_data
    delete_and_wait()
    packets = sign.build_image_program(img, scroll='static')
    sign.encode_ystp01 = orig
    print(f"  {len(packets)} packets")
    send_packets(packets)


def method_4_force_bpp4():
    """Method 4: Force bpp=4 by adding extra dummy colors.
    Maybe the sign only supports bpp=1 and bpp=4, not bpp=2."""
    print("\n=== METHOD 4: Force bpp=4 (add dummy colors) ===")
    img = make_test_image()
    draw = ImageDraw.Draw(img)
    # Add tiny pixels with extra colors to push past 4 unique colors
    draw.point((0, 21), fill=(128, 0, 0))
    draw.point((1, 21), fill=(0, 128, 0))

    delete_and_wait()
    packets = sign.build_image_program(img, scroll='static')

    ystp = sign.encode_ystp01(img, sign.SIGN_W, sign.SIGN_H)
    print(f"  bpp={ystp[14]}, {len(packets)} packets")
    send_packets(packets)


def method_5_gif_format():
    """Method 5: Send as GIF instead of YSTP01.
    The JS has a separate path: if data starts with 'GIF', it sends
    the raw GIF bytes through V()→R() without YSTP encoding."""
    print("\n=== METHOD 5: Raw GIF format ===")
    import io
    img = make_test_image()
    buf = io.BytesIO()
    img.save(buf, format='GIF')
    gif_data = buf.getvalue()
    print(f"  GIF size: {len(gif_data)} bytes")

    ts = int(time.time())
    prog_id = 0
    res_str = f"pro1_gif_{ts}"
    id_res = hashlib.sha1(res_str.encode()).digest()

    elements = []

    # Program header (same as image but without item summary)
    prog_hdr = bytearray()
    prog_hdr += bytes([0x08, 0x02, 0x00, prog_id])
    prog_hdr += bytes([0x09, 0x01, 0x01])
    prog_hdr += bytes([0x0C, 0x01, prog_id])
    prog_hdr += bytes([0x1C, 0x06, 0x00, 0x00])
    prog_hdr += struct.pack("<H", 3)
    prog_hdr += struct.pack("<H", 3)
    prog_hdr += bytes([0x0D, 0x01, 0x00])
    prog_hdr += bytes([0x1D, 0x09])
    prog_hdr += struct.pack("<HHHH", 0, 0, sign.SIGN_W, sign.SIGN_H)
    prog_hdr += bytes([0x00])
    elements.append(bytes(prog_hdr))

    # Item header (V() for GIF — same structure)
    item_hdr = bytearray()
    item_hdr += bytes([0x09, 0x01, 0x01])
    item_hdr += bytes([0x0C, 0x01, prog_id])
    item_hdr += bytes([0x0D, 0x01, 0x00])
    item_hdr += bytes([0x0E, 0x01, 0x00])
    item_hdr += bytes([0x14, 0x03, 0x01, 0x00, 0xFF])
    item_hdr += bytes([0x11, 0x04, 0x00, 0x01, 0x00, 0x0A])
    elements.append(bytes(item_hdr))

    # Chunk the GIF data using R() logic
    chunk_max = 960
    total_chunks = (len(gif_data) + chunk_max - 1) // chunk_max
    for i in range(total_chunks):
        chunk = gif_data[i * chunk_max:(i + 1) * chunk_max]
        chunk_elem = bytearray()
        chunk_elem += bytes([0x09, 0x01, 0x01])
        chunk_elem += bytes([0x0C, 0x01, prog_id])
        chunk_elem += bytes([0x0D, 0x01, 0x00])
        chunk_elem += bytes([0x0E, 0x01, 0x00])
        chunk_elem += bytes([0x12, 0x07])
        chunk_elem += struct.pack("<H", total_chunks)
        chunk_elem += struct.pack("<H", i)
        chunk_elem += struct.pack("<H", chunk_max)
        chunk_elem += bytes([0x00])
        chunk_elem += bytes([0x13]) + sign.var_len(len(chunk)) + chunk
        elements.append(bytes(chunk_elem))

    elements.append(bytes([0x18, 0x04, 0x02, prog_id, 0x00, 0xFF]))
    elements.append(bytes([0x35, 0x19, prog_id]) + id_res + struct.pack("<I", ts))

    wire_order = [elements[0], elements[1]]
    chunks_list = elements[2:2+total_chunks]
    wire_order.extend(reversed(chunks_list))
    wire_order.append(elements[2+total_chunks])
    wire_order.append(elements[2+total_chunks+1])

    total_size = sum(len(e) for e in elements)
    if total_size > 997:
        packets = [sign.aa55_packet(sign.next_sno(), e, cmd_type=2) for e in wire_order]
    else:
        combined = b"".join(wire_order)
        packets = [sign.aa55_packet(sign.next_sno(), combined, cmd_type=2)]

    delete_and_wait()
    print(f"  {len(packets)} packets")
    send_packets(packets)


def method_6_bmp_format():
    """Method 6: Send raw BMP instead of YSTP01.
    The JS K() function reads BMP, and the sign might accept raw BMP."""
    print("\n=== METHOD 6: Raw BMP format ===")
    import io
    img = make_test_image()
    # Resize to sign dimensions
    img = img.resize((sign.SIGN_W, sign.SIGN_H), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, format='BMP')
    bmp_data = buf.getvalue()
    print(f"  BMP size: {len(bmp_data)} bytes")

    ts = int(time.time())
    prog_id = 0
    res_str = f"pro1_bmp_{ts}"
    id_res = hashlib.sha1(res_str.encode()).digest()

    elements = []

    prog_hdr = bytearray()
    prog_hdr += bytes([0x08, 0x02, 0x00, prog_id])
    prog_hdr += bytes([0x09, 0x01, 0x01])
    prog_hdr += bytes([0x0C, 0x01, prog_id])
    prog_hdr += bytes([0x1C, 0x06, 0x00, 0x00])
    prog_hdr += struct.pack("<H", 3)
    prog_hdr += struct.pack("<H", 3)
    prog_hdr += bytes([0x0D, 0x01, 0x00])
    prog_hdr += bytes([0x1D, 0x09])
    prog_hdr += struct.pack("<HHHH", 0, 0, sign.SIGN_W, sign.SIGN_H)
    prog_hdr += bytes([0x00])
    elements.append(bytes(prog_hdr))

    item_hdr = bytearray()
    item_hdr += bytes([0x09, 0x01, 0x01])
    item_hdr += bytes([0x0C, 0x01, prog_id])
    item_hdr += bytes([0x0D, 0x01, 0x00])
    item_hdr += bytes([0x0E, 0x01, 0x00])
    item_hdr += bytes([0x14, 0x03, 0x01, 0x00, 0xFF])
    item_hdr += bytes([0x11, 0x04, 0x00, 0x01, 0x00, 0x0A])
    elements.append(bytes(item_hdr))

    chunk_max = 960
    total_chunks = (len(bmp_data) + chunk_max - 1) // chunk_max
    for i in range(total_chunks):
        chunk = bmp_data[i * chunk_max:(i + 1) * chunk_max]
        chunk_elem = bytearray()
        chunk_elem += bytes([0x09, 0x01, 0x01])
        chunk_elem += bytes([0x0C, 0x01, prog_id])
        chunk_elem += bytes([0x0D, 0x01, 0x00])
        chunk_elem += bytes([0x0E, 0x01, 0x00])
        chunk_elem += bytes([0x12, 0x07])
        chunk_elem += struct.pack("<H", total_chunks)
        chunk_elem += struct.pack("<H", i)
        chunk_elem += struct.pack("<H", chunk_max)
        chunk_elem += bytes([0x00])
        chunk_elem += bytes([0x13]) + sign.var_len(len(chunk)) + chunk
        elements.append(bytes(chunk_elem))

    elements.append(bytes([0x18, 0x04, 0x02, prog_id, 0x00, 0xFF]))
    elements.append(bytes([0x35, 0x19, prog_id]) + id_res + struct.pack("<I", ts))

    wire_order = [elements[0], elements[1]]
    chunks_list = elements[2:2+total_chunks]
    wire_order.extend(reversed(chunks_list))
    wire_order.append(elements[2+total_chunks])
    wire_order.append(elements[2+total_chunks+1])

    total_size = sum(len(e) for e in elements)
    if total_size > 997:
        packets = [sign.aa55_packet(sign.next_sno(), e, cmd_type=2) for e in wire_order]
    else:
        combined = b"".join(wire_order)
        packets = [sign.aa55_packet(sign.next_sno(), combined, cmd_type=2)]

    delete_and_wait()
    print(f"  {len(packets)} packets")
    send_packets(packets)


def method_7_rt_draw_overlay():
    """Method 7: Multi-color via layered rt_draw calls.
    Send one 1-bit bitmap per unique color. The sign's rt_draw supports
    explicit color per call, so we overlay colored layers on black."""
    print("\n=== METHOD 7: rt_draw color overlay (MOST LIKELY TO WORK) ===")
    img = make_test_image()
    print("  Sending color layers via rt_draw...")
    sign.rt_draw_color_image(img, sign.BRIDGE_URL)
    print("  Done — each color sent as separate rt_draw layer")


def method_8_small_bpp2():
    """Method 8: Tiny bpp=2 image (half-width) to stay under BLE buffer limit.
    If this works but full-width bpp=2 doesn't, it proves BLE packet size is the issue."""
    print("\n=== METHOD 8: Small bpp=2 image (48x22, should be ~400B) ===")
    img = Image.new('RGB', (48, sign.SIGN_H), (0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.rectangle([2, 2, 22, 13], fill=(255, 0, 0))
    draw.rectangle([25, 2, 45, 13], fill=(0, 0, 255))

    ystp = sign.encode_ystp01(img, 48, sign.SIGN_H)
    print(f"  YSTP: {len(ystp)}B, bpp={ystp[14]}")

    # Build program with modified dimensions
    orig_w = sign.SIGN_W
    sign.SIGN_W = 48
    delete_and_wait()
    packets = sign.build_image_program(img, scroll='static')
    sign.SIGN_W = orig_w
    total = sum(len(p) for p in packets)
    print(f"  {len(packets)} packets, total {total}B")
    send_packets(packets)


def method_9_fixed_ystp():
    """Method 9: Fixed YSTP with all agent findings applied.
    - No gamma (gamma=1.0, matching JS when gray=0)
    - No item summary in program header
    - BLE chunking on bridge side (rebuild bridge first!)"""
    print("\n=== METHOD 9: Fixed YSTP (no gamma, no item summary) ===")
    print("  NOTE: Rebuild the iOS bridge for BLE chunking to take effect!")
    img = make_test_image()
    delete_and_wait()
    packets = sign.build_image_program(img, scroll='static')
    ystp = sign.encode_ystp01(img, sign.SIGN_W, sign.SIGN_H)
    print(f"  bpp={ystp[14]}, gamma=1.0, {len(packets)} packets")
    send_packets(packets)


if __name__ == "__main__":
    import sys
    methods = {
        '1': method_1_gamma_corrected,
        '2': method_2_no_item_summary,
        '3': method_3_no_gamma,
        '4': method_4_force_bpp4,
        '5': method_5_gif_format,
        '6': method_6_bmp_format,
        '7': method_7_rt_draw_overlay,
        '8': method_8_small_bpp2,
        '9': method_9_fixed_ystp,
    }

    if len(sys.argv) > 1:
        for num in sys.argv[1:]:
            if num in methods:
                methods[num]()
            else:
                print(f"Unknown method: {num}")
    else:
        print("Color image test methods:")
        print("  1 - Gamma-corrected YSTP (bpp=2, gamma=1.6)")
        print("  2 - No item summary in program header")
        print("  3 - No gamma correction (bpp=2)")
        print("  4 - Force bpp=4 with dummy colors")
        print("  5 - Raw GIF format")
        print("  6 - Raw BMP format")
        print("  7 - rt_draw color overlay (HIGHEST CONFIDENCE)")
        print("  8 - Small bpp=2 (half-width, fits in BLE buffer)")
        print("  9 - Fixed YSTP (no gamma + no item summary)")
        print("\nRecommended order: python3 test_color.py 7 8 9 1 2 3 4 5 6")
        print("  Method 7 first — sidesteps YSTP via rt_draw overlay")
        print("  Method 8 — diagnostic: if this works, BLE packet size is the root cause")
