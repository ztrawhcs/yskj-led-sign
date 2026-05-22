#!/usr/bin/env python3
"""Command-line controller for YSKJ LED signs via the iOS HTTP bridge."""

import argparse
import hashlib
import struct
import sys
import time
import urllib.request
import json
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("pip install Pillow  (required for text/image rendering)")
    sys.exit(1)

SIGN_W, SIGN_H = 96, 22
BRIDGE_URL = "http://matts-iphone.local:8080"

# ---------------------------------------------------------------------------
# AA55 packet builder
# ---------------------------------------------------------------------------

def aa55_packet(sno: int, payload: bytes, flags: int = 0xC1, cmd_type: int = 2) -> bytes:
    body = struct.pack("<H", sno) + bytes([flags, cmd_type]) + payload
    has_checksum = flags & 0x80 != 0
    length = len(body) + (2 if has_checksum else 0)
    pkt = b"\xAA\x55\xFF\xFF" + struct.pack("<H", length) + body
    if has_checksum:
        checksum = sum(pkt) & 0xFFFF
        pkt += struct.pack("<H", checksum)
    return pkt


_sno = 0
def next_sno() -> int:
    global _sno
    _sno += 1
    return _sno


# ---------------------------------------------------------------------------
# Variable-length encoding (BER-TLV style, used by rt_draw)
# ---------------------------------------------------------------------------

def var_len(n: int) -> bytes:
    if n < 128:
        return bytes([n])
    elif n < 256:
        return bytes([0x81, n])
    else:
        return bytes([0x82, n & 0xFF, (n >> 8) & 0xFF])


# ---------------------------------------------------------------------------
# Bitmap helpers
# ---------------------------------------------------------------------------

def image_to_bitmap(img: Image.Image) -> bytes:
    """Convert a PIL Image to 1-bit packed bitmap (MSB first, top-to-bottom).
    White pixels (255/1) = ON = bit set."""
    img = img.convert("1")
    if img.size != (SIGN_W, SIGN_H):
        img = img.resize((SIGN_W, SIGN_H), Image.LANCZOS)

    row_bytes = (SIGN_W + 7) // 8  # 12 bytes per row
    bitmap = bytearray(row_bytes * SIGN_H)
    pixels = img.load()

    for y in range(SIGN_H):
        for x in range(SIGN_W):
            if pixels[x, y]:  # white/nonzero = pixel ON
                byte_idx = y * row_bytes + x // 8
                bit_idx = 7 - (x % 8)  # MSB first
                bitmap[byte_idx] |= (1 << bit_idx)

    return bytes(bitmap)


def render_text(text: str, font_size: int = 18) -> Image.Image:
    """Render text to a 96x22 1-bit image. White pixels = ON."""
    img = Image.new("1", (SIGN_W, SIGN_H), color=0)  # black background
    draw = ImageDraw.Draw(img)

    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", font_size)
    except (OSError, IOError):
        try:
            font = ImageFont.truetype("/System/Library/Fonts/SFNSMono.ttf", font_size)
        except (OSError, IOError):
            font = ImageFont.load_default()

    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = (SIGN_W - tw) // 2
    y = (SIGN_H - th) // 2 - bbox[1]

    draw.text((x, y), text, fill=1, font=font)  # white text on black

    return img


# ---------------------------------------------------------------------------
# rt_draw commands
# ---------------------------------------------------------------------------

def rt_draw_bitmap(bitmap_data: bytes, x0=0, y0=0, x1=95, y1=21,
                   color: tuple[int, int, int] = (255, 255, 255)) -> bytes:
    """Build rt_draw type=0 (bitmap) payload."""
    r, g, b = color
    inner = (
        b"\x00"  # type = bitmap
        + bytes([r, g, b])
        + struct.pack("<HH", x0, y0)
        + struct.pack("<HH", x1, y1)
        + bitmap_data
    )
    return b"\x32" + var_len(len(inner)) + inner


def rt_draw_color_image(img: Image.Image, bridge_url: str = BRIDGE_URL,
                        max_colors: int = 8) -> bool:
    """Send a multi-color image using layered rt_draw calls — one per unique color.
    Each layer is a 1-bit bitmap with its color, overlaid on the cleared screen.
    Quantizes to max_colors to keep the number of BLE packets manageable."""
    img = img.convert("RGB").resize((SIGN_W, SIGN_H), Image.LANCZOS)

    if max_colors < 256:
        img = img.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT).convert("RGB")
    pixels = img.load()

    colors = {}
    for y in range(SIGN_H):
        for x in range(SIGN_W):
            c = pixels[x, y]
            if c == (0, 0, 0):
                continue
            if c not in colors:
                colors[c] = []
            colors[c].append((x, y))

    # Pre-build all layer packets before sending (minimize gap between clear and first draw)
    row_bytes = (SIGN_W + 7) // 8
    layers = []
    for color, pixel_list in colors.items():
        bitmap = bytearray(row_bytes * SIGN_H)
        for x, y in pixel_list:
            byte_idx = y * row_bytes + x // 8
            bit_idx = 7 - (x % 8)
            bitmap[byte_idx] |= (1 << bit_idx)
        payload = rt_draw_bitmap(bytes(bitmap), color=color)
        layers.append((len(pixel_list), aa55_packet(next_sno(), payload, cmd_type=2)))

    # Sort largest layers first so main content appears immediately
    layers.sort(key=lambda x: -x[0])

    clear_pkt = aa55_packet(next_sno(), rt_draw_clear(), cmd_type=2)
    send_packet(clear_pkt, bridge_url)
    time.sleep(0.02)

    for _, pkt in layers:
        send_packet(pkt, bridge_url)
        time.sleep(0.02)

    return True


def rt_draw_icon(icon_name: str, x0: int, color: tuple[int, int, int]) -> bytes:
    """Build rt_draw for a 16x16 weather icon at position x0."""
    rows = WEATHER_BITMAPS.get(icon_name, WEATHER_BITMAPS["cloud"])
    bitmap = bytearray()
    for row_val in rows:
        bitmap.append((row_val >> 8) & 0xFF)
        bitmap.append(row_val & 0xFF)
    r, g, b = color
    inner = (
        b"\x00"
        + bytes([r, g, b])
        + struct.pack("<HH", x0, 3)     # top-left (y=3 to vertically center in 22px)
        + struct.pack("<HH", x0 + 15, 18)  # bottom-right
        + bytes(bitmap)
    )
    return b"\x32" + var_len(len(inner)) + inner


def rt_draw_clear() -> bytes:
    """Build rt_draw type=1 (rectangle fill) to clear screen."""
    return bytes([
        0x32, 0x0D, 0x01,
        0x00, 0x00, 0x00,  # color = black (off)
        0x00,  # type_rect = filled
        0x00, 0x00,  # x0
        0x00, 0x00,  # y0
        SIGN_W - 1, 0x00,  # x1
        SIGN_H - 1, 0x00,  # y1
    ])


# ---------------------------------------------------------------------------
# YSTP01 image encoding (sign's native bitmap format)
# ---------------------------------------------------------------------------

def _build_gamma_lut(gamma: float) -> list[int]:
    """Build gamma correction lookup table matching JS W() function.
    For each input byte value 0-255, returns the gamma-corrected output."""
    lut = [0] * 256
    for a in range(256):
        t = (a + 0.5) / 256.0
        t = t ** gamma
        val = 256.0 * t - 0.5
        # Uint8Array truncation behavior: clamp to 0-255
        lut[a] = max(0, min(255, int(val)))
    return lut


def encode_ystp01(img: Image.Image, width: int = None, height: int = None,
                  gamma: float = 1.0) -> bytes:
    """Encode a PIL Image as YSTP01 format for the sign's program dispatch.

    Gamma: JS uses 1.0 when sign reports gray=0 (default), 1.6 when gray>0.
    Our sign likely reports gray=0, so default to 1.0 (no correction).
    """
    if width is None:
        width = img.size[0]
    if height is None:
        height = img.size[1]

    img = img.convert("RGB").resize((width, height), Image.LANCZOS)
    pixels = img.load()

    # Apply gamma correction to pixel values (matching JS W() function).
    # The JS app reads BMP data through W() which applies gamma before
    # Y() builds the palette and J() encodes pixel data.
    gamma_lut = _build_gamma_lut(gamma)

    # Build palette from unique gamma-corrected colors
    palette = []
    color_index = {}
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            c = (gamma_lut[r], gamma_lut[g], gamma_lut[b])
            if c not in color_index:
                color_index[c] = len(palette)
                palette.append(c)
                if len(palette) > 256:
                    break
        if len(palette) > 256:
            break

    # Determine bits per pixel
    n_colors = len(palette)
    if n_colors <= 2:
        bpp = 1
    elif n_colors <= 4:
        bpp = 2
    elif n_colors <= 16:
        bpp = 4
    elif n_colors <= 256:
        bpp = 8
    else:
        bpp = 24

    row_bytes = (width * bpp + 7) // 8

    if bpp <= 8:
        # Palette header
        pal_size = 1 << bpp
        pal_data = bytearray(3 * pal_size)
        for i, (r, g, b) in enumerate(palette):
            pal_data[3 * i] = r
            pal_data[3 * i + 1] = g
            pal_data[3 * i + 2] = b

        # Pixel data (top-to-bottom rows — JS reads BMP bottom-up then
        # iterates g=height-1 down to 0, producing top-to-bottom YSTP output)
        pix_data = bytearray(row_bytes * height)
        for y in range(height):
            bit_offset = 0
            for x in range(width):
                r_raw, g_raw, b_raw = pixels[x, y]
                c = (gamma_lut[r_raw], gamma_lut[g_raw], gamma_lut[b_raw])
                idx = color_index[c]
                byte_pos = y * row_bytes + bit_offset // 8
                bit_pos = bit_offset % 8
                mask = (1 << bpp) - 1
                pix_data[byte_pos] |= (idx & mask) << bit_pos
                bit_offset += bpp
    else:
        # 24-bit: no palette, raw RGB top-to-bottom (NOT BGR!)
        # JS J() inner function writes: p[m+3*b]=v[3*b+2], p[m+3*b+1]=v[3*b+1], p[m+3*b+2]=v[3*b]
        # Source v is BGR (BMP order), so output is R, G, B.
        pal_data = b""
        row_stride = 3 * width  # No BMP alignment — JS uses S(width*24,8) = 3*width
        pix_data = bytearray(row_stride * height)
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                off = y * row_stride + 3 * x
                pix_data[off] = gamma_lut[r]      # R (JS: v[3*b+2] from BGR)
                pix_data[off + 1] = gamma_lut[g]  # G (JS: v[3*b+1])
                pix_data[off + 2] = gamma_lut[b]  # B (JS: v[3*b])

    header = b"YSTP01"
    header += struct.pack("<HH", width, height)
    header += struct.pack("<H", 1)      # page_count = 1
    header += struct.pack("<H", width)  # len_valid = width
    header += bytes([bpp])

    return header + bytes(pal_data) + bytes(pix_data)


def build_image_program(img: Image.Image, scroll: str = "static",
                        speed: int = 1, duration: int = 0,
                        program_id: int = 1) -> list[bytes]:
    """Build an image program upload as a list of AA55 packets.

    Matches the JS protocol exactly:
    - ce() builds program header (delete + tags 09/0C + tag 1C + region + item summary)
    - V() builds item header (O() prefix with animation + tag 11 with type=0x0A)
    - R() builds each data chunk as a SEPARATE element with its own item prefix
    - de() wraps elements: if total > 997, each element gets its own AA55 packet
    - pe() prepends res_hash, then calls de()
    """
    ystp_data = encode_ystp01(img, SIGN_W, SIGN_H)
    ts = int(time.time())
    prog_id = program_id - 1  # 0-based

    res_str = f"pro{program_id}_img_{ts}"
    id_res = hashlib.sha1(res_str.encode()).digest()

    animate_map = {"left": 0, "right": 27, "up": 4, "down": 5, "static": 1}
    ani_type = animate_map.get(scroll, 1)
    speed_byte = max(0, speed - 1)
    time_stay = 255 if scroll == "static" else 0

    # Determine play mode for program header (ce() logic)
    if duration > 0:
        play_mode_ce = 1  # fixed_time
        play_count_ce = duration
    else:
        play_mode_ce = 0  # loop
        play_count_ce = 3  # match text program default

    # ---------------------------------------------------------------
    # Build e[] array (elements), matching JS assembly order
    # Items are unshift'd (prepended), so we build in reverse and
    # reverse at the end before processing with de() logic
    # ---------------------------------------------------------------
    elements = []

    # --- 1. Program header block (ce() output) ---
    # ce() builds: [delete] + [09,01,1-data_save] + [0C,01,prog_id] + [1C,...] + region + item_summary
    # This is unshift'd to e[] first, so it ends up as the LAST element

    prog_hdr = bytearray()

    # Delete existing program inline (tag 0x08) — matches ce() when a != 255
    prog_hdr += bytes([0x08, 0x02, 0x00, prog_id])

    # data_save tag and prog_id tag (matches ce())
    prog_hdr += bytes([0x09, 0x01, 0x01])  # data_save=0 → 1-0=1
    prog_hdr += bytes([0x0C, 0x01, prog_id])

    # Tag 0x1C program header (matches ce() with type_pro=0, play_cmd=0)
    # Format: [1C, 06, bg_type, play_mode, count_lo, count_hi, time_sync_lo, time_sync_hi]
    prog_hdr += bytes([0x1C, 0x06, 0x00, play_mode_ce])  # bg_type=0 (solid black)
    prog_hdr += struct.pack("<H", play_count_ce)
    prog_hdr += struct.pack("<H", 3)  # time_sync = 3 (JS default)

    # Region definition (re() output) — rect_id=0, bg_flag=0
    prog_hdr += bytes([0x0D, 0x01, 0x00])  # rect_id = 0
    prog_hdr += bytes([0x1D, 0x09])         # info_pos tag
    prog_hdr += struct.pack("<HHHH", 0, 0, SIGN_W, SIGN_H)
    prog_hdr += bytes([0x00])               # bg_flag = 0

    # No item summary here — JS le() is buggy and produces nothing in ce() context.
    # The real JS program header has NO item summary block.

    elements.append(bytes(prog_hdr))

    # --- 2. Item header (V() output) ---
    # V() calls O() to build prefix, then adds [11, 04, E(), 0x0A]
    # O() output (r=false, no show prefix): [09,01,1] [0C,01,prog] [0D,01,rect] [0E,01,item] [14,03,ani,spd,stay]
    item_hdr = bytearray()
    item_hdr += bytes([0x09, 0x01, 0x01])  # data_save
    item_hdr += bytes([0x0C, 0x01, prog_id])
    item_hdr += bytes([0x0D, 0x01, 0x00])  # rect_id = 0
    item_hdr += bytes([0x0E, 0x01, 0x00])  # item_id = 0
    item_hdr += bytes([0x14, 0x03, ani_type, speed_byte, time_stay])
    # Tag 0x11 for graphic: [11, 04, play_mode, count_lo, count_hi, 0x0A]
    # When control is null, JS E() writes [0x00, 0x01, 0x00] (play_mode=0, count=1)
    item_hdr += bytes([0x11, 0x04, 0x00, 0x01, 0x00, 0x0A])  # E() with null control, type=graphic

    elements.append(bytes(item_hdr))

    # --- 3. Data chunks (R() output) ---
    # R() builds each chunk as a separate element with its own item prefix
    # Each chunk: [09,01,1-n] [0C,01,prog] [0D,01,rect] [0E,01,item] [12,07,...] [13,varlen,data]
    chunk_max = 960
    total_chunks = (len(ystp_data) + chunk_max - 1) // chunk_max

    # R() iterates forward through chunks but unshift's each, so chunk 0 ends up
    # last in the array (closest to item_header). We'll add them in forward order
    # here; de() processes in reverse, giving wire order: chunk_N-1, ..., chunk_0
    for i in range(total_chunks):
        chunk_data = ystp_data[i * chunk_max:(i + 1) * chunk_max]
        chunk_len = len(chunk_data)

        # Build the 25-byte prefix template (matching R())
        chunk_elem = bytearray()
        chunk_elem += bytes([0x09, 0x01, 0x01])  # data_save = 1 (1-0)
        chunk_elem += bytes([0x0C, 0x01, prog_id])
        chunk_elem += bytes([0x0D, 0x01, 0x00])  # rect_id
        chunk_elem += bytes([0x0E, 0x01, 0x00])  # item_id

        # Chunk header: [12, 07, total_lo, total_hi, idx_lo, idx_hi, max_size_lo, max_size_hi, 0x00]
        # NOTE: R() always writes A=960 here, NOT the actual chunk size.
        # The actual data length is encoded in the var_len after tag 0x13.
        chunk_elem += bytes([0x12, 0x07])
        chunk_elem += struct.pack("<H", total_chunks)
        chunk_elem += struct.pack("<H", i)
        chunk_elem += struct.pack("<H", chunk_max)  # always 960, matching JS R()
        chunk_elem += bytes([0x00])

        # Chunk data: [13, var_len, data...]
        chunk_elem += bytes([0x13]) + var_len(chunk_len) + chunk_data

        elements.append(bytes(chunk_elem))

    # --- 4. Dispatch (from ue() inline function L()) ---
    # For show_now=1 with default play: [18, 04, 02, prog_id, 00, FF] (6 bytes)
    # a=255 means no fixed count, i=4 is the data length
    dispatch = bytes([0x18, 0x04, 0x02, prog_id, 0x00, 0xFF])

    elements.append(dispatch)

    # --- 5. Resource hash (pe() prepends this) ---
    res_hash = bytes([0x35, 0x19, prog_id]) + id_res + struct.pack("<I", ts)
    elements.append(res_hash)

    # ---------------------------------------------------------------
    # de() logic: wrap elements into AA55 packets
    # ---------------------------------------------------------------
    # elements[] is ordered: [prog_hdr, item_hdr, chunk_0, ..., chunk_N-1, dispatch, res_hash]
    # de() processes in REVERSE order (last→first), giving wire order:
    #   res_hash → dispatch → chunk_N-1 → ... → chunk_0 → item_hdr → prog_hdr
    # Wait — de() iterates l=o-1 down to 0 and appends to output buffer,
    # so the FIRST bytes on the wire are from e[last] = res_hash... but that's
    # wrong, res_hash should be first conceptually but last to be sent.
    #
    # Actually re-reading de(): it processes e[] from END to START, writing
    # each AA55 packet sequentially. Since e[] was built with unshift(),
    # e[0]=res_hash (first unshifted = most recently added to front).
    # But our elements[] is built in forward order. We need to reverse
    # the mapping:
    #
    # JS e[] after all unshifts (in order):
    #   [0]=res_hash, [1]=dispatch, [2]=chunk_0, ..., [N+1]=chunk_N-1,
    #   [N+2]=item_hdr, [N+3]=prog_hdr
    #
    # de() iterates l from (len-1) down to 0:
    #   prog_hdr → item_hdr → chunk_N-1 → ... → chunk_0 → dispatch → res_hash
    #
    # So wire order is: prog_hdr, item_hdr, chunk_N-1...chunk_0, dispatch, res_hash
    #
    # Our elements[] = [prog_hdr, item_hdr, chunk_0, ..., chunk_N-1, dispatch, res_hash]
    # To match de()'s reverse iteration, we reverse our list so last=prog_hdr:

    total_size = sum(len(e) for e in elements)
    CHUNK_THRESHOLD = 997  # y = A + 37 = 960 + 37

    packets = []

    if total_size > CHUNK_THRESHOLD:
        # Each element gets its own AA55 packet
        # de() iterates from end to start of the JS e[] array
        # JS e[] = [res_hash, dispatch, chunk_0..N-1, item_hdr, prog_hdr]
        # de() l=len-1 to 0 → prog_hdr first on wire
        #
        # Our elements = [prog_hdr, item_hdr, chunk_0..N-1, dispatch, res_hash]
        # We want wire order: prog_hdr, item_hdr, chunks(reversed), dispatch, res_hash
        #
        # Build wire-order list:
        wire_order = []
        wire_order.append(elements[0])   # prog_hdr
        wire_order.append(elements[1])   # item_hdr
        # Chunks in reverse order (chunk_N-1 first, chunk_0 last)
        chunk_elements = elements[2:2+total_chunks]
        wire_order.extend(reversed(chunk_elements))
        wire_order.append(elements[2+total_chunks])   # dispatch
        wire_order.append(elements[2+total_chunks+1]) # res_hash

        for elem in wire_order:
            packets.append(aa55_packet(next_sno(), elem, cmd_type=2))
    else:
        # Small payload: concatenate all elements (reverse order like de())
        # and wrap in a single AA55 packet
        combined = b""
        wire_order = []
        wire_order.append(elements[0])
        wire_order.append(elements[1])
        chunk_elements = elements[2:2+total_chunks]
        wire_order.extend(reversed(chunk_elements))
        wire_order.append(elements[2+total_chunks])
        wire_order.append(elements[2+total_chunks+1])
        for elem in wire_order:
            combined += elem
        packets.append(aa55_packet(next_sno(), combined, cmd_type=2))

    return packets


# ---------------------------------------------------------------------------
# Program upload (text with built-in sign fonts + animations)
# ---------------------------------------------------------------------------

def text_to_gb2312(text: str) -> bytes:
    try:
        return text.encode("gb2312")
    except (UnicodeEncodeError, LookupError):
        return text.encode("ascii", errors="replace")


def build_text_program(text: str, font_size: int = 16, scroll: str = "left",
                       speed: int = 10, duration: int = 0, program_id: int = 1,
                       color: tuple[int, int, int] = (255, 255, 255),
                       segments: list[tuple[str, tuple[int, int, int]]] | None = None,
                       font_family: int = 0x00) -> list[bytes]:
    """Build a text program upload as a list of AA55 packets.

    scroll: 'left', 'right', 'up', 'down', 'static'
    speed: 1-20 (higher = faster)
    duration: seconds to display (0 = loop forever)
    """
    text_bytes = text_to_gb2312(text)
    ts = int(time.time())

    # Compute resource hash (SHA1 of content identifier)
    res_str = f"pro{program_id}_text_{text}_{ts}"
    id_res = hashlib.sha1(res_str.encode()).digest()

    packets = []

    # --- Packet 1: Delete existing program ---
    delete_payload = bytes([0x08, 0x02, 0x00, 0xFF])
    packets.append(aa55_packet(next_sno(), delete_payload, cmd_type=2))

    # --- Packet 2: Resource hash (tag 0x35) ---
    res_hash_tlv = bytes([0x35, 0x19, program_id - 1]) + id_res + struct.pack("<I", ts)

    # --- Packet 3: Program header (tag 0x1C) ---
    play_mode = 0x00 if duration == 0 else 0x01  # 0=loop, 1=fixed_time
    play_count = duration if duration > 0 else 3
    prog_header = bytes([
        0x1C, 0x08,
        0x00,  # bg_type = solid
        0x00, 0x00, 0x00,  # bg_color = black
        play_mode,
        0x00,  # reserved
    ]) + struct.pack("<H", play_count)

    # --- Packet 4: Region definition (tag 0x0D) ---
    region_def = bytes([0x0D, 0x01, 0x00])  # rect_id=0
    region_def += bytes([0x1D, 0x09])  # info_pos tag
    region_def += struct.pack("<HHHH", 0, 0, SIGN_W, SIGN_H)
    region_def += bytes([0x00])  # bg_flag

    # --- Packet 5: Text item header (tag 0x11) ---
    animate_map = {"left": 0, "right": 27, "up": 4, "down": 5, "static": 1}
    ani_type = animate_map.get(scroll, 0)

    # --- Item header with inline animation (matches O() function) ---
    # O() output: [09 01 1] [0C 01 prog_id] [0D 01 rect_id] [0E 01 item_id] [14 03 ani speed stay]
    speed_byte = max(0, speed - 1)  # LOY PLAY subtracts 1
    time_stay = 0 if scroll != "static" else 3
    item_prefix = (
        bytes([0x09, 0x01, 0x01])  # data_save = 1
        + bytes([0x0C, 0x01, program_id - 1])  # program id
        + bytes([0x0D, 0x01, 0x00])  # rect id = 0
        + bytes([0x0E, 0x01, 0x00])  # item id = 0
        + bytes([0x14, 0x03, ani_type, speed_byte, time_stay])  # animation
    )

    # Text item header (tag 0x11): type=6 (text_audio for built-in rendering)
    text_header = bytes([
        0x11, 0x0A,
        0x00, 0x00, 0x00,  # control bytes
        0x06,  # type = text_audio (sign renders text)
        0x01,  # code = default font
        0x00,  # font family
        font_size,
        0x00,  # rotate = 0
    ]) + struct.pack("<H", 0)  # interval

    # Pad scrolling text with spaces so there's a gap between repeats
    if scroll != "static" and not segments:
        text_bytes += b"          "  # 10 trailing spaces

    # --- Text data with formatting ---
    # Alignment: left for scrolling (center causes offset with padding), center for static
    if scroll == "static":
        align_data = bytes([0x03, 0x00, 0x01, 0x03, 0x01, 0x01])  # center/center
    else:
        align_data = bytes([0x03, 0x00, 0x00, 0x03, 0x01, 0x00])  # left/left
    # Font spec: [tag, subtag, size, family]
    font_data = bytes([0x01, 0x01, font_size, font_family])

    if segments:
        # Multi-color: first segment gets alignment + font, rest just get color changes
        first_r, first_g, first_b = segments[0][1]
        text_data = bytes([0x00, first_r, first_g, first_b, 0x00, 0x00, 0x00])
        text_data += align_data + font_data
        first_seg_bytes = text_to_gb2312(segments[0][0])
        if scroll != "static":
            first_seg_bytes += b"          "
        text_data += first_seg_bytes
        for seg_text, seg_color in segments[1:]:
            sr, sg, sb = seg_color
            text_data += bytes([0x00, sr, sg, sb, 0x00, 0x00, 0x00])
            text_data += text_to_gb2312(seg_text)
    else:
        r, g, b = color
        color_data = bytes([0x00, r, g, b, 0x00, 0x00, 0x00])
        text_data = color_data + align_data + font_data + text_bytes

    # Chunk the text data
    chunk_size = 960
    total_chunks = (len(text_data) + chunk_size - 1) // chunk_size
    chunk_tlvs = b""
    for i in range(total_chunks):
        chunk = text_data[i * chunk_size:(i + 1) * chunk_size]
        chunk_hdr = bytes([0x12, 0x07]) + struct.pack("<HHH", total_chunks, i, len(chunk)) + bytes([0x00])
        chunk_tlv = chunk_hdr + bytes([0x13]) + var_len(len(chunk)) + chunk
        chunk_tlvs += chunk_tlv

    # --- Dispatch (tag 0x18) ---
    dispatch = bytes([0x18, 0x06, 0x02, program_id - 1, 0x00, play_mode]) + struct.pack("<H", play_count)

    # Assemble all TLVs into one payload
    full_payload = (
        res_hash_tlv + prog_header + region_def +
        item_prefix + text_header + chunk_tlvs + dispatch
    )

    # Split into AA55 packets if needed (max ~500 bytes per packet to be safe)
    max_payload = 480
    offset = 0
    while offset < len(full_payload):
        chunk = full_payload[offset:offset + max_payload]
        packets.append(aa55_packet(next_sno(), chunk, cmd_type=2))
        offset += max_payload

    return packets


# ---------------------------------------------------------------------------
# Bridge communication
# ---------------------------------------------------------------------------

def send_raw(hex_data: str, bridge_url: str = BRIDGE_URL):
    url = f"{bridge_url}/raw/{hex_data}"
    req = urllib.request.Request(url, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())
    except Exception as e:
        print(f"Error: {e}")
        return None


def send_packet(pkt: bytes, bridge_url: str = BRIDGE_URL):
    return send_raw(pkt.hex(), bridge_url)


def get_status(bridge_url: str = BRIDGE_URL):
    try:
        with urllib.request.urlopen(f"{bridge_url}/status", timeout=5) as resp:
            return json.loads(resp.read())
    except Exception as e:
        print(f"Error: {e}")
        return None


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def cmd_text(args):
    """Send text to the display using rt_draw (bitmap)."""
    img = render_text(args.text, font_size=args.size)
    bitmap = image_to_bitmap(img)

    if args.preview:
        for y in range(SIGN_H):
            row = ""
            for x in range(SIGN_W):
                byte_idx = y * 12 + x // 8
                bit_idx = 7 - (x % 8)
                row += "#" if bitmap[byte_idx] & (1 << bit_idx) else " "
            print(f"|{row}|")
        return

    # Clear then draw
    clear_pkt = aa55_packet(next_sno(), rt_draw_clear(), cmd_type=2)
    draw_payload = rt_draw_bitmap(bitmap)
    draw_pkt = aa55_packet(next_sno(), draw_payload, cmd_type=2)

    print(f"Sending text: {args.text!r}")
    print(f"  clear: {len(clear_pkt)}B, draw: {len(draw_pkt)}B")

    r1 = send_packet(clear_pkt, args.bridge)
    time.sleep(0.1)
    r2 = send_packet(draw_pkt, args.bridge)

    if r2 and r2.get("ok"):
        print("Done!")
    else:
        print(f"Response: {r2}")


def cmd_scroll(args):
    """Send scrolling text using the sign's built-in font renderer."""
    packets = build_text_program(
        args.text,
        font_size=args.size,
        scroll=args.direction,
        speed=args.speed,
        duration=args.duration,
    )

    print(f"Sending scrolling text: {args.text!r} ({args.direction}, speed={args.speed})")
    print(f"  {len(packets)} packets")

    for i, pkt in enumerate(packets):
        r = send_packet(pkt, args.bridge)
        if r and not r.get("ok", True):
            print(f"  packet {i}: {r}")
        time.sleep(0.15)

    print("Done!")


def cmd_image(args):
    """Send an image file to the display."""
    img = Image.open(args.file)

    # Resize to fit, maintaining aspect ratio
    img.thumbnail((SIGN_W, SIGN_H), Image.LANCZOS)

    # Center on black background
    bg = Image.new("RGB", (SIGN_W, SIGN_H), (0, 0, 0))
    offset = ((SIGN_W - img.size[0]) // 2, (SIGN_H - img.size[1]) // 2)
    bg.paste(img, offset)

    # Convert to 1-bit (black pixels = ON for sign)
    bw = bg.convert("L")
    threshold = args.threshold
    bw = bw.point(lambda p: 0 if p > threshold else 255).convert("1")

    if args.invert:
        from PIL import ImageOps
        bw = ImageOps.invert(bw.convert("L")).convert("1")

    bitmap = image_to_bitmap(bw)

    if args.preview:
        for y in range(SIGN_H):
            row = ""
            for x in range(SIGN_W):
                byte_idx = y * 12 + x // 8
                bit_idx = 7 - (x % 8)
                row += "#" if bitmap[byte_idx] & (1 << bit_idx) else " "
            print(f"|{row}|")
        return

    clear_pkt = aa55_packet(next_sno(), rt_draw_clear(), cmd_type=2)
    draw_payload = rt_draw_bitmap(bitmap)
    draw_pkt = aa55_packet(next_sno(), draw_payload, cmd_type=2)

    print(f"Sending image: {args.file}")
    r1 = send_packet(clear_pkt, args.bridge)
    time.sleep(0.1)
    r2 = send_packet(draw_pkt, args.bridge)

    if r2 and r2.get("ok"):
        print("Done!")
    else:
        print(f"Response: {r2}")


def cmd_clear(args):
    """Clear the display."""
    pkt = aa55_packet(next_sno(), rt_draw_clear(), cmd_type=2)
    r = send_packet(pkt, args.bridge)
    print("Cleared!" if r and r.get("ok") else f"Response: {r}")


def cmd_power(args):
    """Power on/off."""
    url = f"{args.bridge}/power/{args.state}"
    req = urllib.request.Request(url, method="POST")
    with urllib.request.urlopen(req, timeout=5) as resp:
        print(json.loads(resp.read()))


def cmd_brightness(args):
    """Set brightness."""
    url = f"{args.bridge}/brightness/{args.level}"
    req = urllib.request.Request(url, method="POST")
    with urllib.request.urlopen(req, timeout=5) as resp:
        print(json.loads(resp.read()))


def cmd_status(args):
    """Show bridge/sign status."""
    s = get_status(args.bridge)
    if s:
        print(f"State: {s['state']}")
        print(f"Device: {s['device']}")
        if s.get('info'):
            print(f"Info: {s['info']}")
        print(f"Commands sent: {s['commands_sent']}")
        print(f"HTTP requests: {s['http_requests']}")


def temp_color(temp_f: int) -> tuple[int, int, int]:
    if temp_f <= 32:
        return (0, 100, 255)      # ice blue
    elif temp_f <= 50:
        return (0, 200, 255)      # cyan
    elif temp_f <= 65:
        return (0, 255, 100)      # green
    elif temp_f <= 80:
        return (180, 255, 0)      # yellow-green
    elif temp_f <= 90:
        return (255, 140, 0)      # orange
    else:
        return (255, 40, 0)       # red


# Weather icon bitmaps (16x16 pixels, 1-bit, 2 bytes per row)
# These get sent as a separate rt_draw before the text program
WEATHER_BITMAPS = {
    "sun": [
        0x0100, 0x0100, 0x2108, 0x1210, 0x0820, 0x07C0, 0x0820, 0x1FF8,
        0x0820, 0x07C0, 0x0820, 0x1210, 0x2108, 0x0100, 0x0100, 0x0000,
    ],
    "moon": [
        0x0780, 0x0FC0, 0x1FC0, 0x1F80, 0x3F00, 0x3F00, 0x3F00, 0x3F00,
        0x3F00, 0x3F00, 0x1F80, 0x1FC0, 0x0FC0, 0x0780, 0x0100, 0x0000,
    ],
    "cloud": [
        0x0000, 0x0000, 0x0000, 0x03C0, 0x0FF0, 0x1C38, 0x1008, 0x700E,
        0xC003, 0x8001, 0x8001, 0xC003, 0x7FFE, 0x3FFC, 0x0000, 0x0000,
    ],
    "rain": [
        0x03C0, 0x0FF0, 0x1C38, 0x1008, 0x700E, 0xC003, 0x8001, 0xFFFF,
        0x7FFE, 0x0000, 0x4422, 0x2244, 0x4422, 0x2244, 0x0000, 0x0000,
    ],
    "snow": [
        0x03C0, 0x0FF0, 0x1C38, 0x1008, 0x700E, 0xC003, 0x8001, 0xFFFF,
        0x7FFE, 0x0000, 0x0920, 0x0540, 0x0380, 0x0540, 0x0920, 0x0000,
    ],
    "storm": [
        0x03C0, 0x0FF0, 0x1C38, 0x1008, 0x700E, 0xC003, 0x8001, 0xFFFF,
        0x7FFE, 0x0180, 0x0300, 0x07E0, 0x0060, 0x00C0, 0x0080, 0x0000,
    ],
    "fog": [
        0x0000, 0x0000, 0x0000, 0x7FFE, 0x0000, 0x0000, 0x3FFC, 0x0000,
        0x0000, 0x7FFE, 0x0000, 0x0000, 0x3FFC, 0x0000, 0x0000, 0x0000,
    ],
}

WMO_TO_ICON = {
    0: ("sun", "moon"),    1: ("cloud", "moon"),   2: ("cloud", "cloud"),
    3: ("cloud", "cloud"), 45: ("fog", "fog"),     48: ("fog", "fog"),
    51: ("rain", "rain"),  53: ("rain", "rain"),   55: ("rain", "rain"),
    61: ("rain", "rain"),  63: ("rain", "rain"),   65: ("rain", "rain"),
    71: ("snow", "snow"),  73: ("snow", "snow"),   75: ("snow", "snow"),
    77: ("snow", "snow"),  80: ("rain", "rain"),   81: ("rain", "rain"),
    82: ("rain", "rain"),  85: ("snow", "snow"),   86: ("snow", "snow"),
    95: ("storm", "storm"), 96: ("storm", "storm"), 99: ("storm", "storm"),
}

ICON_COLORS = {
    "sun": (255, 255, 0),      "moon": (180, 180, 255),
    "cloud": (200, 200, 200),  "fog": (140, 140, 140),
    "rain": (50, 100, 255),    "snow": (220, 230, 255),
    "storm": (255, 50, 255),
}

ICON_CHARS = {
    "sun": "*",    "moon": ")",
    "cloud": "~",  "fog": "=",
    "rain": "//",  "snow": "**",
    "storm": "!",
}


def fetch_weather(lat: float = 38.895, lon: float = -77.264) -> dict:
    """Fetch current temperature + conditions + hourly forecast from Open-Meteo."""
    url = (
        f"https://api.open-meteo.com/v1/forecast?"
        f"latitude={lat}&longitude={lon}"
        f"&current=temperature_2m,weather_code,is_day"
        f"&hourly=temperature_2m,uv_index"
        f"&temperature_unit=fahrenheit"
        f"&timezone=America%2FNew_York"
        f"&forecast_days=1"
    )
    try:
        with urllib.request.urlopen(url, timeout=10) as resp:
            data = json.loads(resp.read())
            cur = data["current"]
            temp = int(round(cur["temperature_2m"]))
            code = cur.get("weather_code", 0)
            is_day = cur.get("is_day", 1)
            day_icon, night_icon = WMO_TO_ICON.get(code, ("cloud", "cloud"))
            icon = day_icon if is_day else night_icon
            hourly_temps = [round(t) for t in data.get("hourly", {}).get("temperature_2m", [])]
            hourly_uv = data.get("hourly", {}).get("uv_index", [])
            return {"temp": temp, "icon": icon, "code": code, "is_day": is_day,
                    "hourly": hourly_temps, "hourly_uv": hourly_uv}
    except Exception as e:
        print(f"Weather fetch failed: {e}")
        return {"temp": 70, "icon": "???", "code": 0, "is_day": 1, "hourly": [], "hourly_uv": []}


def fetch_alerts(lat: float = 38.895, lon: float = -77.264) -> list[dict]:
    """Fetch active NWS severe weather alerts for the given location."""
    url = f"https://api.weather.gov/alerts/active?point={lat},{lon}&status=actual"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "led-sign-controller/1.0"})
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read())
            alerts = []
            for f in data.get("features", []):
                p = f["properties"]
                alerts.append({
                    "event": p.get("event", ""),
                    "headline": p.get("headline", ""),
                    "severity": p.get("severity", ""),
                    "urgency": p.get("urgency", ""),
                    "expires": p.get("expires", ""),
                })
            return alerts
    except Exception as e:
        print(f"Alert fetch failed: {e}")
        return []


ALERT_COLORS = {
    "Extreme": (255, 0, 0),
    "Severe": (255, 60, 0),
    "Moderate": (255, 165, 0),
    "Minor": (255, 255, 0),
}


def render_alert_screen(alert: dict) -> Image.Image:
    """Render a full-screen weather alert (flashes on screen briefly)."""
    img = Image.new("RGB", (SIGN_W, SIGN_H), (0, 0, 0))
    draw = ImageDraw.Draw(img)

    severity = alert.get("severity", "Moderate")
    color = ALERT_COLORS.get(severity, (255, 165, 0))
    event = alert.get("event", "ALERT")

    # Shorten to fit 96px — severity is conveyed by color, not text
    short = event.replace("Severe ", "").replace("Thunderstorm ", "TSTORM ")
    short = short.replace("Tornado ", "TORNADO ").replace("Winter Storm ", "WSTORM ")
    short = short.replace("Warning", "WARN").replace("Advisory", "ADVSY")
    short = short.replace("Watch", "WATCH").upper()

    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 11)
        font_sm = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 9)
    except (OSError, IOError):
        font = ImageFont.load_default()
        font_sm = font

    # Top border
    for x in range(SIGN_W):
        img.putpixel((x, 0), color)
        if VISIBLE_H - 1 < SIGN_H:
            img.putpixel((x, VISIBLE_H - 1), color)

    # Alert icon: "!" triangle (simple)
    draw.text((2, 2), "!", fill=color, font=font)

    # Event text
    bbox = draw.textbbox((0, 0), short, font=font_sm)
    text_w = bbox[2] - bbox[0]
    tx = max(12, (SIGN_W - text_w) // 2)
    ty = (VISIBLE_H - (bbox[3] - bbox[1])) // 2 - bbox[1]
    draw.text((tx, ty), short, fill=color, font=font_sm)

    return img


# ---------------------------------------------------------------------------
# Breaking news alerts
# ---------------------------------------------------------------------------

NEWS_INCLUDE = [
    "supreme court", "scotus", "ruling", "decision", "opinion issued",
    "indictment", "indicted", "conviction", "convicted", "verdict", "sentenced",
    "executive order", "veto", "impeach",
    "passed by", "signed into law", "legislation",
    "breaking", "emergency", "developing",
    "mass shooting", "active shooter", "gunman",
    "attack", "explosion", "bombing",
    "earthquake", "hurricane", "tornado", "wildfire", "tsunami",
    "recession", "market halt",
    "invasion", "ceasefire", "treaty signed",
    "antitrust", "cybersecurity", "data breach", "hack",
    "nasa", "launches", "space station",
]

NEWS_EXCLUDE = [
    "sport", "nfl", "nba", "mlb", "nhl", "ncaa", "playoff", "super bowl",
    "world series", "champions league", "premier league", "world cup",
    "celebrity", "kardashian", "reality tv",
    "box office", "movie", "album release",
    "remembered", "memorial", "funeral", "vigil", "tribute", "anniversary of",
    "victims identified", "community mourns", "laid to rest",
]

_SEEN_HEADLINES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".seen_headlines.json")

def _load_seen_headlines() -> tuple[set, dict]:
    try:
        with open(_SEEN_HEADLINES_FILE) as f:
            data = json.load(f)
            cutoff = time.time() - 86400
            fresh = {h: ts for h, ts in data.items() if ts > cutoff}
            return set(fresh.keys()), fresh
    except Exception:
        return set(), {}

def _save_seen_headlines(seen_ts: dict):
    try:
        cutoff = time.time() - 86400
        fresh = {h: ts for h, ts in seen_ts.items() if ts > cutoff}
        with open(_SEEN_HEADLINES_FILE, "w") as f:
            json.dump(fresh, f)
    except Exception:
        pass

_seen_headlines, _seen_headlines_ts = _load_seen_headlines()

def fetch_breaking_news() -> list[str]:
    """Fetch headlines from Google News RSS, filtered for breaking/important stories."""
    import xml.etree.ElementTree as ET
    try:
        req = urllib.request.Request(
            "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en",
            headers={"User-Agent": "LEDSign/1.0"}
        )
        with urllib.request.urlopen(req, timeout=10) as resp:
            root = ET.fromstring(resp.read())
            titles = [item.text for item in root.findall(".//item/title") if item.text]
    except Exception as e:
        print(f"News fetch failed: {e}")
        return []

    results = []
    for title in titles[:20]:
        lower = title.lower()
        if any(ex in lower for ex in NEWS_EXCLUDE):
            continue
        if any(inc in lower for inc in NEWS_INCLUDE):
            h = hashlib.md5(title.encode()).hexdigest()[:8]
            if h not in _seen_headlines:
                _seen_headlines.add(h)
                _seen_headlines_ts[h] = time.time()
                # Strip " - Source Name" suffix from Google News titles
                clean = title.rsplit(" - ", 1)[0] if " - " in title else title
                results.append(clean)
    if results:
        _save_seen_headlines(_seen_headlines_ts)
    return results


WEATHER_ICON_PX = {
    "sun": [
        "......#......",
        "..#...#...#..",
        "...#.....#...",
        "....#####....",
        "...##...##...",
        "#.##.....##.#",
        "###.......###",
        "#.##.....##.#",
        "...##...##...",
        "....#####....",
        "...#.....#...",
        "..#...#...#..",
        "......#......",
    ],
    "moon": [
        "....#####....",
        "...###..##...",
        "..###....##..",
        ".####.....#..",
        ".####.....#..",
        "######.......",
        "######.......",
        "######.......",
        ".####.....#..",
        ".####.....#..",
        "..###....##..",
        "...###..##...",
        "....#####....",
    ],
    "cloud": [
        ".............",
        "....####.....",
        "..##....##...",
        ".#........#..",
        ".#........#..",
        "#..........#.",
        "#..........#.",
        ".############",
        "..##########.",
        ".............",
    ],
    "rain": [
        "....####.....",
        "..##....##...",
        ".#........#..",
        "#..........#.",
        "#############",
        ".###########.",
        ".............",
        ".#..#..#..#..",
        "..#..#..#..#.",
        ".#..#..#..#..",
    ],
    "snow": [
        "....####.....",
        "..##....##...",
        ".#........#..",
        "#..........#.",
        "#############",
        ".###########.",
        ".............",
        "..#...#...#..",
        "...#.#.#.#...",
        "..#...#...#..",
    ],
    "storm": [
        "....####.....",
        "..##....##...",
        ".#........#..",
        "#############",
        ".###########.",
        "......##.....",
        ".....##......",
        "....####.....",
        "......##.....",
        ".......#.....",
    ],
    "fog": [
        ".............",
        "#############",
        ".............",
        ".###########.",
        ".............",
        "#############",
        ".............",
        ".###########.",
        ".............",
        ".............",
    ],
}


VISIBLE_H = 16
CLOCK_H = 11
SPARK_H = 5
SPARK_Y = CLOCK_H

def render_clock_face(time_str: str, temp_str: str, icon_name: str,
                      time_color: tuple, temp_clr: tuple, icon_color: tuple,
                      hourly_temps: list = None, current_hour: int = 0,
                      forecast_style: str = "strip") -> Image.Image:
    """Render clock face with optional forecast visualization.
    forecast_style: 'strip', 'sparkline', or 'none' (full-size clock)."""
    img = Image.new("RGB", (SIGN_W, SIGN_H), (0, 0, 0))
    draw = ImageDraw.Draw(img)

    content_h = VISIBLE_H if forecast_style == "none" else CLOCK_H

    if forecast_style == "none":
        font_size, font_sm_size, font_ampm_size = 16, 13, 9
    else:
        font_size, font_sm_size, font_ampm_size = 14, 11, 8

    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", font_size)
        font_sm = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", font_sm_size)
        font_ampm = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", font_ampm_size)
    except (OSError, IOError):
        font = ImageFont.load_default()
        font_sm = font
        font_ampm = font

    if "AM" in time_str:
        t_part, ampm = time_str.replace("AM", ""), "AM"
    elif "PM" in time_str:
        t_part, ampm = time_str.replace("PM", ""), "PM"
    else:
        t_part, ampm = time_str, ""

    # Split time at colon for custom colon dots closer to center
    if ":" in t_part:
        hour_str, min_str = t_part.split(":", 1)
    else:
        hour_str, min_str = t_part, ""

    bbox_hour = draw.textbbox((0, 0), hour_str, font=font)
    hour_w = bbox_hour[2] - bbox_hour[0]
    bbox_min = draw.textbbox((0, 0), min_str, font=font) if min_str else (0, 0, 0, 0)
    min_w = bbox_min[2] - bbox_min[0]
    colon_w = 3  # 1px dot + 1px gap + 1px implicit
    time_w = hour_w + colon_w + min_w

    bbox_ampm = draw.textbbox((0, 0), ampm, font=font_ampm) if ampm else (0, 0, 0, 0)
    ampm_w = bbox_ampm[2] - bbox_ampm[0]

    bbox_temp = draw.textbbox((0, 0), temp_str, font=font_sm)
    temp_w = bbox_temp[2] - bbox_temp[0]

    icon_px = WEATHER_ICON_PX.get(icon_name, WEATHER_ICON_PX.get("cloud"))
    icon_w = len(icon_px[0]) if icon_px else 0
    icon_h = len(icon_px) if icon_px else 0

    gap = 4
    total = time_w + ampm_w + 1 + gap + icon_w + gap + temp_w
    x_start = max(0, (SIGN_W - total) // 2)

    # Draw time — centered in top content_h rows
    time_h = bbox_hour[3] - bbox_hour[1]
    ty = (content_h - time_h) // 2 - bbox_hour[1]

    # Draw hour digits
    draw.text((x_start, ty), hour_str, fill=time_color, font=font)

    # Draw custom colon dots (closer to vertical center)
    colon_x = x_start + hour_w + 1
    center_y = content_h // 2
    dot_offset = max(2, content_h // 5)
    img.putpixel((colon_x, center_y - dot_offset), time_color)
    img.putpixel((colon_x, center_y + dot_offset - 1), time_color)

    # Draw minute digits
    if min_str:
        draw.text((x_start + hour_w + colon_w, ty), min_str, fill=time_color, font=font)

    if ampm:
        ampm_h = bbox_ampm[3] - bbox_ampm[1]
        ampm_y = ty + time_h - ampm_h + bbox_hour[1] - bbox_ampm[1]
        draw.text((x_start + time_w + 1, ampm_y), ampm, fill=time_color, font=font_ampm)

    # Draw icon — centered in content area
    ix = x_start + time_w + ampm_w + 1 + gap
    iy = max(0, (content_h - icon_h) // 2)
    if icon_px:
        for row_i, row in enumerate(icon_px):
            for col_i, ch in enumerate(row):
                if ch == "#":
                    px = ix + col_i
                    py = iy + row_i
                    if 0 <= px < SIGN_W and 0 <= py < content_h:
                        img.putpixel((px, py), icon_color)

    # Draw temperature
    tx = ix + icon_w + gap
    temp_h = bbox_temp[3] - bbox_temp[1]
    tty = (content_h - temp_h) // 2 - bbox_temp[1]
    draw.text((tx, tty), temp_str, fill=temp_clr, font=font_sm)

    _draw_forecast(img, hourly_temps, current_hour, forecast_style)

    return img


def _draw_forecast(img, hourly_temps, current_hour, style="strip"):
    """Draw forecast visualization in the bottom rows of the clock image."""
    if not hourly_temps or len(hourly_temps) < 24:
        return

    t_low = min(hourly_temps[:24])
    t_high = max(hourly_temps[:24])
    t_range = max(t_high - t_low, 1)
    current_temp = hourly_temps[min(current_hour, 23)]

    if style == "strip":
        # Gradient strip: left=low, right=high, white marker = current temp
        strip_y = VISIBLE_H - 2
        strip_h = 2
        for x in range(SIGN_W):
            frac = x / (SIGN_W - 1)
            t_at_x = t_low + frac * t_range
            c = temp_color(int(t_at_x))
            for y in range(strip_y, strip_y + strip_h):
                img.putpixel((x, y), (c[0] // 3, c[1] // 3, c[2] // 3))
        now_frac = (current_temp - t_low) / t_range
        now_x = max(1, min(SIGN_W - 2, int(now_frac * (SIGN_W - 1))))
        for y in range(strip_y, strip_y + strip_h):
            for dx in (-1, 0, 1):
                if 0 <= now_x + dx < SIGN_W:
                    img.putpixel((now_x + dx, y), (255, 255, 255))

    elif style == "sparkline":
        # Time-series: left=midnight, right=11pm, smooth interpolated curve
        strip_y = CLOCK_H
        strip_h = SPARK_H
        for x in range(SIGN_W):
            hour_f = x / SIGN_W * 24
            h0 = int(hour_f)
            h1 = min(h0 + 1, 23)
            frac_h = hour_f - h0
            temp_val = hourly_temps[h0] * (1 - frac_h) + hourly_temps[h1] * frac_h
            frac = (temp_val - t_low) / t_range
            row = strip_y + strip_h - 1 - round(frac * (strip_h - 1))
            c = temp_color(int(temp_val))
            for fill_y in range(row, strip_y + strip_h):
                if fill_y < VISIBLE_H:
                    if fill_y == row:
                        img.putpixel((x, fill_y), c)
                    else:
                        img.putpixel((x, fill_y), (c[0] // 4, c[1] // 4, c[2] // 4))
        px_per_hour = SIGN_W / 24
        now_x = int(current_hour * px_per_hour + px_per_hour / 2)
        for y in range(strip_y, min(strip_y + strip_h, VISIBLE_H)):
            if 0 <= now_x < SIGN_W:
                img.putpixel((now_x, y), (255, 255, 255))


def render_forecast_fullscreen(hourly_temps: list, current_hour: int,
                               time_str: str = "", temp_now: int = None,
                               icon_name: str = "",
                               hourly_uv: list = None) -> Image.Image:
    """Full-screen 24h sparkline using all 16 visible rows."""
    img = Image.new("RGB", (SIGN_W, SIGN_H), (0, 0, 0))
    if not hourly_temps or len(hourly_temps) < 24:
        return img

    t_low = min(hourly_temps[:24])
    t_high = max(hourly_temps[:24])
    t_range = max(t_high - t_low, 1)
    px_per_hour = SIGN_W / 24

    # Interpolate temp for each pixel column for smooth curve
    for x in range(SIGN_W):
        hour_f = x / SIGN_W * 24
        h0 = int(hour_f)
        h1 = min(h0 + 1, 23)
        frac_h = hour_f - h0
        temp_val = hourly_temps[h0] * (1 - frac_h) + hourly_temps[h1] * frac_h
        frac = (temp_val - t_low) / t_range
        row = VISIBLE_H - 1 - round(frac * (VISIBLE_H - 1))
        c = temp_color(int(temp_val))
        for fill_y in range(row, VISIBLE_H):
            if fill_y == row:
                img.putpixel((x, fill_y), c)
            else:
                img.putpixel((x, fill_y), (c[0] // 4, c[1] // 4, c[2] // 4))

    # White vertical line at current hour
    now_x = int(current_hour * px_per_hour + px_per_hour / 2)
    for y in range(VISIBLE_H):
        if 0 <= now_x < SIGN_W:
            img.putpixel((now_x, y), (255, 255, 255))

    # UV peak — find highest 3-hour window, label drawn later
    UV_COLOR = (180, 0, 255)
    _uv_peak = None
    if hourly_uv and len(hourly_uv) >= 24:
        best_sum, best_start = -1, 0
        for h in range(22):
            s = sum(hourly_uv[h:h+3])
            if s > best_sum:
                best_sum, best_start = s, h
        peak_uv = max(hourly_uv[best_start:best_start+3])
        if peak_uv >= 1:
            px_per_hour = SIGN_W / 24
            uv_cx = int((best_start + 1.5) * px_per_hour)
            _uv_peak = (int(peak_uv), uv_cx)

    # Label high/low temps — hand-drawn 3x5 pixel digits
    TINY_DIGITS = {
        '0': ["###", "# #", "# #", "# #", "###"],
        '1': [" # ", "## ", " # ", " # ", "###"],
        '2': ["###", "  #", "###", "#  ", "###"],
        '3': ["###", "  #", "###", "  #", "###"],
        '4': ["# #", "# #", "###", "  #", "  #"],
        '5': ["###", "#  ", "###", "  #", "###"],
        '6': ["###", "#  ", "###", "# #", "###"],
        '7': ["###", "  #", "  #", "  #", "  #"],
        '8': ["###", "# #", "###", "# #", "###"],
        '9': ["###", "# #", "###", "  #", "###"],
        ':': ["#", " ", "#", " ", " "],  # 1px wide colon
    }
    def draw_tiny_num(img, num_str, x, y, color):
        cx = x
        for ch in num_str:
            glyph = TINY_DIGITS.get(ch)
            if not glyph:
                continue
            glyph_w = len(glyph[0])
            for ry, row in enumerate(glyph):
                for rx, pixel in enumerate(row):
                    if pixel == '#':
                        px, py = cx + rx, y + ry
                        if 0 <= px < SIGN_W and 0 <= py < VISIBLE_H:
                            img.putpixel((px, py), color)
            cx += glyph_w + 1

    def tiny_str_w(s):
        if not s:
            return 0
        w = 0
        for ch in s:
            g = TINY_DIGITS.get(ch)
            w += (len(g[0]) if g else 0) + 1
        return w - 1  # no trailing gap

    hi_str = f"{int(t_high)}"
    lo_str = f"{int(t_low)}"
    hi_c = temp_color(int(t_high))
    lo_c = temp_color(int(t_low))
    hi_w = tiny_str_w(hi_str)
    lo_w = tiny_str_w(lo_str)
    # Black background rectangles for high/low
    for label, lx, ly, color in [
        (hi_str, SIGN_W - hi_w - 1, 0, hi_c),
        (lo_str, SIGN_W - lo_w - 1, VISIBLE_H - 5, lo_c),
    ]:
        w = tiny_str_w(label) + 1
        img.paste((0,0,0), (lx - 1, ly, lx + w, ly + 6))
        draw_tiny_num(img, label, lx, ly, color)

    # Mini status overlay: time + icon + temp on one line
    TINY_ICONS = {
        "sun":   ["  #  ", "# # #", " ### ", "# # #", "  #  "],
        "moon":  [" ##  ", "  ## ", "  ## ", "  ## ", " ##  "],
        "cloud": ["     ", " ### ", "#####", "#####", " ### "],
        "rain":  [" ### ", "#####", " # # ", "# # #", "     "],
        "snow":  [" # # ", "  #  ", "# # #", "  #  ", " # # "],
        "storm": ["  #  ", " ##  ", "#### ", " ##  ", "#    "],
        "fog":   ["#####", "     ", "#####", "     ", "#####"],
    }
    if time_str or temp_now is not None:
        cx = 1
        parts_w = 0
        if time_str:
            parts_w += tiny_str_w(time_str) + 2
        if icon_name:
            parts_w += 7  # 5px icon + 2px gap
        if temp_now is not None:
            parts_w += tiny_str_w(f"{int(temp_now)}")
        # Black background
        img.paste((0,0,0), (0, 0, parts_w + 2, 6))

        if time_str:
            draw_tiny_num(img, time_str, cx, 0, (50, 130, 255))
            cx += tiny_str_w(time_str) + 2

        if icon_name:
            tiny_icon = TINY_ICONS.get(icon_name, TINY_ICONS.get("cloud"))
            if tiny_icon:
                ic = ICON_COLORS.get(icon_name, (200, 200, 200))
                for ry, row in enumerate(tiny_icon):
                    for rx, ch in enumerate(row):
                        if ch == '#':
                            px, py = cx + rx, ry
                            if 0 <= px < SIGN_W and 0 <= py < VISIBLE_H:
                                img.putpixel((px, py), ic)
                cx += 7

        if temp_now is not None:
            temp_s = f"{int(temp_now)}"
            draw_tiny_num(img, temp_s, cx, 0, temp_color(int(temp_now)))

    # UV peak label — "UV #" in purple centered at peak hours
    if _uv_peak:
        uv_val, uv_cx = _uv_peak
        uv_str = f"{uv_val}"
        # "UV" as pixel art (2 chars × 4px + number)
        UV_GLYPHS = {
            'U': ["# #", "# #", "# #", "# #", "###"],
            'V': ["# #", "# #", "# #", " # ", " # "],
        }
        full_w = 4 + 4 + 1 + tiny_str_w(uv_str)  # U + V + space + number
        lx = max(0, min(SIGN_W - full_w - 1, uv_cx - full_w // 2))
        ly = VISIBLE_H - 6
        img.paste((0,0,0), (lx - 1, ly - 1, lx + full_w + 1, ly + 6))
        cx = lx
        for ch in "UV":
            glyph = UV_GLYPHS[ch]
            for ry, row in enumerate(glyph):
                for rx, pixel in enumerate(row):
                    if pixel == '#':
                        px, py = cx + rx, ly + ry
                        if 0 <= px < SIGN_W and 0 <= py < VISIBLE_H:
                            img.putpixel((px, py), UV_COLOR)
            cx += 4
        cx += 1
        draw_tiny_num(img, uv_str, cx, ly, UV_COLOR)

    return img


def cmd_clock(args):
    """Display live time + temperature, updating every minute.
    Press 's' for small (with weather icon), 'b' for big, 'q' to quit."""
    import select
    import termios
    import tty
    from datetime import datetime

    display_size = 'color'
    forecast_style = getattr(args, 'forecast', 'none')
    print(f"Live clock+temp mode (updating every {args.interval}s)")
    print(f"  Keys: [c]olor  [b]ig  [s]mall  [f]orecast toggle  [q]uit")
    print(f"  Current mode: {display_size}, forecast: {forecast_style}")

    weather = None
    last_weather_fetch = 0
    alerts = []
    last_alert_fetch = 0
    last_alert_shown = 0
    last_forecast_flash = 0
    last_news_fetch = 0
    news_queue = []
    last_watchdog = 0

    interactive = sys.stdin.isatty()
    old_settings = None
    if interactive:
        old_settings = termios.tcgetattr(sys.stdin)
        tty.setcbreak(sys.stdin.fileno())

    try:
        last_minute_sent = -1
        force_send = True
        while True:
            if interactive and select.select([sys.stdin], [], [], 0)[0]:
                ch = sys.stdin.read(1).lower()
                if ch == 'q':
                    print("\nStopped.")
                    return
                elif ch == 'c':
                    display_size = 'color'
                    print(f"\n  → Color bitmap mode ({forecast_style} forecast)")
                    force_send = True
                elif ch == 'b':
                    display_size = 'big'
                    print(f"\n  → Big mode")
                    force_send = True
                elif ch == 's':
                    display_size = 'small'
                    print(f"\n  → Small mode (with weather icon)")
                    force_send = True
                elif ch == 'f':
                    cycle = ['strip', 'sparkline', 'none']
                    forecast_style = cycle[(cycle.index(forecast_style) + 1) % len(cycle)]
                    print(f"\n  → Forecast: {forecast_style}")
                    if display_size == 'color':
                        force_send = True

            # Watchdog: every 2 min, force redraw to reclaim from remote presets
            if time.time() - last_watchdog > 120:
                last_watchdog = time.time()
                force_send = True

            now = datetime.now()
            current_minute = now.hour * 60 + now.minute
            if not force_send and current_minute == last_minute_sent:
                time.sleep(0.2)
                continue
            if not force_send and now.second > 2:
                time.sleep(0.2)
                continue

            now = datetime.now()
            time_str = f"{now.strftime('%-I:%M')}"
            ampm = now.strftime("%p")

            if weather is None or (time.time() - last_weather_fetch) > 120:
                weather = fetch_weather(args.lat, args.lon)
                last_weather_fetch = time.time()
                print(f"  Weather: {weather['temp']}F {weather['icon']} (code {weather['code']}, {'day' if weather['is_day'] else 'night'})")

            if time.time() - last_alert_fetch > 600:
                alerts = fetch_alerts(args.lat, args.lon)
                last_alert_fetch = time.time()
                if alerts:
                    for a in alerts:
                        print(f"  ALERT: {a['event']} ({a['severity']})")

            # Flash severe alert every 5 minutes
            severe = [a for a in alerts if a["severity"] in ("Severe", "Extreme")]
            if severe and (time.time() - last_alert_shown > 300):
                alert_img = render_alert_screen(severe[0])
                rt_draw_color_image(alert_img, args.bridge, max_colors=4)
                last_alert_shown = time.time()
                print(f"  ⚡ ALERT FLASH: {severe[0]['event']}")
                time.sleep(4)

            t_color = temp_color(weather["temp"])
            icon_name = weather["icon"]
            # Override icon when severe weather alerts are active
            if any(a["severity"] in ("Severe", "Extreme") for a in alerts):
                storm_events = [a["event"].lower() for a in alerts]
                if any("tornado" in e for e in storm_events):
                    icon_name = "storm"
                elif any("thunderstorm" in e or "storm" in e for e in storm_events):
                    icon_name = "storm"
                elif any("snow" in e or "ice" in e or "blizzard" in e for e in storm_events):
                    icon_name = "snow"
                elif any("rain" in e or "flood" in e for e in storm_events):
                    icon_name = "rain"

            # Flash full-screen forecast every 5 minutes for 10 seconds
            hourly = weather.get("hourly", [])
            if hourly and len(hourly) >= 24 and (time.time() - last_forecast_flash > 300):
                if last_forecast_flash > 0:  # skip on first loop
                    fc_time = f"{now.strftime('%-I')}:{now.strftime('%M')}"
                    forecast_img = render_forecast_fullscreen(
                        hourly, now.hour, time_str=fc_time,
                        temp_now=weather["temp"], icon_name=icon_name,
                        hourly_uv=weather.get("hourly_uv", []))
                    rt_draw_color_image(forecast_img, args.bridge, max_colors=12)
                    print(f"  📊 Forecast flash (10s)")
                    time.sleep(10)
                    force_send = True  # redraw clock after
                last_forecast_flash = time.time()
            # Breaking news — check every 5 min, scroll new headlines
            if time.time() - last_news_fetch > 300:
                new_headlines = fetch_breaking_news()
                if new_headlines:
                    news_queue.extend(new_headlines)
                    print(f"  📰 {len(new_headlines)} new headline(s)")
                last_news_fetch = time.time()
            if news_queue:
                headline = news_queue.pop(0)
                print(f"  📰 SCROLL: {headline}")
                # At speed 8, each pixel col ~30ms, ~8px/char → ~0.24s/char
                # Use 0.3s/char for the wait, but set duration much higher so
                # the sign's internal timer never triggers a second loop
                scroll_wait = max(int(len(headline) * 0.3), 8)
                scroll_duration = scroll_wait * 3  # generous — sign stops before looping
                segments = [(headline + "          ", (255, 100, 0))]
                pkts = build_text_program(
                    "", font_size=14, scroll="left", speed=8,
                    duration=scroll_duration,
                    segments=segments, font_family=0x00)
                for pkt in pkts:
                    send_packet(pkt, args.bridge)
                    time.sleep(0.15)
                time.sleep(scroll_wait)
                force_send = True

            icon_clr = ICON_COLORS.get(icon_name, (200, 200, 200))
            icon_char = ICON_CHARS.get(icon_name, "?")
            time_color = (50, 130, 255)

            temp_str = f"{weather['temp']}F"

            if display_size == 'big':
                segments = [
                    (f"{time_str}{ampm} ", time_color),
                    (f"{temp_str}", t_color),
                ]
                packets = build_text_program(
                    "", font_size=16, scroll="static",
                    segments=segments, font_family=0x00,
                )
                print(f"  [{now.strftime('%H:%M:%S')}] {time_str}{ampm} {temp_str} [big]")
                ok = False
                for attempt in range(3):
                    try:
                        for pkt in packets:
                            send_packet(pkt, args.bridge)
                            time.sleep(0.15)
                        ok = True
                        break
                    except Exception as e:
                        if attempt < 2:
                            print(f"  Send failed (attempt {attempt+1}): {e}, retrying...")
                            time.sleep(1)
                        else:
                            print(f"  Send failed after 3 attempts: {e}")
            elif display_size == 'color':
                hourly = weather.get("hourly", [])
                clock_img = render_clock_face(
                    time_str + ampm, temp_str, icon_name,
                    time_color, t_color, icon_clr,
                    hourly_temps=hourly, current_hour=now.hour,
                    forecast_style=forecast_style,
                )
                hi = max(hourly[:24]) if len(hourly) >= 24 else "?"
                lo = min(hourly[:24]) if len(hourly) >= 24 else "?"
                print(f"  [{now.strftime('%H:%M:%S')}] {time_str}{ampm} {icon_name} {temp_str} H:{hi} L:{lo} [color]")
                ok = False
                for attempt in range(3):
                    try:
                        rt_draw_color_image(clock_img, args.bridge, max_colors=12)
                        ok = True
                        break
                    except Exception as e:
                        if attempt < 2:
                            print(f"  Send failed (attempt {attempt+1}): {e}, retrying...")
                            time.sleep(1)
                        else:
                            print(f"  Send failed after 3 attempts: {e}")
            else:
                segments = [
                    (f"{time_str}{ampm} ", time_color),
                    (f"{icon_char} ", icon_clr),
                    (f"{temp_str}", t_color),
                ]
                packets = build_text_program(
                    "", font_size=16, scroll="static",
                    segments=segments, font_family=0x02,
                )
                print(f"  [{now.strftime('%H:%M:%S')}] {time_str}{ampm} {icon_char} {temp_str} [small]")
                ok = False
                for attempt in range(3):
                    try:
                        for pkt in packets:
                            send_packet(pkt, args.bridge)
                            time.sleep(0.15)
                        ok = True
                        break
                    except Exception as e:
                        if attempt < 2:
                            print(f"  Send failed (attempt {attempt+1}): {e}, retrying...")
                            time.sleep(1)
                        else:
                            print(f"  Send failed after 3 attempts: {e}")

            if ok:
                last_minute_sent = current_minute
            force_send = False
    finally:
        if old_settings:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)


def main():
    parser = argparse.ArgumentParser(description="YSKJ LED Sign Controller")
    parser.add_argument("--bridge", default=BRIDGE_URL, help="Bridge URL")
    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("text", help="Display static text")
    p.add_argument("text", help="Text to display")
    p.add_argument("--size", type=int, default=18, help="Font size (default: 18)")
    p.add_argument("--preview", action="store_true", help="Preview in terminal")
    p.set_defaults(func=cmd_text)

    p = sub.add_parser("scroll", help="Display scrolling text (sign's built-in fonts)")
    p.add_argument("text", help="Text to scroll")
    p.add_argument("--size", type=int, default=16, help="Font size (default: 16)")
    p.add_argument("--direction", choices=["left", "right", "up", "down", "static"], default="left")
    p.add_argument("--speed", type=int, default=10, help="Scroll speed 1-20 (default: 10)")
    p.add_argument("--duration", type=int, default=0, help="Duration in seconds (0=loop)")
    p.set_defaults(func=cmd_scroll)

    p = sub.add_parser("image", help="Display an image")
    p.add_argument("file", help="Image file path")
    p.add_argument("--threshold", type=int, default=128, help="B/W threshold (default: 128)")
    p.add_argument("--invert", action="store_true", help="Invert image")
    p.add_argument("--preview", action="store_true", help="Preview in terminal")
    p.set_defaults(func=cmd_image)

    p = sub.add_parser("clear", help="Clear the display")
    p.set_defaults(func=cmd_clear)

    p = sub.add_parser("power", help="Power on/off")
    p.add_argument("state", choices=["on", "off"])
    p.set_defaults(func=cmd_power)

    p = sub.add_parser("brightness", help="Set brightness (0-15)")
    p.add_argument("level", type=int, choices=range(16))
    p.set_defaults(func=cmd_brightness)

    p = sub.add_parser("clock", help="Live time + temperature display")
    p.add_argument("--size", type=int, default=16, help="Font size (default: 16)")
    p.add_argument("--interval", type=int, default=60, help="Update interval in seconds (default: 60)")
    p.add_argument("--lat", type=float, default=38.895, help="Latitude (default: Vienna VA)")
    p.add_argument("--lon", type=float, default=-77.264, help="Longitude (default: Vienna VA)")
    p.add_argument("--preview", action="store_true", help="Preview in terminal")
    p.add_argument("--forecast", choices=["strip", "sparkline", "none"], default="none", help="Forecast style")
    p.set_defaults(func=cmd_clock)

    p = sub.add_parser("status", help="Show sign status")
    p.set_defaults(func=cmd_status)

    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        return

    args.func(args)


if __name__ == "__main__":
    main()
