"""Generate the multiresolution aeri Windows icon, using only Python's stdlib."""
from pathlib import Path
import math
import struct
import zlib

def png(size):
    factor = 4
    width = size * factor
    pixels = bytearray(width * width)
    paths = [[(9,25),(28,10),(55,12),(55,35)], [(12,45),(29,34),(46,34),(46,54)]]
    radius = 2.5 * width / 64
    for path in paths:
        for step in range(width * 2 + 1):
            t = step / (width * 2)
            u = 1 - t
            cx, cy = [sum(weight * point[axis] for weight, point in zip([u**3,3*u*u*t,3*u*t*t,t**3],path)) * width / 64 for axis in [0,1]]
            for y in range(max(0,math.floor(cy-radius)),min(width,math.ceil(cy+radius)+1)):
                for x in range(max(0,math.floor(cx-radius)),min(width,math.ceil(cx+radius)+1)):
                    if (x-cx)**2+(y-cy)**2 <= radius**2:
                        pixels[y*width+x] = 255
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        for x in range(size):
            alpha = sum(pixels[(y*factor+dy)*width+x*factor+dx] for dy in range(factor) for dx in range(factor)) // factor**2
            raw.extend((101,215,199,alpha))
    def chunk(kind, data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

sizes = [16,24,32,48,64,128,256]
images = [png(size) for size in sizes]
offset = 6 + 16*len(sizes)
entries = bytearray()
for size, data in zip(sizes, images):
    entries.extend(struct.pack('<BBBBHHII',size%256,size%256,0,0,1,32,len(data),offset))
    offset += len(data)
destination = Path(__file__).resolve().parent.parent/'assets'/'aeri.ico'
destination.write_bytes(struct.pack('<HHH',0,1,len(sizes))+entries+b''.join(images))
print(destination)
