#!/usr/bin/env python3
"""SOMA_PARCHE_021_FPU: resultado esperado de /bin/ssetest.elf, calculado en el host con aritmetica EXACTA y el redondeo
que corresponda: sum_{k=1..N} 1/(k*k) en doble precision, con cada division y cada suma redondeadas
  modo 0: al mas cercano (par)   modo 1: hacia +infinito   modo 2: hacia cero (= hacia abajo, todo es positivo)
Devuelve los 64 bits del resultado (comparables con el 'sum=' que imprime el programa)."""
from __future__ import annotations

import math
import struct
from fractions import Fraction


def _round(fr: Fraction, mode: int) -> float:
    f = float(fr)                                  # al mas cercano, correctamente redondeado
    if mode == 1 and Fraction(f) < fr:
        f = math.nextafter(f, math.inf)
    elif mode == 2 and Fraction(f) > fr:
        f = math.nextafter(f, -math.inf)
    return f


def series_bits(mode: int, n: int) -> int:
    if mode == 0:                                  # camino rapido: es exactamente la aritmetica nativa
        s = 0.0
        for k in range(1, n + 1):
            s += 1.0 / (k * k)
    else:
        s = 0.0
        for k in range(1, n + 1):
            q = _round(Fraction(1, k * k), mode)
            s = _round(Fraction(s) + Fraction(q), mode)
    return struct.unpack("<Q", struct.pack("<d", s))[0]


def series_exact_nearest(n: int) -> int:
    """El mismo calculo del modo 0 pero con fracciones: valida el emulador de redondeo contra la aritmetica nativa."""
    s = 0.0
    for k in range(1, n + 1):
        q = _round(Fraction(1, k * k), 0)
        s = _round(Fraction(s) + Fraction(q), 0)
    return struct.unpack("<Q", struct.pack("<d", s))[0]
