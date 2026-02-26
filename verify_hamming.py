def hamming1511_check(d):
    c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8] ^ d[11]
    c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9] ^ d[12]
    c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10] ^ d[13]
    c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10] ^ d[14]
    return c0, c1, c2, c3

def hamming1511_correct(d):
    c0, c1, c2, c3 = hamming1511_check(d)
    n = (c3 << 3) | (c2 << 2) | (c1 << 1) | c0
    if n == 0: return d
    
    mapping = {
        0x09: 0, 0x0B: 1, 0x0F: 2, 0x07: 3, 0x0E: 4, 0x05: 5, 0x0A: 6, 0x0D: 7, 0x03: 8, 0x06: 9, 0x0C: 10,
        0x01: 11, 0x02: 12, 0x04: 13, 0x08: 14
    }
    if n in mapping:
        idx = mapping[n]
        d[idx] ^= 1
    return d

# Test Hamming 15,11
data = [0]*15
# Set some data bits
data[0] = 1
data[2] = 1
# Calculate parity bits manually using the equations
p0 = data[0] ^ data[1] ^ data[2] ^ data[3] ^ data[5] ^ data[7] ^ data[8]
p1 = data[1] ^ data[2] ^ data[3] ^ data[4] ^ data[6] ^ data[8] ^ data[9]
p2 = data[2] ^ data[3] ^ data[4] ^ data[5] ^ data[7] ^ data[9] ^ data[10]
p3 = data[0] ^ data[1] ^ data[2] ^ data[4] ^ data[6] ^ data[7] ^ data[10]
data[11] = p0
data[12] = p1
data[13] = p2
data[14] = p3

print(f"Original codeword: {data}")
c = hamming1511_check(data)
print(f"Check of valid codeword: {c} (should be (0,0,0,0))")

# Inject error
data_err = list(data)
data_err[2] ^= 1
print(f"Codeword with error at bit 2: {data_err}")
c_err = hamming1511_check(data_err)
n_err = (c_err[3] << 3) | (c_err[2] << 2) | (c_err[1] << 1) | c_err[0]
print(f"Syndrome: {bin(n_err)} ({n_err})")

data_fixed = hamming1511_correct(data_err)
print(f"Fixed codeword: {data_fixed}")
print(f"Success: {data_fixed == data}")

# Test Hamming 7,4
def hamming74_check(t):
    s0 = t[0] ^ t[1] ^ t[2] ^ t[4]
    s1 = t[1] ^ t[2] ^ t[3] ^ t[5]
    s2 = t[0] ^ t[1] ^ t[3] ^ t[6]
    return s0, s1, s2

def hamming74_correct(t):
    s0, s1, s2 = hamming74_check(t)
    s = (s2 << 2) | (s1 << 1) | s0
    if s == 0: return t
    mapping = {5: 0, 7: 1, 3: 2, 6: 3, 1: 4, 2: 5, 4: 6}
    if s in mapping:
        t[mapping[s]] ^= 1
    return t

t = [0, 1, 0, 0, 0, 0, 0] # TC=1 (Slot 2)
# Calculate parity
p0 = t[0] ^ t[1] ^ t[2]
p1 = t[1] ^ t[2] ^ t[3]
p2 = t[0] ^ t[1] ^ t[3]
t[4], t[5], t[6] = p0, p1, p2
print(f"\nTact codeword: {t}")
s0, s1, s2 = hamming74_check(t)
print(f"Check: {s0, s1, s2} (should be (0,0,0))")

t_err = list(t)
t_err[1] ^= 1 # Error in TC bit
print(f"Tact with error in TC: {t_err}")
t_fixed = hamming74_correct(t_err)
print(f"Fixed Tact: {t_fixed}")
print(f"Success: {t_fixed == t}")

