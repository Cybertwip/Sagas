from __future__ import annotations

import math
from typing import Sequence

from .types import Vec3

Matrix = list[float]


def identity() -> Matrix:
    return [1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0]


def multiply(a: Matrix, b: Matrix) -> Matrix:
    out = [0.0] * 16
    for row in range(4):
        for col in range(4):
            total = 0.0
            for k in range(4):
                total += a[row * 4 + k] * b[k * 4 + col]
            out[row * 4 + col] = total
    return out


def translation(v: Sequence[float]) -> Matrix:
    out = identity()
    out[3] = v[0]
    out[7] = v[1]
    out[11] = v[2]
    return out


def scale(v: Sequence[float]) -> Matrix:
    out = identity()
    out[0] = v[0]
    out[5] = v[1]
    out[10] = v[2]
    return out


def rotation(v: Sequence[float]) -> Matrix:
    cx, sx = math.cos(v[0]), math.sin(v[0])
    cy, sy = math.cos(v[1]), math.sin(v[1])
    cz, sz = math.cos(v[2]), math.sin(v[2])
    x = [1, 0, 0, 0, 0, cx, -sx, 0, 0, sx, cx, 0, 0, 0, 0, 1]
    y = [cy, 0, sy, 0, 0, 1, 0, 0, -sy, 0, cy, 0, 0, 0, 0, 1]
    z = [cz, -sz, 0, 0, sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    return multiply(multiply(z, y), x)


def transform(m: Matrix, v: Vec3) -> Vec3:
    return Vec3(m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3],
                m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7],
                m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11])


def transform_direction(m: Matrix, v: Vec3) -> Vec3:
    return Vec3(m[0] * v.x + m[1] * v.y + m[2] * v.z,
                m[4] * v.x + m[5] * v.y + m[6] * v.z,
                m[8] * v.x + m[9] * v.y + m[10] * v.z)


def transform_normal(m: Matrix, v: Vec3) -> Vec3:
    a, b, c = m[0], m[1], m[2]
    d, e, f = m[4], m[5], m[6]
    g, h, i = m[8], m[9], m[10]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    if abs(det) < 1.0e-8:
        return transform_direction(m, v)
    inv = 1.0 / det
    return Vec3(((e * i - f * h) * v.x + (f * g - d * i) * v.y + (d * h - e * g) * v.z) * inv,
                ((c * h - b * i) * v.x + (a * i - c * g) * v.y + (b * g - a * h) * v.z) * inv,
                ((b * f - c * e) * v.x + (c * d - a * f) * v.y + (a * e - b * d) * v.z) * inv)


def sub(a: Vec3, b: Vec3) -> Vec3:
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z)


def add(a: Vec3, b: Vec3) -> Vec3:
    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z)


def scale_vec(a: Vec3, n: float) -> Vec3:
    return Vec3(a.x * n, a.y * n, a.z * n)


def dot(a: Vec3, b: Vec3) -> float:
    return a.x * b.x + a.y * b.y + a.z * b.z


def cross(a: Vec3, b: Vec3) -> Vec3:
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)


def length(v: Vec3) -> float:
    return math.sqrt(max(1e-12, dot(v, v)))


def normalize(v: Vec3) -> Vec3:
    scale_n = 1.0 / length(v)
    return Vec3(v.x * scale_n, v.y * scale_n, v.z * scale_n)


def look_at(eye: Vec3, at: Vec3, up: Vec3) -> Matrix:
    forward = normalize(sub(at, eye))
    right = normalize(cross(forward, up if abs(dot(forward, up)) < 0.999 else Vec3(1, 0, 0)))
    true_up = cross(right, forward)
    return [
        right.x, right.y, right.z, -dot(right, eye),
        true_up.x, true_up.y, true_up.z, -dot(true_up, eye),
        -forward.x, -forward.y, -forward.z, dot(forward, eye),
        0.0, 0.0, 0.0, 1.0,
    ]


def perspective(fov_y_deg: float, aspect: float, near: float, far: float) -> Matrix:
    f = 1.0 / math.tan(math.radians(max(fov_y_deg, 1.0)) * 0.5)
    nf = 1.0 / (near - far)
    return [
        f / max(aspect, 1e-6), 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far + near) * nf, 2 * far * near * nf,
        0, 0, -1, 0,
    ]


def column_major(m: Matrix) -> list[float]:
    return [m[col * 4 + row] if False else m[row + col * 4] for col in range(4) for row in range(4)]
