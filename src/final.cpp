#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <cctype>

#include "hashtable.h"

namespace fs = std::filesystem;

const std::string WESNOTH_UNITS_PATH = "wesnoth/units";

// Removes content between curly braces (like {SPECIAL_NOTES})
std::string remove_braced_tags(std::string line) {
    size_t start_pos = 0;
    while ((start_pos = line.find('{', start_pos)) != std::string::npos) {
        size_t end_pos = line.find('}', start_pos);
        if (end_pos != std::string::npos) {
            line.erase(start_pos, end_pos - start_pos + 1);
        }
        else {
            break;
        }
    }
    return line;
}

// Extract quoted value (key="value") and translation prefix ('_')
std::string extract_quoted_value(const std::string& line, const std::string& key) {
    size_t key_pos = line.find(key);
    if (key_pos == std::string::npos) return "";

    size_t start = line.find('"', key_pos);
    if (start == std::string::npos) return "";

    size_t end = line.find('"', start + 1);
    if (end == std::string::npos) return "";

    std::string value = line.substr(start + 1, end - start - 1);

    // Remove WML translation prefix
    if (value.length() > 0 && value[0] == '_') {
        return value.substr(1);
    }
    return value;
}

// Extracts unquoted WML values (like, usage=fighter)
std::string extract_unquoted_string_value(const std::string& line, const std::string& key) {
    size_t key_pos = line.find(key);
    if (key_pos == std::string::npos) return "";

    size_t eq_pos = line.find('=', key_pos);
    if (eq_pos == std::string::npos) return "";

    std::string value_candidate = line.substr(eq_pos + 1);

    // Trim leading whitespace
    size_t first = value_candidate.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    value_candidate = value_candidate.substr(first);

    // Find the end of the value (space, tab, newline, comment (#), or block end (]))
    size_t last_pos = value_candidate.find_first_of(" \t\r\n#]");

    if (last_pos != std::string::npos) {
        value_candidate = value_candidate.substr(0, last_pos);
    }

    return value_candidate;
}

// Extracts an integer value (key=value)
int extract_int_value(const std::string& line, const std::string& key) {
    size_t key_pos = line.find(key);
    if (key_pos == std::string::npos) return -1;

    size_t eq_pos = line.find('=', key_pos);
    if (eq_pos == std::string::npos) return -1;

    std::stringstream ss(line.substr(eq_pos + 1));
    int value;
    if (ss >> value) {
        return value;
    }
    return -1;
}

// Utility function to trim leading/trailing whitespace
void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
        }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
        }).base(), s.end());
}

class CharacterClass {
public:
    // Name is the display name (Dwarvish Fighter) not the unique key
    std::string name;
    std::string description;
    int level;
    int cost;
    std::string usage;
    std::string movement_type;

    CharacterClass() : level(0), cost(0) {}

    CharacterClass(const fs::path& filepath) : level(0), cost(0) {
        parse_wml_file(filepath);
    }

    void print_stats() const {
        std::cout << "\n*** " << name << " ***\n";
        std::cout << "Description: " << description << "\n";
        std::cout << "Level:       " << level << "\n";
        std::cout << "Cost:        " << cost << " gold\n";
        std::cout << "Usage:       " << usage << "\n";
        std::cout << "Movement:    " << movement_type << "\n";
        std::cout << "-----------------------\n";
    }

private:
    void parse_wml_file(const fs::path& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + filepath.string());
        }

        std::string line;
        while (std::getline(file, line)) {
            // Clean returns and trim whitespace
            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);

            if (line.empty() || line[0] == '#') continue;

            std::string cleaned_line = remove_braced_tags(line);

            // Extract display name from the quoted 'name=' tag
            if (name.empty()) {
                name = extract_quoted_value(cleaned_line, "name=");
            }
            if (description.empty()) description = extract_quoted_value(cleaned_line, "description=");
            if (usage.empty()) usage = extract_unquoted_string_value(cleaned_line, "usage=");
            if (movement_type.empty()) movement_type = extract_unquoted_string_value(cleaned_line, "movement_type=");

            // Extract integer properties
            int val;
            if (level == 0 && (val = extract_int_value(cleaned_line, "level=")) != -1) level = val;
            if (cost == 0 && (val = extract_int_value(cleaned_line, "cost=")) != -1) cost = val;
        }
    }
};

using UnitMap = HashTable<std::string, CharacterClass>;

void load_all_units(UnitMap& unit_map, const std::string& root_dir) {
    if (!fs::exists(root_dir)) {
        std::cerr << "Error: Directory '" << root_dir << "' not found.\n";
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(root_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cfg") {
            try {
                CharacterClass new_unit(entry.path());

                // Unique key from path (e.g., 'dwarves_fighter')
                std::string dir_name = entry.path().parent_path().filename().string();
                std::string file_name = entry.path().stem().string();
                std::string unique_key = dir_name + "_" + file_name;

                // Normalize key to lowercase for case-insensitive lookup
                std::string lower_key = unique_key;
                std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                    [](unsigned char c) {
                        return std::tolower(c);
                    });

                // unit_map[lower_key] = new_unit;
                unit_map[lower_key] = new_unit;

            }
            catch (const std::exception& e) {
                // Log files that couldn't be parsed
                std::cerr << "Warning: Skipping " << entry.path().filename().string() << " (" << e.what() << ")\n";
            }
        }
    }
}

void run_user_interface(UnitMap& unit_map) {
    std::cout << "\n--- Wesnoth Unit Finder ---\n";
    std::cout << "Loaded " << unit_map.get_items() << " unit classes.\n";
    std::cout << "---------------------------\n";
    std::string unit_name;

    while (true) {
        std::cout << "Search uses unit type, followed by unit name. (Example: dwarves_fighter)\n";
        std::cout << "\nEnter Unit Name (Use _ for Spaces or 'quit' to exit): ";
        std::getline(std::cin, unit_name);

        trim(unit_name);
        // Normalize search key to lowercase
        std::transform(unit_name.begin(), unit_name.end(), unit_name.begin(),
            [](unsigned char c) {
                return std::tolower(c);
            });

        if (unit_name == "quit" || unit_name == "exit") {
            break;
        }
        if (unit_name.empty()) {
            continue;
        }

        if (unit_map.contains(unit_name)) {
            unit_map[unit_name].print_stats();
        }
        else {
            std::cout << "Unit '" << unit_name << "' not found.\n";
        }
    }
    std::cout << "Exiting program. Goodbye!\n";
}

int main() {
    UnitMap unit_map;

    load_all_units(unit_map, WESNOTH_UNITS_PATH);

    if (unit_map.get_items() > 0) {
        run_user_interface(unit_map);
    }
    else {
        std::cerr << "\nFATAL: No units were loaded. Check the path and file structure.\n";
    }

    return 0;
}
