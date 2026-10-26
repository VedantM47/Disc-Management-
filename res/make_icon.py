# Generates app.ico (16/32/48/256 px, PNG-in-ICO) with stdlib only.
# Rounded-square accent background with a white donut ring (a "used vs free" pie glyph).
import math, struct, zlib, os

def render(size):
    ss = 3  # supersampling per axis
    px = bytearray()
    r_out, r_in = 0.31 * size, 0.17 * size
    cx = cy = size / 2.0
    corner = 0.22 * size
    half = size / 2.0 - 0.5
    for y in range(size):
        px.append(0)  # PNG filter byte
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    fx = x + (sx + 0.5) / ss
                    fy = y + (sy + 0.5) / ss
                    # rounded square signed distance
                    qx = abs(fx - cx) - (half - corner)
                    qy = abs(fy - cy) - (half - corner)
                    d = math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - corner
                    if d > 0:
                        continue
                    t = fy / size
                    col = (0 + 40 * t, 103 + 50 * t, 192 + 40 * t)  # #0067C0 -> lighter
                    a = 1.0
                    dr = math.hypot(fx - cx, fy - cy)
                    if r_in <= dr <= r_out:
                        ang = math.degrees(math.atan2(fy - cy, fx - cx))  # -180..180, 0 = right
                        k = 1.0 if not (-80 <= ang <= -15) else 0.0       # "free" wedge, shown dimmed
                        w = 1.0 if k else 0.38
                        col = tuple(c * (1 - w) + 255 * w for c in col)
                    acc[0] += col[0]; acc[1] += col[1]; acc[2] += col[2]; acc[3] += a
            n = ss * ss
            if acc[3] == 0:
                px += bytes((0, 0, 0, 0))
            else:
                px += bytes((int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]), int(255 * acc[3] / n)))
    return bytes(px)

def png(size):
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = render(size)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

sizes = [16, 32, 48, 256]
images = [png(s) for s in sizes]
out = struct.pack('<HHH', 0, 1, len(sizes))
offset = 6 + 16 * len(sizes)
for s, img in zip(sizes, images):
    out += struct.pack('<BBBBHHII', 0 if s == 256 else s, 0 if s == 256 else s, 0, 0, 1, 32, len(img), offset)
    offset += len(img)
out += b''.join(images)
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'app.ico'), 'wb').write(out)
print('app.ico', len(out), 'bytes')
