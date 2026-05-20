#!/usr/bin/env python3

import json
import sys


def emit_escaped(data: bytes) -> str:
    parts = []
    for b in data:
        if b == 0x22:
            parts.append('\\"')
        elif b == 0x5C:
            parts.append("\\\\")
        elif b == 0x0A:
            parts.append('\\n"\n    "')
        elif b == 0x0D:
            parts.append("\\r")
        elif b == 0x09:
            parts.append("\\t")
        elif 0x20 <= b < 0x7F:
            parts.append(chr(b))
        else:
            # close literal after hex
            parts.append('\\x%02x" "' % b)
    return "".join(parts)


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input.json> <output.c> <symbol>", file=sys.stderr)
        sys.exit(1)

    src, dst, symbol = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(src, "rb") as f:
        data = f.read()

    try:
        json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        print(f"embed_json: {src} is not valid JSON: {e}", file=sys.stderr)
        sys.exit(1)

    body = (
        f"/* Auto-generated from {src}. Do not edit. */\n"
        f"const char {symbol}[] =\n"
        f'    "{emit_escaped(data)}";\n'
    )

    with open(dst, "w") as f:
        f.write(body)


if __name__ == "__main__":
    main()
