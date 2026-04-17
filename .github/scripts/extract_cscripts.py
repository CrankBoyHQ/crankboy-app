#!/usr/bin/env python3
"""
Extract C script info and generate markdown wiki page.
"""

import os
import re
import sys
from datetime import datetime


def extract_description_macro(content):
    """Extract the DESCRIPTION macro value from file content."""
    lines = content.split('\n')
    in_description = False
    description_lines = []

    for line in lines:
        if in_description:
            # Check if line ends with backslash (continuation)
            if line.rstrip().endswith('\\'):
                description_lines.append(line.rstrip()[:-1])  # Remove trailing backslash
            else:
                # Last line of the macro
                description_lines.append(line)
                break
        elif '#define DESCRIPTION' in line:
            # Start of DESCRIPTION macro
            # Get content after #define DESCRIPTION
            parts = line.split('#define DESCRIPTION', 1)
            if len(parts) > 1:
                remainder = parts[1].strip()
                if remainder:
                    # Content on same line
                    if remainder.endswith('\\'):
                        description_lines.append(remainder[:-1])
                        in_description = True
                    else:
                        description_lines.append(remainder)
                        break
                else:
                    in_description = True

    if description_lines:
        desc = ''.join(description_lines)
        desc = desc.strip()
        return desc

    return ''


def parse_c_string_literal(s):
    """Parse a C string literal, handling escape sequences."""
    # Remove surrounding quotes if present
    s = s.strip()
    if s.startswith('"') and s.endswith('"'):
        s = s[1:-1]

    # Handle common escape sequences
    s = s.replace('\\n', '\n')
    s = s.replace('\\t', '\t')
    s = s.replace('\\\\', '\\')
    s = s.replace('\\"', '"')

    return s


def extract_cscript_info(filepath):
    """Extract C_SCRIPT blocks from a .c file."""
    with open(filepath, 'r') as f:
        content = f.read()

    # First extract the DESCRIPTION macro if present
    description_macro = extract_description_macro(content)

    # Find all C_SCRIPT blocks
    pattern = r'C_SCRIPT\s*\{([^}]+)\}'
    matches = re.findall(pattern, content, re.DOTALL)

    scripts = []
    for match in matches:
        script = {}

        # Extract rom_name
        rom_match = re.search(r'\.rom_name\s*=\s*"([^"]+)"', match)
        script['rom_name'] = rom_match.group(1) if rom_match else 'Unknown'

        # Extract description
        desc_match = re.search(r'\.description\s*=\s*([^,]+)', match, re.DOTALL)
        if desc_match:
            desc = desc_match.group(1).strip()
            # If it's the DESCRIPTION macro, use the macro value
            if desc == 'DESCRIPTION':
                desc = description_macro
            elif desc.startswith('DESCRIPTION'):
                # Handle cases like DESCRIPTION with extra stuff
                desc = description_macro

            # Handle string concatenation - split by quoted strings and parse each
            # Pattern to match quoted strings
            string_pattern = r'"((?:[^"\\]|\\.)*)"'
            string_matches = re.findall(string_pattern, desc)
            if string_matches:
                # Concatenate all string literals
                parsed_strings = [parse_c_string_literal(f'"{m}"') for m in string_matches]
                desc = ''.join(parsed_strings)
            else:
                # Not a quoted string, use as-is
                desc = parse_c_string_literal(desc)

            # Clean up extra whitespace from macro indentation (but preserve newlines)
            # First normalize newlines
            desc = desc.replace('\r\n', '\n').replace('\r', '\n')
            # Then collapse multiple spaces/tabs into single spaces, but keep newlines
            lines = desc.split('\n')
            cleaned_lines = []
            for line in lines:
                # Collapse multiple spaces/tabs within each line
                cleaned_line = ' '.join(line.split())
                if cleaned_line:  # Only keep non-empty lines
                    cleaned_lines.append(cleaned_line)
            desc = '\n'.join(cleaned_lines)
            script['description'] = desc
        else:
            script['description'] = ''

        # Extract experimental flag
        exp_match = re.search(r'\.experimental\s*=\s*(true|false)', match)
        script['experimental'] = exp_match.group(1) == 'true' if exp_match else False

        scripts.append(script)

    return scripts


def format_description(desc):
    """Format description for markdown table."""
    # Replace actual newlines with <br>
    desc = desc.replace('\n', '<br>')
    # Remove trailing newlines
    desc = desc.strip()
    return desc


def generate_markdown(scripts, version_tag):
    """Generate the full markdown wiki page."""
    stable = [s for s in scripts if not s['experimental']]
    experimental = [s for s in scripts if s['experimental']]

    lines = [
        "## Stable",
        "",
        "| Game | Features | Source |",
        "|------|----------|--------|"
    ]

    for script in sorted(stable, key=lambda x: x['rom_name']):
        desc = format_description(script['description'])
        filename = script['filename']
        source_link = f"[{filename}](https://github.com/CrankBoyHQ/crankboy-app/blob/master/src/cscripts/{filename})"
        lines.append(f"| {script['rom_name']} | {desc} | {source_link} |")

    if not stable:
        lines.append("| *No stable scripts yet* | | |")

    lines.extend([
        "",
        "## Experimental",
        "",
        "| Game | Features | Source |",
        "|------|----------|--------|"
    ])

    for script in sorted(experimental, key=lambda x: x['rom_name']):
        desc = format_description(script['description'])
        filename = script['filename']
        source_link = f"[{filename}](https://github.com/CrankBoyHQ/crankboy-app/blob/master/src/cscripts/{filename})"
        lines.append(f"| {script['rom_name']} | {desc} | {source_link} |")

    if not experimental:
        lines.append("| *No experimental scripts yet* | | |")

    today = datetime.now().strftime('%Y-%m-%d')
    lines.extend([
        "",
        "---",
        "",
        "This page is automatically generated from the C scripts in the repository.",
        "",
        f"*Last updated: {today} for {version_tag} by GitHub Actions*"
    ])

    return '\n'.join(lines)


def main():
    cscripts_dir = 'src/cscripts'
    version_tag = sys.argv[1] if len(sys.argv) > 1 else 'unknown'

    all_scripts = []

    for filename in sorted(os.listdir(cscripts_dir)):
        if filename.endswith('.c'):
            filepath = os.path.join(cscripts_dir, filename)
            scripts = extract_cscript_info(filepath)
            for script in scripts:
                script['filename'] = filename
            all_scripts.extend(scripts)

    markdown = generate_markdown(all_scripts, version_tag)
    print(markdown)


if __name__ == '__main__':
    main()
