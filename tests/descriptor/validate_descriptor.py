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
REPORT_ID_RELATIVE_MOUSE = 2
REPORT_ID_ABSOLUTE_MOUSE = 3

ABSOLUTE_COORDINATE_MIN = 0
ABSOLUTE_COORDINATE_MAX = 32767

ITEM_TYPE_MAIN = 0
ITEM_TYPE_GLOBAL = 1
ITEM_TYPE_LOCAL = 2

MAIN_INPUT = 0x8
MAIN_OUTPUT = 0x9
MAIN_COLLECTION = 0xA
MAIN_FEATURE = 0xB
MAIN_END_COLLECTION = 0xC

GLOBAL_USAGE_PAGE = 0x0
GLOBAL_LOGICAL_MINIMUM = 0x1
GLOBAL_LOGICAL_MAXIMUM = 0x2
GLOBAL_REPORT_SIZE = 0x7
GLOBAL_REPORT_ID = 0x8
GLOBAL_REPORT_COUNT = 0x9

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


def signed_item_value(data: List[int]) -> int:
    if not data:
        return 0
    unsigned = item_value(data)
    sign_bit = 1 << ((len(data) * 8) - 1)
    if unsigned & sign_bit:
        return unsigned - (1 << (len(data) * 8))
    return unsigned


def add_usage_range(usages: Set[Usage], usage_page: Optional[int], minimum: Optional[int], maximum: Optional[int]) -> None:
    if usage_page is None or minimum is None or maximum is None:
        return
    if minimum > maximum:
        raise ValueError(f"usage minimum {minimum} exceeds maximum {maximum}")
    for usage in range(minimum, maximum + 1):
        usages.add((usage_page, usage))


def parse_descriptor(descriptor: List[int]) -> Dict[str, object]:
    usage_page: Optional[int] = None
    logical_minimum: Optional[int] = None
    logical_maximum: Optional[int] = None
    report_size: Optional[int] = None
    report_count: Optional[int] = None
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

        data = descriptor[index : index + data_size]
        value = item_value(data)
        signed_value = signed_item_value(data)
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
                        "logical_minimum": logical_minimum,
                        "logical_maximum": logical_maximum,
                        "report_size": report_size,
                        "report_count": report_count,
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
            elif tag == GLOBAL_LOGICAL_MINIMUM:
                logical_minimum = signed_value
            elif tag == GLOBAL_LOGICAL_MAXIMUM:
                logical_maximum = signed_value
            elif tag == GLOBAL_REPORT_SIZE:
                report_size = value
            elif tag == GLOBAL_REPORT_ID:
                current_report_id = value
                report_ids.add(value)
            elif tag == GLOBAL_REPORT_COUNT:
                report_count = value
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


def require_button_usages(items: List[InputItem], report_name: str) -> Set[Usage]:
    usages: Set[Usage] = set()
    for item in items:
        usages.update(item["usages"])

    expected_buttons = {(USAGE_PAGE_BUTTON, button) for button in range(1, 4)}
    missing_buttons = expected_buttons - usages
    if missing_buttons:
        raise ValueError(f"{report_name} button usages missing: {sorted(missing_buttons)}")
    return expected_buttons


def require_axis_item(
    items: List[InputItem],
    axis: int,
    report_name: str,
    *,
    relative: bool,
    report_size: int,
    report_count: int,
    logical_minimum: int,
    logical_maximum: int,
) -> None:
    axis_usage = (USAGE_PAGE_GENERIC_DESKTOP, axis)
    matching_items = [item for item in items if axis_usage in item["usages"]]
    if not matching_items:
        raise ValueError(f"{report_name} axis usage missing: {axis_usage}")

    for item in matching_items:
        flags = int(item["flags"])
        is_relative = bool(flags & INPUT_FLAG_RELATIVE)
        if is_relative != relative:
            expected = "relative" if relative else "absolute"
            raise ValueError(f"{report_name} axis is not {expected}: {axis_usage}")
        if item["report_size"] != report_size:
            raise ValueError(f"{report_name} axis {axis_usage} report size mismatch: {item['report_size']}")
        if item["report_count"] != report_count:
            raise ValueError(f"{report_name} axis {axis_usage} report count mismatch: {item['report_count']}")
        if item["logical_minimum"] != logical_minimum:
            raise ValueError(f"{report_name} axis {axis_usage} logical minimum mismatch: {item['logical_minimum']}")
        if item["logical_maximum"] != logical_maximum:
            raise ValueError(f"{report_name} axis {axis_usage} logical maximum mismatch: {item['logical_maximum']}")


def validate_descriptor(descriptor: List[int]) -> Dict[str, object]:
    summary = parse_descriptor(descriptor)

    expected_report_ids = {REPORT_ID_KEYBOARD, REPORT_ID_RELATIVE_MOUSE, REPORT_ID_ABSOLUTE_MOUSE}
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
    unexpected_mouse_reports = {
        item["report_id"]
        for item in mouse_items
        if item["report_id"] not in {REPORT_ID_RELATIVE_MOUSE, REPORT_ID_ABSOLUTE_MOUSE}
    }
    if unexpected_mouse_reports:
        raise ValueError(f"mouse input uses unexpected report IDs: {sorted(unexpected_mouse_reports)}")

    relative_mouse_items = [item for item in mouse_items if item["report_id"] == REPORT_ID_RELATIVE_MOUSE]
    if not relative_mouse_items:
        raise ValueError("relative mouse report has no input items")
    relative_buttons = require_button_usages(relative_mouse_items, "relative mouse")
    for axis in (USAGE_X, USAGE_Y):
        require_axis_item(
            relative_mouse_items,
            axis,
            "relative mouse",
            relative=True,
            report_size=8,
            report_count=2,
            logical_minimum=-127,
            logical_maximum=127,
        )

    absolute_mouse_items = [item for item in mouse_items if item["report_id"] == REPORT_ID_ABSOLUTE_MOUSE]
    if not absolute_mouse_items:
        raise ValueError("absolute mouse report has no input items")
    absolute_buttons = require_button_usages(absolute_mouse_items, "absolute mouse")
    for axis in (USAGE_X, USAGE_Y):
        require_axis_item(
            absolute_mouse_items,
            axis,
            "absolute mouse",
            relative=False,
            report_size=16,
            report_count=2,
            logical_minimum=ABSOLUTE_COORDINATE_MIN,
            logical_maximum=ABSOLUTE_COORDINATE_MAX,
        )

    summary["report_ids"] = sorted(report_ids)
    summary["top_level_usages"] = sorted(found_top_level_usages)
    summary["relative_mouse_buttons"] = sorted(relative_buttons)
    summary["relative_mouse_axes"] = sorted(
        {(USAGE_PAGE_GENERIC_DESKTOP, USAGE_X), (USAGE_PAGE_GENERIC_DESKTOP, USAGE_Y)}
    )
    summary["absolute_mouse_buttons"] = sorted(absolute_buttons)
    summary["absolute_mouse_axes"] = sorted(
        {(USAGE_PAGE_GENERIC_DESKTOP, USAGE_X), (USAGE_PAGE_GENERIC_DESKTOP, USAGE_Y)}
    )
    summary["absolute_mouse_coordinate_range"] = (ABSOLUTE_COORDINATE_MIN, ABSOLUTE_COORDINATE_MAX)
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
    print(f"Relative mouse buttons: {summary['relative_mouse_buttons']}")
    print(f"Relative mouse axes: {summary['relative_mouse_axes']} (relative, 8-bit, -127..127)")
    print(f"Absolute mouse buttons: {summary['absolute_mouse_buttons']}")
    print(
        f"Absolute mouse axes: {summary['absolute_mouse_axes']} "
        f"(absolute, 16-bit, {ABSOLUTE_COORDINATE_MIN}..{ABSOLUTE_COORDINATE_MAX})"
    )
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
