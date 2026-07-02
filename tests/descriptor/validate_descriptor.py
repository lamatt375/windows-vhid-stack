#!/usr/bin/env python3
"""Validate the static HID report descriptor shape without loading a driver."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


USAGE_PAGE_GENERIC_DESKTOP = 0x01
USAGE_PAGE_BUTTON = 0x09

USAGE_MOUSE = 0x02
USAGE_KEYBOARD = 0x06
USAGE_X = 0x30
USAGE_Y = 0x31
USAGE_WHEEL = 0x38

REPORT_ID_KEYBOARD = 1
REPORT_ID_MOUSE = 2

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
LOCAL_USAGE_MINIMUM = 0x1
LOCAL_USAGE_MAXIMUM = 0x2

INPUT_FLAG_RELATIVE = 0x04
COLLECTION_APPLICATION = 0x01

Usage = Tuple[int, int]
InputItem = Dict[str, object]


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


def add_usage_range(usages: Set[Usage], usage_page: Optional[int], minimum: Optional[int], maximum: Optional[int]) -> None:
    if usage_page is None or minimum is None or maximum is None:
        return
    if minimum > maximum:
        raise ValueError(f"usage minimum {minimum} exceeds maximum {maximum}")
    for usage in range(minimum, maximum + 1):
        usages.add((usage_page, usage))


def parse_descriptor(descriptor: List[int]) -> Dict[str, object]:
    usage_page: Optional[int] = None
    local_usages: List[Usage] = []
    usage_minimum: Optional[int] = None
    usage_maximum: Optional[int] = None
    collection_stack: List[Dict[str, object]] = []
    top_level_usages: List[Usage] = []
    report_ids: Set[int] = set()
    current_report_id: Optional[int] = None
    input_items: List[InputItem] = []
    output_count = 0
    feature_count = 0
    all_usages: Set[Usage] = set()

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
            usage_minimum = None
            usage_maximum = None
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
            expanded_usages = set(local_usages)
            add_usage_range(expanded_usages, usage_page, usage_minimum, usage_maximum)
            top_level_usage = collection_stack[0]["usage"] if collection_stack else None

            if tag == MAIN_INPUT:
                input_items.append(
                    {
                        "report_id": current_report_id,
                        "flags": value,
                        "usages": expanded_usages,
                        "top_level_usage": top_level_usage,
                    }
                )
            elif tag == MAIN_OUTPUT:
                output_count += 1
            elif tag == MAIN_COLLECTION:
                collection_usage = local_usages[0] if local_usages else None
                if value == COLLECTION_APPLICATION and not collection_stack and collection_usage is not None:
                    top_level_usages.append(collection_usage)
                collection_stack.append({"type": value, "usage": collection_usage})
            elif tag == MAIN_FEATURE:
                feature_count += 1
            elif tag == MAIN_END_COLLECTION:
                if not collection_stack:
                    raise ValueError("end collection without matching collection")
                collection_stack.pop()

            all_usages.update(expanded_usages)
            local_usages.clear()
            usage_minimum = None
            usage_maximum = None
        elif item_type == ITEM_TYPE_GLOBAL:
            if tag == GLOBAL_USAGE_PAGE:
                usage_page = value
            elif tag == GLOBAL_REPORT_ID:
                current_report_id = value
                report_ids.add(value)
        elif item_type == ITEM_TYPE_LOCAL:
            if tag == LOCAL_USAGE:
                if usage_page is None:
                    raise ValueError("usage item appeared before usage page")
                usage = (usage_page, value)
                local_usages.append(usage)
                all_usages.add(usage)
            elif tag == LOCAL_USAGE_MINIMUM:
                usage_minimum = value
            elif tag == LOCAL_USAGE_MAXIMUM:
                usage_maximum = value

    if collection_stack:
        raise ValueError("descriptor ended with unclosed collections")

    return {
        "byte_count": len(descriptor),
        "report_ids": report_ids,
        "top_level_usages": top_level_usages,
        "input_items": input_items,
        "output_items": output_count,
        "feature_items": feature_count,
        "all_usages": all_usages,
    }


def validate_descriptor(descriptor: List[int]) -> Dict[str, object]:
    summary = parse_descriptor(descriptor)

    expected_report_ids = {REPORT_ID_KEYBOARD, REPORT_ID_MOUSE}
    report_ids = summary["report_ids"]
    if report_ids != expected_report_ids:
        raise ValueError(f"expected report IDs {sorted(expected_report_ids)}, found {sorted(report_ids)}")

    expected_top_level_usages = {
        (USAGE_PAGE_GENERIC_DESKTOP, USAGE_KEYBOARD),
        (USAGE_PAGE_GENERIC_DESKTOP, USAGE_MOUSE),
    }
    found_top_level_usages = set(summary["top_level_usages"])
    missing = expected_top_level_usages - found_top_level_usages
    if missing:
        raise ValueError(f"missing top-level usages: {sorted(missing)}")

    if summary["output_items"] or summary["feature_items"]:
        raise ValueError(
            f"unexpected output/feature items: outputs={summary['output_items']}, features={summary['feature_items']}"
        )

    input_items = summary["input_items"]
    if not input_items:
        raise ValueError("descriptor has no input items")

    if (USAGE_PAGE_GENERIC_DESKTOP, USAGE_WHEEL) in summary["all_usages"]:
        raise ValueError("wheel usage must remain absent")

    keyboard_items = [
        item for item in input_items if item["top_level_usage"] == (USAGE_PAGE_GENERIC_DESKTOP, USAGE_KEYBOARD)
    ]
    if not keyboard_items:
        raise ValueError("keyboard top-level collection has no input items")
    unexpected_keyboard_reports = {item["report_id"] for item in keyboard_items if item["report_id"] != REPORT_ID_KEYBOARD}
    if unexpected_keyboard_reports:
        raise ValueError(f"keyboard input uses unexpected report IDs: {sorted(unexpected_keyboard_reports)}")

    mouse_items = [item for item in input_items if item["top_level_usage"] == (USAGE_PAGE_GENERIC_DESKTOP, USAGE_MOUSE)]
    if not mouse_items:
        raise ValueError("mouse top-level collection has no input items")
    unexpected_mouse_reports = {item["report_id"] for item in mouse_items if item["report_id"] != REPORT_ID_MOUSE}
    if unexpected_mouse_reports:
        raise ValueError(f"mouse input uses unexpected report IDs: {sorted(unexpected_mouse_reports)}")

    mouse_usages: Set[Usage] = set()
    for item in mouse_items:
        mouse_usages.update(item["usages"])

    expected_buttons = {(USAGE_PAGE_BUTTON, button) for button in range(1, 4)}
    missing_buttons = expected_buttons - mouse_usages
    if missing_buttons:
        raise ValueError(f"mouse button usages missing: {sorted(missing_buttons)}")

    for axis in (USAGE_X, USAGE_Y):
        axis_usage = (USAGE_PAGE_GENERIC_DESKTOP, axis)
        matching_items = [item for item in mouse_items if axis_usage in item["usages"]]
        if not matching_items:
            raise ValueError(f"mouse axis usage missing: {axis_usage}")
        if not any(int(item["flags"]) & INPUT_FLAG_RELATIVE for item in matching_items):
            raise ValueError(f"mouse axis is not relative: {axis_usage}")

    summary["report_ids"] = sorted(report_ids)
    summary["top_level_usages"] = sorted(found_top_level_usages)
    summary["mouse_buttons"] = sorted(expected_buttons)
    summary["mouse_relative_axes"] = sorted(
        {(USAGE_PAGE_GENERIC_DESKTOP, USAGE_X), (USAGE_PAGE_GENERIC_DESKTOP, USAGE_Y)}
    )
    return summary


def main() -> int:
    path = descriptor_header_path()
    descriptor = read_descriptor_bytes(path)
    summary = validate_descriptor(descriptor)

    print(f"Descriptor header: {path}")
    print(f"Descriptor bytes: {summary['byte_count']}")
    print(f"Report IDs: {summary['report_ids']}")
    print(f"Top-level usages: {summary['top_level_usages']}")
    print(f"Input items: {len(summary['input_items'])}")
    print(f"Mouse buttons: {summary['mouse_buttons']}")
    print(f"Mouse relative axes: {summary['mouse_relative_axes']}")
    print("Wheel usage: absent")
    print("Output items: 0")
    print("Feature items: 0")
    print("PASS: static descriptor invariants satisfied")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
