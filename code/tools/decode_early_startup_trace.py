#!/usr/bin/env python3

import argparse
import struct
from pathlib import Path

RECORD = struct.Struct("<IIIQQI")

MARKERS = {
    1: "FreezeEnter",
    2: "SnapshotBegin",
    3: "SnapshotEnd",
    4: "Thread32FirstBegin",
    5: "Thread32FirstEnd",
    6: "Thread32NextBegin",
    7: "Thread32NextEnd",
    8: "CandidateThread",
    9: "OpenThreadBegin",
    10: "OpenThreadEnd",
    11: "SuspendThreadBegin",
    12: "SuspendThreadEnd",
    13: "GetThreadContextBegin",
    14: "GetThreadContextEnd",
    15: "SetThreadContextBegin",
    16: "SetThreadContextEnd",
    17: "FreezeExit",
    18: "UnfreezeOpenThreadBegin",
    19: "UnfreezeOpenThreadEnd",
    20: "ResumeThreadBegin",
    21: "ResumeThreadEnd",
}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Decode FiveM MinHook early-startup binary trace records."
    )
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()

    data = args.trace.read_bytes()

    if len(data) % RECORD.size != 0:
        trailing = len(data) % RECORD.size
        raise SystemExit(
            f"invalid trace size: {len(data)} bytes; "
            f"{trailing} trailing byte(s), record size is {RECORD.size}"
        )

    for index, offset in enumerate(range(0, len(data), RECORD.size)):
        marker, pid, tid, value1, value2, last_error = RECORD.unpack_from(
            data, offset
        )
        name = MARKERS.get(marker, f"Unknown({marker})")

        print(
            f"{index:05d} "
            f"{name:<30} "
            f"pid={pid:<6} "
            f"tid={tid:<6} "
            f"value1=0x{value1:016X} "
            f"value2=0x{value2:016X} "
            f"lastError={last_error} (0x{last_error:08X})"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
