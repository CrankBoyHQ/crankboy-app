#!/usr/bin/env python3

import urllib.request
import urllib.error
import re
import json
import os

def create_split_game_json_256():
    """
    Downloads and processes game DAT files, integrates a local homebrew JSON,
    and then splits the final data into 256 separate JSON files (00-FF)
    based on the first two characters of the ROM CRC. Files are saved in
    the 'Source/db/' directory.
    """

    headers = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36'
    }

    urls = [
        "https://raw.githubusercontent.com/libretro/libretro-database/refs/heads/master/metadat/genre/Nintendo%20-%20Game%20Boy%20Color.dat",
        "https://raw.githubusercontent.com/libretro/libretro-database/refs/heads/master/metadat/genre/Nintendo%20-%20Game%20Boy.dat"
    ]

    phrases_to_keep = [
        "(Seiken Densetsu Collection)",
        "(Castlevania Anniversary Collection)",
        "(Contra Anniversary Collection)",
        "(Collection of Mana)",
        "(Collection of SaGa)"
    ]

    all_games_dict = {}

    for url in urls:
        print(f"Processing file from: {url}")

        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req) as response:
                dat_content = response.read().decode('utf-8')
                print(f"  -> Download successful. Parsing...")
        except urllib.error.URLError as e:
            print(f"Error: A network error occurred. Details: {e}")
            continue

        entries = dat_content.split('\n\n')

        processed_count = 0
        for entry in entries:
            if entry.strip().startswith('game ('):
                comment_match = re.search(r'comment\s+\"(.*?)\"', entry)
                crc_match = re.search(r'crc\s+([A-F0-9]{8})', entry)

                if comment_match and crc_match:
                    long_title = comment_match.group(1)
                    crc = crc_match.group(1)

                    pattern = r'\s*\([^)]*\)'
                    short_title = re.sub(
                        pattern,
                        lambda m: m.group(0) if m.group(0).strip() in phrases_to_keep else '',
                        long_title
                    ).strip()

                    if "Butt-head" in long_title:
                        long_title = long_title.replace("Butt-head", "Butt-Head")
                        short_title = short_title.replace("Butt-head", "Butt-Head")

                    all_games_dict[crc] = {
                        "long": long_title,
                        "short": short_title
                    }
                    processed_count += 1

        print(f"  -> Found and processed {processed_count} games from this file.")

    # --- HOMEBREW JSON INTEGRATION ---

    homebrew_file = "homebrew.json"
    print(f"\nLooking for '{homebrew_file}'...")

    if os.path.exists(homebrew_file):
        try:
            with open(homebrew_file, 'r', encoding='utf-8') as f:
                homebrew_data = json.load(f)

            print(f"  -> Found '{homebrew_file}'. Integrating entries...")
            homebrew_added_count = 0
            for crc, data in homebrew_data.items():
                if crc.upper() != 'XXXXXXXX':
                    all_games_dict[crc] = data
                    homebrew_added_count += 1
                else:
                    print(f"  -> Skipped invalid entry: {data.get('long', 'N/A')}")

            print(f"  -> Integrated {homebrew_added_count} homebrew games.")

        except json.JSONDecodeError as e:
            print(f"Error: Could not parse '{homebrew_file}'. It might not be valid JSON. Details: {e}")
        except Exception as e:
            print(f"Error: An unexpected error occurred while processing '{homebrew_file}'. Details: {e}")
    else:
        print(f"  -> '{homebrew_file}' not found. Skipping integration.")

    # --- FILE SPLITTING AND OUTPUT (00-FF) ---

    output_dir = os.path.join("Source", "db")

    hex_chars = "0123456789ABCDEF"
    prefixes = [f"{i}{j}" for i in hex_chars for j in hex_chars]

    split_data = {prefix: {} for prefix in prefixes}

    for crc, game_data in all_games_dict.items():
        prefix = crc[0:2].upper()
        if prefix in split_data:
            split_data[prefix][crc] = game_data

    print(f"\nTotal unique games found: {len(all_games_dict)}")
    print(f"Splitting data into up to 256 files in '{output_dir}/'...")

    try:
        os.makedirs(output_dir, exist_ok=True)
    except OSError as e:
        print(f"Error: Could not create directory '{output_dir}'. Details: {e}")
        return

    files_written = 0
    for prefix, games in split_data.items():
        if games:
            file_path = os.path.join(output_dir, f"{prefix.lower()}.json")
            try:
                with open(file_path, 'w', encoding='utf-8') as json_file:
                    json.dump(games, json_file, indent=4, ensure_ascii=False)
                files_written += 1
            except IOError as e:
                print(f"Error: Could not write to file '{file_path}'. Details: {e}")

    print(f"  -> Wrote {files_written} files.")
    print("\nScript finished successfully.")

if __name__ == "__main__":
    create_split_game_json_256()
