import re
import json
import os

guest_funcs_path = r"F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_funcs.h"
functions_json_path = r"C:\Users\Administrator\Desktop\isaac-recompiler\functions.json"
output_path = r"F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_table.c"

with open(guest_funcs_path) as f:
    content = f.read()

decls = re.findall(r'void (sub_[0-9a-fA-F]+)\(CPU', content)
decls_lower = [name.lower() for name in decls]

with open(functions_json_path) as f:
    data = json.load(f)
entry_rva = data["entry_rva"]

with open(output_path, "w") as out:
    out.write("/* GENERATED dispatch table — do not edit. */\n")
    out.write('#include "guest.h"\n')
    out.write('#include "guest_funcs.h"\n\n')

    out.write("static const uint32_t guest_addresses[] = {\n")
    for i, name in enumerate(decls_lower):
        rva = int(name[4:], 16)
        if i % 16 == 0:
            out.write("    ")
        out.write("0x%08X," % rva)
        if i % 16 == 15 or i == len(decls_lower) - 1:
            out.write("\n")
        else:
            out.write(" ")
    out.write("};\n\n")

    out.write("static const guest_fn guest_functions[] = {\n")
    for i, name in enumerate(decls_lower):
        if i % 8 == 0:
            out.write("    ")
        out.write("%s," % name)
        if i % 8 == 7 or i == len(decls_lower) - 1:
            out.write("\n")
        else:
            out.write(" ")
    out.write("};\n\n")

    out.write("void guest_register_all(void)\n")
    out.write("{\n")
    out.write("    guest_register(guest_addresses, guest_functions,\n")
    out.write("                 sizeof(guest_addresses) / sizeof(guest_addresses[0]));\n")
    out.write("}\n\n")

    out.write("guest_fn guest_entry_point(void)\n")
    out.write("{\n")
    out.write("    return sub_%08x;\n" % entry_rva)
    out.write("}\n")

print(f"Generated {output_path}")
print(f"  {len(decls_lower)} functions registered (from guest_funcs.h)")
print(f"  entry_rva = 0x{entry_rva:08X}")
