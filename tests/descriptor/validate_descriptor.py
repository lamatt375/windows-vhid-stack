#!/usr/bin/env python3
"""Validate the static HID report descriptor shape without loading a driver."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


USAGE_PAGE_GENERIC_DESKTOP = 0x01
USAGE_KEYBOARD = 0x06
USAGE_MOUSE = 0x02

ITEM_TYPE_MAIN = 0
ITEM_TYPE_GLOBAL = 1
ITEM_TYPE_LOCAL = 2

MAIN_INPUT = 0x8
MAIN_OUTPUT = 0x9
MAIN_COLLECTION = 0xA
MAIN_FEATURE = 0xB
MAIN_END_COLLECTION = 0xC

GLOBAL_USAGE_PAGE = 0x0
GLOBAL_REPORT_ID = 0x8

LOCAL_USAGE = 0x0


def descriptor_header_path() -> Path:
    return Path(__file__).resolve().parents[2] / "src" / "shared" / "VhidHidDescriptor.h"


def read_descriptor_bytes(path: Path) -> List[int]:
    text = path.read_text(encoding="utf-8")
    array_match = re.search(r"VhidHidReportDescriptor\[\]\s*=\s*\{(?P<body>.*?)\};", text, re.S)
    if not array_match:
        raise ValueError(f"descriptor array not found in {path}")
    values = [int(match.group(0), 16) for match in re.finditer(r"0x[0-9A-Fa-f]{1,2}", array_match.group("body"))]
    if not values:
        raise ValueError(f"no descriptor bytes found in {path}")
    return values


def item_value(data: List[int]) -> int:
    value = 0
    for offset, byte in enumerate(data):
        value |= byte << (8 * offset)
    return value


def validate_descriptor(descriptor: List[int]) -> Dict[str, object]:
    usage_page: Optional[int] = None
    local_usages: List[Tuple[Optional[int], int]] = []
    collection_stack: List[int] = []
    top_level_usages: List[Tuple[Optional[int], int]] = []
    report_ids: Set[int] = set()
    input_count = 0
    output_count = 0
    feature_count = 0

    index = 0
    while index < len(descriptor):
        prefix = descriptor[index]
        index += 1

        if prefix == 0xFE:
            if index + 2 > len(descriptor):
                raise ValueError("truncated long item header")
            data_size = descriptor[index]
            index += 2
            if index + data_size > len(descriptor):
                raise ValueError("truncated long item payload")
            index += data_size
            local_usages.clear()
            continue

        size_code = prefix & 0x03
        data_size = 4 if size_code == 3 else size_code
        item_type = (prefix >> 2) & 0x03
        tag = (prefix >> 4) & 0x0F

        if index + data_size > len(descriptor):
            raise ValueError(f"item at byte {index - 1} overruns descriptor")

        value = item_value(descriptor[index : index + data_size])
        index += data_size

        if item_type == ITEM_TYPE_MAIN:
            if tag == MAIN_INPUT:
                input_count += 1
            elif tag == MAIN_OUTPUT:
                output_count += 1
            elif tag == MAIN_COLLECTION:
                if value == 0x01 and not collection_stack and local_usages:
                    top_level_usages.append(local_usages[0])
                collection_stack.append(value)
            elif tag == MAIN_FEATURE:
                feature_count += 1
            elif tag == MAIN_END_COLLECTION:
                if not collection_stack:
                    raise ValueError("end collection without matching collection")
                collection_stack.pop()
            local_usages.clear()
        elif item_type == ITEM_TYPE_GLOBAL:
            if tag == GLOBAL_USAGE_PAGE:
                usage_page = value
            elif tag == GLOBAL_REPORT_ID:
                report_ids.add(value)
        elif item_type == ITEM_TYPE_LOCAL:
            if tag == LOCAL_USAGE:
                local_usages.append((usage_page, value))

    if collection_stack:
        raise ValueError("descriptor ended with unclosed collections")

    expected_report_ids = {1, 2}
    if report_ids != expected_report_ids:
        raise ValueError(f"expected report IDs {sorted(expected_report_ids)}, found {sorted(report_ids)}")

    expected_top_level_usages = {
        (USAGE_PAGE_GENERIC_DESKTOP, USAGE_KEYBOARD),
        (USAGE_PAGE_GENERIC_DESKTOP, USAGE_MOUSE),
    }
    found_top_level_usages = set(top_level_usages)
    missing = expected_top_level_usages - found_top_level_usages
    if missing:
        raise ValueError(f"missing top-level usages: {sorted(missing)}")

    if output_count or feature_count:
        raise ValueError(f"unexpected output/feature items: outputs={output_count}, features={feature_count}")

    if input_count == 0:
        raise ValueError("descriptor has no input items")

    return {
        "byte_count": len(descriptor),
        "report_ids": sorted(report_ids),
        "top_level_usages": sorted(found_top_level_usages),
        "input_items": input_count,
        "output_items": output_count,
        "feature_items": feature_count,
    }


def main() -> int:
    path = descriptor_header_path()
    descriptor = read_descriptor_bytes(path)
    summary = validate_descriptor(descriptor)

    print(f"Descriptor header: {path}")
    print(f"Descriptor bytes: {summary['byte_count']}")
    print(f"Report IDs: {summary['report_ids']}")
    print(f"Top-level usages: {summary['top_level_usages']}")
    print(f"Input items: {summary['input_items']}")
    print("PASS: static descriptor invariants satisfied")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)