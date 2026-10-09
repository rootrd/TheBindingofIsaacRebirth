import struct, zlib
M = 0xFFFFFFFF
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/animations.a"
d = open(P, "rb").read()
blob = d[18:18+474]
seed = 0xb807817b

def isaac_init(seed, rounds=4):
    # Bob Jenkins randinit-style: r[0..15] from golden ratio + seed words
    r = [0]*256
    a = 0x9e3779b9
    c = [seed]*1
    # standard readable.c randinit: r[i] = golden ratios, then mix seed in r[0],
    # then 4 full passes over r[] with randval
    golden = [0x1367df5a, 0x95d90059, 0xc3162e65, 0x3bf11aea]  # not used; use GG ratios
    h = [0]*8
    h[0] = 0x1367df5a; h[1] = 0x95d90059; h[2] = 0xc3162e65; h[3] = 0x3bf11aea
    h[4] = 0x1367df5a; h[5] = 0x95d90059; h[6] = 0xc3162e65; h[7] = 0x3bf11aea
    def mix(hh):
        hh[0]^= hh[1]<<11 & M; hh[3]^=hh[2]; hh[5]^= hh[4]<<2 & M
        hh[7]^=hh[6]; hh[6]^= hh[5]>>3 & M; hh[4]^=hh[5]<<9 & M if True else 0
        return hh
    # Too many variants; use the canonical randinit from readable.c instead.
    return None

# Canonical ISAAC from Bob Jenkins' readable.c
GOLD = 0x9e3779b9
def mix8(x):
    x[0]^= x[1]<<11 & M; x[3]^= x[2]; x[5]^= x[4]<<2 & M
    x[7]^= x[6]; x[6]^= (x[5]>>3)&M; x[4]^= x[5]<<9 & M
    x[2]^= x[1]<<3 & M if False else (x[1]<<3)&M; x[0]^=0
    # fallback: use exact readable.c mix below
    return x
def mix_readable(m):
    m[0]^= (m[1]<<11)&M; m[3]^= m[2]; m[5]^= (m[4]<<2)&M
    m[7]^= m[6]; m[6]^= (m[5]>>3)&M; m[4]^= (m[5]<<9)&M
    m[2]^= m[3]>>5 if False else 0
    return m

def randinit(r, seed):
    mm = [0]*8
    a = GOLD
    a = (a ^ (seed)) & M
    for i in range(4):
        # mix mm
        a, mm = mix_round(a, mm)
    return r

# Simpler: implement exactly readable.c randinit
def mix(r, i):
    pass

print("use direct implementation instead")
