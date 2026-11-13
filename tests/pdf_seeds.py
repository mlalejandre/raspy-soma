#!/usr/bin/env python3
"""PARCHE 088: PDF de prueba hechos a mano (solo biblioteca estandar): xref clasica y xref en flujo con flujo de objetos."""
import sys, os, zlib
out = sys.argv[1] if len(sys.argv) > 1 else 'build/tests/pdf'
os.makedirs(out, exist_ok=True)
content = (b"BT /F1 24 Tf 72 720 Td (Hola SOMA) Tj ET\n"
           b"BT /F1 12 Tf 72 690 Td [(Ac) 20 (entos: ) -300 (\\341\\351\\355\\363\\372 \\361)] TJ ET\n"
           b"0 0 1 rg 72 500 200 100 re f 1 0 0 RG 4 w 300 500 m 500 600 l S\n"
           b"q 100 0 0 50 72 400 cm /Im1 Do Q\n")
img = bytes([x * 60 % 256 for x in range(4 * 2 * 3)])
def classic():
    objs = [b"<< /Type /Catalog /Pages 2 0 R >>", b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> /XObject << /Im1 6 0 R >> >> >>",
            None, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>", None]
    c = zlib.compress(content)
    objs[3] = b"<< /Length %d /Filter /FlateDecode >>\nstream\n" % len(c) + c + b"\nendstream"
    ic = zlib.compress(img)
    objs[5] = b"<< /Type /XObject /Subtype /Image /Width 4 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode /Length %d >>\nstream\n" % len(ic) + ic + b"\nendstream"
    d = b"%PDF-1.4\n"; offs = []
    for i, o in enumerate(objs):
        offs.append(len(d)); d += b"%d 0 obj\n" % (i + 1) + o + b"\nendobj\n"
    x = len(d); d += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    for o in offs: d += b"%010d 00000 n \n" % o
    d += b"trailer\n<< /Size %d /Root 1 0 R /Info << /Title (Prueba SOMA) >> >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, x)
    return d
def xrefstream():
    small = [b"<< /Type /Catalog /Pages 2 0 R >>", b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
             b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>"]
    c = zlib.compress(content.split(b"q 100")[0])
    hdr = b" ".join(b"%d %d" % (i + 1, 0) for i in range(3))
    body = b""; pos = []
    for o in small: pos.append(len(body)); body += o + b" "
    hdr = b" ".join(b"%d %d" % (i + 1, pos[i]) for i in range(3)) + b" "
    stm = zlib.compress(hdr + body)
    d = b"%PDF-1.5\n"; offs = {}
    offs[4] = len(d); d += b"4 0 obj\n<< /Length %d /Filter /FlateDecode >>\nstream\n" % len(c) + c + b"\nendstream\nendobj\n"
    offs[5] = len(d); d += b"5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\nendobj\n"
    offs[6] = len(d); d += b"6 0 obj\n<< /Type /ObjStm /N 3 /First %d /Length %d /Filter /FlateDecode >>\nstream\n" % (len(hdr), len(stm)) + stm + b"\nendstream\nendobj\n"
    offs[7] = len(d)
    rows = [bytes([0, 0, 0, 0xFF])]
    for i in range(1, 4): rows.append(bytes([2]) + (6).to_bytes(2, 'big') + bytes([i - 1]))
    for i in (4, 5, 6, 7): rows.append(bytes([1]) + offs[i].to_bytes(2, 'big') + b"\0")
    xs = zlib.compress(b"".join(rows))
    d += b"7 0 obj\n<< /Type /XRef /Size 8 /W [1 2 1] /Root 1 0 R /Length %d /Filter /FlateDecode >>\nstream\n" % len(xs) + xs + b"\nendstream\nendobj\n"
    d += b"startxref\n%d\n%%%%EOF\n" % offs[7]
    return d
open(os.path.join(out, 'clasico.pdf'), 'wb').write(classic())
open(os.path.join(out, 'xrefstm.pdf'), 'wb').write(xrefstream())
print('PDF de prueba en', out)
