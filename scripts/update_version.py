#!/usr/bin/env python3

import sys
import json
import os
from datetime import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)

def load_pdxinfo(filename):
    """
    Loads key-value pairs from a pdxinfo file, preserving the original key order.
    Returns a dictionary of the data and a list of keys in order.
    """
    data = {}
    ordered_keys = []
    try:
        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if line and '=' in line:
                    key, value = line.split('=', 1)
                    key = key.strip()
                    value = value.strip()
                    data[key] = value
                    if key not in ordered_keys:
                        ordered_keys.append(key)
    except FileNotFoundError:
        print(f"Error: The file {filename} was not found.")
        sys.exit(1)
    return data, ordered_keys

def write_pdxinfo(data, ordered_keys, output_file):
    """
    Writes key-value pairs to a pdxinfo file using the provided key order.
    """
    # If any keys were added to `data` that weren't in the original file
    # (like a new buildNumber), add them to the key list so they get written.
    for key in data:
        if key not in ordered_keys:
            ordered_keys.append(key)

    content_lines = [f"{k}={data[k]}" for k in ordered_keys]
    content = '\n'.join(content_lines)

    with open(output_file, 'w') as f:
        f.write(content + '\n')
    print(f"Successfully updated {output_file}")

def write_version_json(data, filename):
    """Writes the updated data back to the version.json file."""
    with open(filename, 'w') as f:
        json.dump(data, f, indent=4)
    print(f"Successfully updated {filename}")


def main():
    # --- Argument Parsing ---
    pdxinfo_path = ""
    version_json_path = ""

    # Case 1: No arguments provided, use default paths
    if len(sys.argv) == 1:
        print("No paths provided. Looking for default files...")
        # Changed: Default directory is now based on project root
        default_dir = os.path.join(PROJECT_ROOT, "Source")
        pdxinfo_path = os.path.join(default_dir, "pdxinfo")
        version_json_path = os.path.join(default_dir, "version.json")

        if not os.path.exists(pdxinfo_path) or not os.path.exists(version_json_path):
            print(f"\nError: Could not find 'pdxinfo' and/or 'version.json' in the '{default_dir}/' directory.")
            print(f"\nUsage: {sys.argv[0]} [pdxinfo_path] [version_json_path] [output_path]")
            print("If no paths are provided, the script automatically looks in the 'Source/' directory relative to the project root.")
            sys.exit(1)
        print("Found default files. Proceeding...")

    # Case 2: Paths are provided as arguments
    elif len(sys.argv) >= 3:
        pdxinfo_path = sys.argv[1]
        version_json_path = sys.argv[2]

    # Case 3: Incorrect number of arguments
    else:
        print(f"Usage: {sys.argv[0]} [pdxinfo_path] [version_json_path] [output_path]")
        print("Please provide both paths or no paths to use the default behavior.")
        sys.exit(1)

    output_path = sys.argv[3] if len(sys.argv) > 3 else pdxinfo_path

    # --- Load Data ---
    pdxinfo, pdxinfo_keys = load_pdxinfo(pdxinfo_path)
    try:
        with open(version_json_path, 'r') as f:
            version_json = json.load(f)
    except FileNotFoundError:
        print(f"Error: The file {version_json_path} was not found.")
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Error: Could not decode JSON from {version_json_path}.")
        sys.exit(1)

    # --- Version Check & Update ---
    current_pdx_version = pdxinfo.get('version', '0.0.0')
    version_from_json = version_json.get('name', 'v0.0.0').lstrip('v')

    print(f"\nCurrent version in pdxinfo: {current_pdx_version}")
    print(f"Version in version.json:   {version_from_json}")

    if current_pdx_version == version_from_json:
        if not sys.stdin.isatty():
            new_version = current_pdx_version
            print("Versions are the same. Running non-interactively, keeping current version.")
        else:
            print("\nVersions are the same.")
            user_input_version = ""
            while not user_input_version:
                user_input_version = input("Please enter the new version number: ").strip().lstrip('v')
                if not user_input_version:
                    print("Version number cannot be empty. Please try again.")

            new_version = user_input_version

            print(f"Updating version.json with new version 'v{new_version}'...")
            version_json['name'] = f'v{new_version}'
            write_version_json(version_json, version_json_path)
    else:
        new_version = version_from_json
        print(f"\nVersion mismatch. Updating pdxinfo to match version.json: {new_version}")

    # --- Set Build Number based on Date ---
    new_build = str(datetime.now().strftime('%Y%m%d'))
    current_build_number = pdxinfo.get('buildNumber', 'not set')

    # --- Update pdxinfo only when version or buildNumber changed ---
    if current_pdx_version != new_version or current_build_number != new_build:
        print(f"Setting build number from {current_build_number} to {new_build}.")
        pdxinfo['version'] = new_version
        pdxinfo['buildNumber'] = new_build
        write_pdxinfo(pdxinfo, pdxinfo_keys, output_path)
    else:
        print(f"{output_path} is already up to date (version {new_version}, buildNumber {new_build}).")

if __name__ == "__main__":
    main()
