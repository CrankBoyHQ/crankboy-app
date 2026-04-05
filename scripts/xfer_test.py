#!/usr/bin/env python3
"""CrankBoy Serial Transfer (ft - File Transfer) Test Script
Optimized protocol with window-based pipelining, adaptive batching, and selective retransmit

Features:
  - Dynamic window sizing: device advertises window size, host adapts
  - Adaptive batching: ACK frequency adjusts based on network stability
  - Selective retransmit: only missing chunks are retransmitted on timeout
  - Cumulative ACKs: one ACK acknowledges all chunks up to that sequence
  - Automatic GBZ compression for .gb and .gbc files (decompressed on device)
  - Pre-compressed .gbz files transferred as-is (kept compressed on device)
  - CRC16 verification for each chunk
  - Detailed error codes for better reliability
  - Progress bar for non-verbose mode
  - Optional restart after transfer
  - Artificial packet drop testing (--drop-rate option)

Protocol:
  ft:b:<filename>:<size>:<crc32>[:<orig_name>:<orig_crc>]  - Begin transfer
  ft:c:<seq>:<crc16>:<base64>                             - Send chunk
  ft:e:<crc32>                                            - End transfer
  ft:s                                                      - Query status
  cb:restart                                               - Restart CrankBoy

Device responses:
  ft:r:<code>          - Ready (format: WWCC hex, WW=window, CC=chunk)
                         Example: ft:r:08B1 = window=8, chunk=177
  ft:a:<seq>           - Cumulative ACK (acknowledges chunks 0 through seq)
  ft:n:<seq>:<code>    - NACK with error code (immediate, not batched):
                         "crc" = CRC mismatch (retry same chunk)
                         "seq" = wrong sequence (resync to seq)
                         "write" = write error (fatal)
                         "size" = size exceeded (fatal)
  ft:d:<base>:<bitmap> - Status response: window_base + bitmap of received chunks
                         bitmap bit 0 = window_base, bit 1 = window_base+1, etc.
  ft:o:<filename>      - OK - transfer complete (original filename for GBZ)
  ft:x:<code>          - Error with code:
                         "nomem" = out of memory
                         "gbz_header" = invalid GBZ header
                         "decompress" = decompression failed
                         "orig_crc" = original file CRC mismatch
  cb:restarting        - Restarting CrankBoy (response to cb:restart)

Window Protocol:
  - Device advertises window size in ft:r response (WW field)
  - Host sends up to <window_size> chunks without waiting for ACKs
  - Device buffers out-of-order chunks within the window
  - Device sends cumulative ACK for highest consecutive chunk received
  - Adaptive batching: starts at 3 chunks, increases to (window-2) after 5 good batches
  - On timeout, host queries status (ft:s) and selectively retransmits missing chunks
  - NACKs are sent immediately on CRC/sequence errors (resets batch size to 3)
  - Default window size: 8 (configurable on device via FT_WINDOW_SIZE define)

Testing:
  --drop-rate N        - Artificially drop N%% of packets to test error recovery
"""

import sys
import os
import base64
import zlib
import gzip
import time
import io
import serial
import argparse
import urllib.parse
import random

# Configuration
SERIAL_PORT = "/dev/cu.usbmodemPDU1_Y0096921"
BAUD_RATE = 115200
TIMEOUT = 5
# Note: Chunk size is fixed at 177 bytes (0xB1) by the device protocol

# Global verbose flag
VERBOSE = False


def calculate_crc32(filepath):
    """Calculate CRC32 of a file."""
    with open(filepath, 'rb') as f:
        data = f.read()
    return zlib.crc32(data) & 0xFFFFFFFF


def print_progress_bar(percent, width=40):
    """Print a simple progress bar."""
    filled = int(width * percent / 100)
    bar = '=' * filled + '>' + ' ' * (width - filled - 1)
    print(f"\r[{bar}] {percent:.1f}%", end='', flush=True)


def compress_to_gbz(data, is_gbc=True):
    """
    Compress ROM data to GBZ format.

    GBZ format:
    [0:8]   Magic: CB 00 FF 47 42 67 7A
    [8]     Version: 1
    [9]     is_gbc: 1 if .gbc, 0 if .gb
    [10:14] CRC32 of original (big-endian)
    [14:18] Original size (little-endian uint32)
    [18:46] ROM header bytes 0x134-0x14F (28 bytes)
    [46:0x150] 0xFF padding (80 bytes)
    [0x150:] gzip compressed data

    Returns: (gbz_data, original_crc, gbz_crc)
    """
    # Calculate CRC32 of original
    original_crc = zlib.crc32(data) & 0xFFFFFFFF

    # Extract ROM header bytes 0x134-0x14F (28 bytes)
    rom_header = data[0x134:0x150]

    # Compress data with gzip
    compressed = gzip.compress(data, compresslevel=9)

    # Build GBZ header
    gbz = bytearray()
    # Magic: "CB\x00\xFFGBgz" = 8 bytes (0x43, 0x42, 0x00, 0xFF, 0x47, 0x42, 0x67, 0x7A)
    gbz.extend(b'CB\x00\xFFGBgz')
    # Version
    gbz.append(1)
    # is_gbc
    gbz.append(1 if is_gbc else 0)
    # CRC32 (big-endian)
    gbz.extend(original_crc.to_bytes(4, 'big'))
    # Original size (little-endian uint32)
    gbz.extend(len(data).to_bytes(4, 'little'))
    # ROM header (28 bytes)
    gbz.extend(rom_header)
    # Padding 0xFF from 0x2E to 0x50 (82 bytes, but we've written 0x2E=46 so far)
    # Total header should be 0x150 = 336 bytes
    # We've written: 8+1+1+4+4+28 = 46 bytes
    padding_needed = 0x150 - len(gbz)
    gbz.extend(b'\xFF' * padding_needed)
    # Compressed data
    gbz.extend(compressed)

    gbz_data = bytes(gbz)
    # Calculate CRC32 of the GBZ file itself (for transfer verification)
    gbz_crc = zlib.crc32(gbz_data) & 0xFFFFFFFF

    return gbz_data, original_crc, gbz_crc


def send_command(ser, cmd):
    """Send a command with 'msg' prefix."""
    full_cmd = f"msg {cmd}\n"
    ser.write(full_cmd.encode('utf-8'))
    if VERBOSE:
        print(f"  → {cmd[:70]}{'...' if len(cmd) > 70 else ''}")


def read_response(ser, timeout=TIMEOUT):
    """Read a response line."""
    ser.timeout = timeout
    try:
        line = ser.readline()
        if not line:
            return None
        decoded = line.decode('utf-8', errors='ignore').strip()
        if not decoded:
            return None
        if VERBOSE:
            print(f"  ← {decoded[:70]}{'...' if len(decoded) > 70 else ''}")
        return decoded
    except serial.SerialTimeoutException:
        return None
    except serial.SerialException as e:
        if VERBOSE:
            print(f"  Serial error: {e}")
        return None


def parse_response(response):
    """Parse ft: protocol response."""
    if not response or not response.startswith("ft:"):
        return None, None, None

    parts = response.split(':')
    if len(parts) < 2:
        return None, None, None

    cmd = parts[1]
    params = ':'.join(parts[2:]) if len(parts) > 2 else ""
    return "ft", cmd, params


def wait_for_response(ser, expected_cmd, timeout=TIMEOUT):
    """Wait for a specific response command (or any ft: response if expected_cmd is None)."""
    if VERBOSE:
        if expected_cmd:
            print(f"  [waiting for ft:{expected_cmd}...]")
        else:
            print(f"  [waiting for response...]")
    start = time.time()
    while time.time() - start < timeout:
        response = read_response(ser, timeout=1)
        if not response:
            continue

        proto, cmd, params = parse_response(response)
        if proto == "ft":
            # If expected_cmd is None, return any ft: response (for cumulative ACK handling)
            if expected_cmd is None:
                return cmd, params
            if cmd == expected_cmd:
                return cmd, params
            if cmd == "x":
                if VERBOSE:
                    print(f"  ERROR: Device error code {params}")
                return cmd, params
            if cmd == "n":
                return cmd, params
    if VERBOSE:
        print("  TIMEOUT waiting for response")
    return None, None


def send_begin(ser, filename, filesize, file_crc, original_filename=None, original_crc=None):
    """Send ft:b (begin) command."""
    encoded_filename = urllib.parse.quote(filename, safe='')
    # Size is decimal, CRC is hex (without leading zeros)
    crc_hex = f"{file_crc:08X}"

    # Include original filename and CRC if provided (for GBZ files)
    if original_filename and original_crc is not None:
        encoded_original = urllib.parse.quote(original_filename, safe='')
        original_crc_hex = f"{original_crc:08X}"
        send_command(ser, f"ft:b:{encoded_filename}:{filesize}:{crc_hex}:{encoded_original}:{original_crc_hex}")
    else:
        send_command(ser, f"ft:b:{encoded_filename}:{filesize}:{crc_hex}")


def send_chunk(ser, seq, chunk_data):
    """Send ft:c (chunk) command with CRC16 verification."""
    seq_hex = f"{seq:04X}"
    # Calculate CRC32 and use lower 16 bits
    crc32 = zlib.crc32(chunk_data) & 0xFFFFFFFF
    crc16 = crc32 & 0xFFFF
    crc16_hex = f"{crc16:04X}"
    b64_data = base64.b64encode(chunk_data).decode('ascii')
    send_command(ser, f"ft:c:{seq_hex}:{crc16_hex}:{b64_data}")


def send_end(ser, file_crc):
    """Send ft:e (end) command."""
    crc_hex = f"{file_crc:08X}"
    send_command(ser, f"ft:e:{crc_hex}")


def send_restart(ser):
    """Send cb:restart command to restart CrankBoy."""
    send_command(ser, "cb:restart")


def send_status(ser):
    """Send ft:s (status) command."""
    send_command(ser, "ft:s")


def parse_status(params):
    """Parse status response params: window_base:bitmap (both 4-digit hex).
    Bitmap shows which chunks in window are received (bit 0 = window_base)."""
    parts = params.split(':')
    if len(parts) >= 2:
        try:
            window_base = int(parts[0], 16)
            bitmap = int(parts[1], 16)
            return window_base, bitmap
        except ValueError:
            pass
    return None, None


def parse_ack(params):
    """Parse ACK response params (just seq as 4-digit hex)."""
    try:
        return int(params, 16)
    except ValueError:
        return None


def parse_nack(params):
    """Parse NACK response params: seq:code (seq=4-digit hex, code=error string)."""
    parts = params.split(':')
    if len(parts) >= 1:
        try:
            seq = int(parts[0], 16)
            code = parts[1] if len(parts) > 1 else "unknown"
            return seq, code
        except ValueError:
            pass
    return None, "unknown"


def transfer_file(filepath, port=SERIAL_PORT, verbose=False, restart=False, drop_rate=0):
    """Transfer a single file to CrankBoy."""
    global VERBOSE
    VERBOSE = verbose

    # Get original file info
    filename = os.path.basename(filepath)
    original_ext = os.path.splitext(filepath)[1].lower()
    is_gbc = original_ext == '.gbc'

    # Check if user provided a pre-compressed GBZ file
    if original_ext == '.gbz':
        # User wants to transfer GBZ as-is, no decompression
        with open(filepath, 'rb') as f:
            gbz_data = f.read()
        gbz_size = len(gbz_data)
        gbz_crc = zlib.crc32(gbz_data) & 0xFFFFFFFF
        gbz_filename = filename
        original_filename = None
        original_crc = None
        original_size = gbz_size
        compression_ratio = 0
        is_user_gbz = True
    else:
        # Compress .gb or .gbc to GBZ format
        with open(filepath, 'rb') as f:
            original_data = f.read()
        original_size = len(original_data)
        original_crc = zlib.crc32(original_data) & 0xFFFFFFFF

        if VERBOSE:
            print(f"\n[Compressing to GBZ format...]")
        gbz_data, _, gbz_crc = compress_to_gbz(original_data, is_gbc=is_gbc)
        gbz_size = len(gbz_data)
        compression_ratio = (1 - gbz_size / original_size) * 100

        # Change filename to .gbz
        base_name = os.path.splitext(filename)[0]
        gbz_filename = f"{base_name}.gbz"
        original_filename = filename
        is_user_gbz = False

    print(f"\n{'='*60}")
    # Show original filename for .gb/.gbc, GBZ filename for pre-compressed
    display_name = original_filename if not is_user_gbz else gbz_filename
    print(f"Transferring: {display_name}")
    if is_user_gbz:
        print(f"Type: Pre-compressed GBZ (will be kept as-is)")
    elif VERBOSE:
        print(f"Original: {original_size} bytes ({original_size/1024:.1f} KB)")
        print(f"Compressed: {gbz_size} bytes ({gbz_size/1024:.1f} KB, {compression_ratio:.1f}% smaller)")
        print(f"Original CRC32: {original_crc:08x}")
        print(f"GBZ CRC32: {gbz_crc:08x}")
    print(f"{'='*60}")

    # Open serial port
    if VERBOSE:
        print(f"\n[Connecting to {port}...]")
    else:
        print(f"Connecting to {port}...")
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=TIMEOUT)
    except serial.SerialException as e:
        print(f"ERROR: Could not open serial port: {e}")
        return False

    ser.reset_input_buffer()
    ser.reset_output_buffer()

    # Send begin
    if VERBOSE:
        print("\n[Phase 1: Initiating transfer...]")
    # Only pass original filename/CRC if we compressed the file (not for user-provided GBZ)
    if is_user_gbz:
        send_begin(ser, gbz_filename, gbz_size, gbz_crc)
    else:
        send_begin(ser, gbz_filename, gbz_size, gbz_crc, original_filename, original_crc)

    cmd, params = wait_for_response(ser, "r", timeout=5)
    if cmd != "r":
        print("ERROR: Did not receive ready response")
        ser.close()
        return False

    # Parse window size and chunk size from 4-digit hex (format: WWCC)
    # WW = window size (2 hex digits), CC = chunk size (2 hex digits)
    try:
        ready_code = int(params, 16)
        window_size = (ready_code >> 8) & 0xFF  # High byte
        chunk_size = ready_code & 0xFF          # Low byte
        if VERBOSE:
            print(f"  Device ready, window size: {window_size}, chunk size: {chunk_size} bytes")
    except ValueError:
        window_size = 4
        chunk_size = 177
        if VERBOSE:
            print(f"  Using defaults, window size: {window_size}, chunk size: {chunk_size}")

    # Transfer chunks with window-based pipelining
    if VERBOSE:
        print(f"\n[Phase 2: Transferring data (window size: {window_size})...]")
    else:
        print("Transferring...")

    start_time = time.time()

    # Pre-chunk the data for window management
    chunks = []
    gbz_stream = io.BytesIO(gbz_data)
    while True:
        chunk = gbz_stream.read(chunk_size)
        if not chunk:
            break
        chunks.append(chunk)
    total_chunks = len(chunks)

    # Window-based transfer state
    next_seq_to_send = 0  # Next chunk to send (high watermark)
    highest_acked = -1    # Highest consecutive ACK received
    in_flight = {}        # seq -> (chunk_data, send_time, retry_count)

    def send_window():
        """Send chunks to fill the window."""
        nonlocal next_seq_to_send
        while next_seq_to_send < total_chunks and len(in_flight) < window_size:
            seq = next_seq_to_send
            chunk_data = chunks[seq]
            # Artificial packet drop for testing
            if drop_rate > 0 and random.random() < (drop_rate / 100.0):
                if VERBOSE:
                    print(f"  [ARTIFICIAL DROP: chunk {seq:04X}]")
                # Still add to in_flight but don't actually send
                # This simulates a lost packet
                in_flight[seq] = {
                    'data': chunk_data,
                    'time': time.time() - 0.6,  # Already "timed out"
                    'retries': 0
                }
                next_seq_to_send += 1
                continue
            send_chunk(ser, seq, chunk_data)
            in_flight[seq] = {
                'data': chunk_data,
                'time': time.time(),
                'retries': 0
            }
            next_seq_to_send += 1
            if VERBOSE and seq % 50 == 0:
                print(f"  [Sent chunk {seq:04X}, window: {len(in_flight)}/{window_size}]")

    def process_response(timeout=0.1):
        """Process one response (ACK or NACK). Returns True if response processed."""
        nonlocal highest_acked, next_seq_to_send
        cmd, params = wait_for_response(ser, None, timeout=timeout)
        if not cmd:
            return False

        if cmd == "a":
            # Cumulative ACK - acknowledges all chunks up to this seq
            ack_seq = parse_ack(params)
            if ack_seq is not None:
                if VERBOSE and ack_seq > highest_acked:
                    print(f"  [Cumulative ACK for chunk {ack_seq:04X}]")
                # Remove all chunks up to and including ack_seq from in_flight
                for seq in list(in_flight.keys()):
                    if seq <= ack_seq:
                        del in_flight[seq]
                if ack_seq > highest_acked:
                    highest_acked = ack_seq
            return True

        elif cmd == "n":
            # NACK - immediate error
            nack_seq, nack_code = parse_nack(params)
            if nack_seq is not None:
                if nack_code == "crc":
                    if VERBOSE:
                        print(f"  [NACK: CRC error for chunk {nack_seq:04X}]")
                    # Mark for retransmission
                    if nack_seq in in_flight:
                        in_flight[nack_seq]['retries'] += 1
                elif nack_code == "seq":
                    if VERBOSE:
                        print(f"  [NACK: Sequence error, resync to {nack_seq:04X}]")
                    # Device is asking for a specific chunk
                    # Remove anything beyond what device wants from in_flight
                    for seq in list(in_flight.keys()):
                        if seq >= nack_seq:
                            del in_flight[seq]
                    next_seq_to_send = nack_seq
                elif nack_code in ("write", "size"):
                    if VERBOSE:
                        print(f"  [Device error '{nack_code}' for chunk {nack_seq:04X}, will retry]")
                    # Don't abort, let retry logic handle it
            return True

        elif cmd == "x":
            # Device error - log but don't abort
            if VERBOSE:
                print(f"  [Device error: {params}, will retry]")
            return True

        return True  # Unknown command but not fatal

    # Main transfer loop
    chunks_completed = 0
    last_progress_print = 0

    while highest_acked < total_chunks - 1:
        # Fill the window
        send_window()

        # Wait for responses with timeout
        if in_flight:
            # Wait for cumulative ACK (continue on errors, let timeout logic handle retries)
            process_response(timeout=0.2)

            # Check for timeouts and retransmit using selective retransmit
            current_time = time.time()
            timeouts = [(seq, info) for seq, info in in_flight.items() if current_time - info['time'] > 0.5]
            if timeouts:
                # Check if this is the first timeout for these chunks or a repeated timeout
                first_timeouts = [(seq, info) for seq, info in timeouts if info['retries'] == 0]
                
                if first_timeouts:
                    # First timeout - just retransmit without status query (give device more time)
                    for seq, info in first_timeouts:
                        if info['retries'] < 5:
                            if VERBOSE:
                                print(f"  [Timeout: retransmitting chunk {seq:04X} (retry {info['retries']+1}/5)]")
                            send_chunk(ser, seq, info['data'])
                            in_flight[seq]['time'] = current_time
                            in_flight[seq]['retries'] += 1
                        else:
                            print(f"ERROR: Max retries exceeded for chunk {seq:04X}")
                            ser.close()
                            return False
                else:
                    # Repeated timeouts - use selective retransmit with status query
                    send_status(ser)
                    cmd, params = wait_for_response(ser, "d", timeout=1)
                    if cmd == "d":
                        window_base, bitmap = parse_status(params)
                        if window_base is not None and bitmap is not None:
                            if VERBOSE:
                                print(f"  [Status query: window_base={window_base:04X}, bitmap={bitmap:04X}]")
                            # Determine which chunks are missing
                            missing = []
                            highest_processed = -1
                            for seq, info in timeouts:
                                if seq < window_base:
                                    # Chunk is behind the window - device already processed it
                                    # Remove from in_flight without retransmitting
                                    if VERBOSE:
                                        print(f"  [Chunk {seq:04X} already processed, removing from in_flight]")
                                    if seq in in_flight:
                                        del in_flight[seq]
                                    # Track highest processed chunk
                                    if seq > highest_processed:
                                        highest_processed = seq
                                    continue
                                elif window_base <= seq < window_base + window_size:
                                    # Chunk is in current window - check bitmap
                                    bit_position = seq - window_base
                                    if bitmap & (1 << bit_position):
                                        # Chunk received, remove from in_flight
                                        if seq in in_flight:
                                            del in_flight[seq]
                                        continue
                                    else:
                                        # Chunk in window but not received
                                        missing.append((seq, info))
                                else:
                                    # Chunk is ahead of window - shouldn't happen but handle it
                                    missing.append((seq, info))
                            # Update highest_acked if we found processed chunks
                            if highest_processed > highest_acked:
                                highest_acked = highest_processed
                                if VERBOSE:
                                    print(f"  [Updated highest_acked to {highest_acked:04X}]")
                            
                            # Retransmit only missing chunks
                            for seq, info in missing:
                                if info['retries'] < 5:
                                    if VERBOSE:
                                        print(f"  [Selective retransmit chunk {seq:04X} (retry {info['retries']+1}/5)]")
                                    send_chunk(ser, seq, info['data'])
                                    in_flight[seq]['time'] = current_time
                                    in_flight[seq]['retries'] += 1
                                else:
                                    print(f"ERROR: Max retries exceeded for chunk {seq:04X}")
                                    ser.close()
                                    return False
                    else:
                        # Status query failed, fall back to retransmitting all timed out chunks
                        for seq, info in timeouts:
                            if info['retries'] < 5:
                                if VERBOSE:
                                    print(f"  [Timeout: retransmitting chunk {seq:04X} (retry {info['retries']+1}/5)]")
                                send_chunk(ser, seq, info['data'])
                                in_flight[seq]['time'] = current_time
                                in_flight[seq]['retries'] += 1
                            else:
                                print(f"ERROR: Max retries exceeded for chunk {seq:04X}")
                                ser.close()
                                return False
        else:
            # Window empty but not done - send more
            time.sleep(0.01)

        # Update progress
        chunks_completed = highest_acked + 1
        bytes_sent = sum(len(chunks[i]) for i in range(min(chunks_completed, total_chunks)))
        progress = (chunks_completed / total_chunks) * 100

        if VERBOSE:
            if chunks_completed - last_progress_print >= 50:
                elapsed = time.time() - start_time
                rate = bytes_sent / elapsed if elapsed > 0 else 0
                print(f"  Progress: {progress:.1f}% ({bytes_sent}/{gbz_size} bytes, {rate/1024:.1f} KB/s)")
                last_progress_print = chunks_completed
        else:
            # Update progress bar every few chunks
            if chunks_completed % 5 == 0 or chunks_completed >= total_chunks:
                print_progress_bar(progress)

    # Finish progress bar in non-verbose mode
    if not verbose:
        print_progress_bar(100.0)
        print()  # New line after progress bar

    bytes_sent = gbz_size

    # Send end
    if VERBOSE:
        print("\n[Phase 3: Completing transfer...]")
    send_end(ser, gbz_crc)

    cmd, params = wait_for_response(ser, "o", timeout=10)
    if cmd != "o":
        print("ERROR: Transfer did not complete successfully")
        ser.close()
        return False

    # Get the filename from OK response (may be different for GBZ -> decompressed)
    # Device now sends decoded filename, so we use it as-is to detect any issues
    saved_filename = params if params else (original_filename if not is_user_gbz else gbz_filename)

    elapsed = time.time() - start_time
    print(f"\n{'='*60}")
    print("SUCCESS!")
    if VERBOSE:
        if is_user_gbz:
            print(f"Transferred {bytes_sent} bytes (pre-compressed GBZ)")
        else:
            compression_saved = original_size - bytes_sent
            print(f"Transferred {bytes_sent} bytes (compressed from {original_size} bytes)")
            print(f"Saved {compression_saved} bytes ({compression_ratio:.1f}% reduction)")
        print(f"Time: {elapsed:.1f}s, Speed: {bytes_sent/elapsed/1024:.1f} KB/s")
    else:
        if is_user_gbz:
            print(f"Transferred: {saved_filename} (kept as GBZ)")
        elif saved_filename != original_filename:
            print(f"Transferred and decompressed: {saved_filename}")
        else:
            print(f"Transferred: {saved_filename}")
    print(f"{'='*60}\n")

    # Restart CrankBoy if requested
    if restart:
        if VERBOSE:
            print("[Restarting CrankBoy...]")
        else:
            print("Restarting CrankBoy...")
        send_restart(ser)
        # Device will restart, no response expected
        ser.close()
        return True

    ser.close()
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Transfer Game Boy ROMs to CrankBoy via serial (ft protocol). "
                    ".gb and .gbc files are automatically compressed for transfer then "
                    "decompressed on device. Pre-compressed .gbz files are transferred as-is."
    )
    parser.add_argument("file", help="ROM file to transfer (.gb, .gbc, .gbz)")
    parser.add_argument("--port", default=SERIAL_PORT, help=f"Serial port (default: {SERIAL_PORT})")
    parser.add_argument("-v", "--verbose", action="store_true", help="Enable verbose output")
    parser.add_argument("--restart", action="store_true", help="Restart CrankBoy after successful transfer")
    parser.add_argument("--drop-rate", type=int, default=0, help="Artificial packet drop rate %% for testing (0-100)")

    args = parser.parse_args()

    if not os.path.exists(args.file):
        print(f"ERROR: File not found: {args.file}")
        sys.exit(1)

    if not os.path.isfile(args.file):
        print(f"ERROR: Not a file: {args.file}")
        sys.exit(1)

    ext = os.path.splitext(args.file)[1].lower()
    if ext not in ['.gb', '.gbc', '.gbz']:
        print(f"WARNING: Unusual extension '{ext}' (expected .gb, .gbc, or .gbz)")
        response = input("Continue anyway? [y/N]: ")
        if response.lower() != 'y':
            sys.exit(1)

    success = transfer_file(args.file, args.port, verbose=args.verbose, restart=args.restart, drop_rate=args.drop_rate)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
