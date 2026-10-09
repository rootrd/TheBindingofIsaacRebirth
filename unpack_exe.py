import struct
import hashlib

src = r"C:\Users\Administrator\Desktop\The Binding of Isaac Rebirth 1.7.9b\isaac-ng.exe"
dst = r"C:\Users\Administrator\Desktop\The Binding of Isaac Rebirth 1.7.9b\isaac-ng-unpacked.exe"

with open(src, "rb") as f:
    data = bytearray(f.read())

e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
opt = coff + 20

# Read original values
orig_entry = struct.unpack_from("<I", data, opt + 16)[0]
orig_size_of_image = struct.unpack_from("<I", data, opt + 56)[0]
orig_num_sections = struct.unpack_from("<H", data, coff + 2)[0]

print(f"Original EntryRVA: 0x{orig_entry:08X}")
print(f"Original SizeOfImage: 0x{orig_size_of_image:08X}")
print(f"Original NumSections: {orig_num_sections}")

# Expected values from build_vita.py
EXPECTED_ENTRY = 0x005EB83E
EXPECTED_SIZE_OF_IMAGE = 0x0085F000
EXPECTED_SIZE = 8_650_240
EXPECTED_SHA = "31846486979cfa07c8c968221553052c3ff603518681ca11912d445f96ca9404"

# Section table
opt_header_size = struct.unpack_from("<H", data, coff + 16)[0]
section_table_offset = opt + opt_header_size

# .bind is section index 5
bind_table_offset = section_table_offset + 5 * 40

# 1. Fix EntryRVA: 0x0085F310 -> 0x005EB83E
struct.pack_into("<I", data, opt + 16, EXPECTED_ENTRY)
print(f"EntryRVA: 0x{orig_entry:08X} -> 0x{EXPECTED_ENTRY:08X}")

# 2. Fix SizeOfImage: 0x00889000 -> 0x0085F000
struct.pack_into("<I", data, opt + 56, EXPECTED_SIZE_OF_IMAGE)
print(f"SizeOfImage: 0x{orig_size_of_image:08X} -> 0x{EXPECTED_SIZE_OF_IMAGE:08X}")

# 3. Decrement NumberOfSections: 6 -> 5
struct.pack_into("<H", data, coff + 2, 5)
print(f"NumSections: {orig_num_sections} -> 5")

# 4. Zero out .bind section table entry (40 bytes)
for j in range(40):
    data[bind_table_offset + j] = 0
print(f"Zeroed .bind section table entry at offset 0x{bind_table_offset:X}")

# 5. Truncate at .bind RawAddr = 0x83FE00
result = data[:0x83FE00]
print(f"Truncated to {len(result)} bytes")

with open(dst, "wb") as f:
    f.write(result)

final_sha = hashlib.sha256(result).hexdigest()
print(f"\nSize: {len(result)} (expected: {EXPECTED_SIZE})")
print(f"SHA256: {final_sha}")
print(f"Expected: {EXPECTED_SHA}")
print(f"Match: {'YES' if final_sha == EXPECTED_SHA else 'NO'}")
