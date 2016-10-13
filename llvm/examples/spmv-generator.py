#!/usr/bin/env python3

from random import random
import struct

size = 50
mat = [[(random(), i) for i in range(size) if random() < 0.1] for _ in range(size)]
v = [random() for _ in range(size)]

def double_to_hex(f):
    return hex(struct.unpack('<Q', struct.pack('<d', f))[0])

values = [size, size]
entries = []
for row in mat:
    values.append(len(entries) + size)
    entries += row
    values.append(len(entries) + size)

for val, col in entries:
    values.append(col)
    values.append(double_to_hex(val))

out = "uint64_t rawMatrix[] = {" + ",".join([str(v) for v in values]) + "};"
out += "double rawVector[] = {" + ",".join([str(v) for v in v]) + "};"

with open("spmv-matrix.gen", "w") as f: f.write(out)
