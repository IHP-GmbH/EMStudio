#!/usr/bin/env python3
"""Regenerate the XOR blob embedded in src/assistantpromptblob.cpp.

  python tools/gen_assistant_policy_blob.py path/to/policy.txt

Paste kPolicyLen / kPolicyFnv / kPolicyBlob into assistantpromptblob.cpp.
The policy plaintext is intentionally not stored next to the agent sources.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

KEY = bytes(
    [0xA7, 0x3C, 0x91, 0x5E, 0x2B, 0xD4, 0x08, 0xF1, 0x6A, 0xC3, 0x19, 0x7E, 0xB5, 0x44, 0xE0, 0x2D]
)


def fnv1a32(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("policy_file", type=pathlib.Path, help="UTF-8 policy text (trailing newline OK)")
    args = parser.parse_args()

    data = args.policy_file.read_bytes()
    # Normalize to UTF-8 text ending with a single \n if the file has content.
    text = data.decode("utf-8")
    if text and not text.endswith("\n"):
        text += "\n"
    data = text.encode("utf-8")

    enc = bytes(b ^ KEY[i % len(KEY)] for i, b in enumerate(data))
    h = fnv1a32(data)

    print(f"kPolicyLen = {len(enc)}")
    print(f"kPolicyFnv = 0x{h:08x}")
    print("kPolicyBlob = {")
    for i in range(0, len(enc), 12):
        chunk = enc[i : i + 12]
        print("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    print("};")
    return 0


if __name__ == "__main__":
    sys.exit(main())
