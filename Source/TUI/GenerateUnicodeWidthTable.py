#!/usr/bin/env python3

import argparse
from pathlib import Path


def parse_range(value):
    if ".." in value:
        begin, end = value.split("..")
        return range(int(begin, 16), int(end, 16) + 1)
    code = int(value, 16)
    return range(code, code + 1)


def parse_property(path, accepted):
    result = set()
    for raw_line in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        code_range, value = (part.strip() for part in line.split(";", 1))
        if value in accepted:
            result.update(parse_range(code_range))
    return result


def parse_categories(path):
    result = set()
    first = None
    first_category = None
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        fields = line.split(";")
        code = int(fields[0], 16)
        name = fields[1]
        category = fields[2]
        if name.endswith(", First>"):
            first = code
            first_category = category
        elif name.endswith(", Last>"):
            if first_category in {"Mn", "Me"}:
                result.update(range(first, code + 1))
            first = None
            first_category = None
        elif category in {"Mn", "Me"}:
            result.add(code)
    return result


def merge_ranges(values):
    result = []
    begin = None
    end = None
    for value in sorted(values):
        if begin is None:
            begin = end = value
        elif value == end + 1:
            end = value
        else:
            result.append((begin, end))
            begin = end = value
    if begin is not None:
        result.append((begin, end))
    return result


def write_ranges(output, name, ranges):
    output.append(f"const TuiWidthRange {name}[] =")
    output.append("{")
    for begin, end in ranges:
        output.append(f"\t{{ 0x{begin:06X}, 0x{end:06X} }},")
    output.append("};")
    output.append("")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--east-asian-width", required=True)
    parser.add_argument("--unicode-data", required=True)
    parser.add_argument("--prop-list", required=True)
    parser.add_argument("--derived-core-properties", required=True)
    parser.add_argument("--emoji-data", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    zero = parse_categories(args.unicode_data)
    zero.update(parse_property(args.prop_list, {"Noncharacter_Code_Point"}))
    zero.update(parse_property(args.derived_core_properties, {"Default_Ignorable_Code_Point"}))
    zero.update(range(0x0000, 0x0020))
    zero.update(range(0x007F, 0x00A0))
    zero.update(range(0xD800, 0xE000))

    wide = parse_property(args.east_asian_width, {"W", "F"})
    wide.update(parse_property(args.emoji_data, {"Emoji_Presentation"}))
    wide.difference_update(zero)

    output = [
        "/***********************************************************************",
        "Generated from the Unicode Character Database 17.0.0.",
        "Run GenerateUnicodeWidthTable.py with the five official data files.",
        "***********************************************************************/",
        "",
        '#include "TUI.Internal.h"',
        "",
        "namespace vl",
        "{",
        "\tnamespace console",
        "\t{",
        "\t\tnamespace tui_internal",
        "\t\t{",
        "\t\t\tstruct TuiWidthRange",
        "\t\t\t{",
        "\t\t\t\tchar32_t\t\t\t\t\tbegin;",
        "\t\t\t\tchar32_t\t\t\t\t\tend;",
        "\t\t\t};",
        "",
    ]
    write_ranges(output, "tuiWidthZeroRanges", merge_ranges(zero))
    write_ranges(output, "tuiWidthTwoRanges", merge_ranges(wide))
    output.extend([
        "\t\t\tbool InRanges(char32_t code, const TuiWidthRange* ranges, vint count)",
        "\t\t\t{",
        "\t\t\t\tvint begin = 0;",
        "\t\t\t\tvint end = count - 1;",
        "\t\t\t\twhile (begin <= end)",
        "\t\t\t\t{",
        "\t\t\t\t\tauto middle = (begin + end) / 2;",
        "\t\t\t\t\tif (code < ranges[middle].begin)",
        "\t\t\t\t\t{",
        "\t\t\t\t\t\tend = middle - 1;",
        "\t\t\t\t\t}",
        "\t\t\t\t\telse if (code > ranges[middle].end)",
        "\t\t\t\t\t{",
        "\t\t\t\t\t\tbegin = middle + 1;",
        "\t\t\t\t\t}",
        "\t\t\t\t\telse",
        "\t\t\t\t\t{",
        "\t\t\t\t\t\treturn true;",
        "\t\t\t\t\t}",
        "\t\t\t\t}",
        "\t\t\t\treturn false;",
        "\t\t\t}",
        "",
        "\t\t\tbool IsZeroWidthCodePoint(char32_t code)",
        "\t\t\t{",
        "\t\t\t\treturn InRanges(code, tuiWidthZeroRanges, sizeof(tuiWidthZeroRanges) / sizeof(*tuiWidthZeroRanges));",
        "\t\t\t}",
        "",
        "\t\t\tbool IsTwoWidthCodePoint(char32_t code)",
        "\t\t\t{",
        "\t\t\t\treturn InRanges(code, tuiWidthTwoRanges, sizeof(tuiWidthTwoRanges) / sizeof(*tuiWidthTwoRanges));",
        "\t\t\t}",
        "",
        "\t\t}",
        "\t}",
        "}",
    ])
    Path(args.output).write_text("\n".join(output), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
