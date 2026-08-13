#!/usr/bin/env python3
"""Sync proto enum definitions with C++ PacketTypes.h"""
import re
import os
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def parse_proto_enums(proto_dir):
    """Parse enum definitions from .proto files"""
    enums = {}
    for fname in os.listdir(proto_dir):
        if not fname.endswith('.proto'):
            continue
        fpath = os.path.join(proto_dir, fname)
        with open(fpath, 'r', encoding='utf-8') as f:
            content = f.read()
        # Find enum blocks
        for match in re.finditer(r'enum\s+(\w+)\s*\{([^}]+)\}', content):
            enum_name = match.group(1)
            body = match.group(2)
            values = {}
            for line in body.split(';'):
                line = line.strip()
                if '=' in line:
                    parts = line.split('=')
                    name = parts[0].strip()
                    val = parts[1].strip().split()[0]
                    try:
                        values[name] = int(val)
                    except ValueError:
                        pass
            enums[enum_name] = values
    return enums

def parse_cpp_constants(header_path):
    """Parse C++ constant definitions from header"""
    constants = {}
    if not os.path.exists(header_path):
        return constants
    with open(header_path, 'r', encoding='utf-8') as f:
        content = f.read()
    # Match patterns like: constexpr uint16_t Name = value;
    for match in re.finditer(r'constexpr\s+\w+\s+(\w+)\s*=\s*(\d+)', content):
        constants[match.group(1)] = int(match.group(2))
    return constants

def main():
    proto_dir = os.path.join(PROJECT_ROOT, 'proto')
    header_path = os.path.join(PROJECT_ROOT, 'src', 'core', 'include', 'nevo', 'core', 'common', 'PacketTypes.h')

    proto_enums = parse_proto_enums(proto_dir)
    cpp_constants = parse_cpp_constants(header_path)

    mismatches = []
    for enum_name, values in proto_enums.items():
        for name, val in values.items():
            if name in cpp_constants:
                if cpp_constants[name] != val:
                    mismatches.append(f"  {enum_name}::{name}: proto={val}, cpp={cpp_constants[name]}")
            else:
                mismatches.append(f"  {enum_name}::{name}: proto={val}, cpp=MISSING")

    if mismatches:
        print(f"Found {len(mismatches)} enum mismatches between proto and C++:")
        for m in mismatches:
            print(m)
        sys.exit(1)
    else:
        print("All proto enums are in sync with C++ definitions.")
        sys.exit(0)

if __name__ == '__main__':
    main()
