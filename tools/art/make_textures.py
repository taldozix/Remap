"""Gera os conjuntos de texturas PBR do jogo em assets/textures/.

Uso: python3 tools/art/make_textures.py [nome ...]
Cada conjunto vira <nome>_albedo.png, <nome>_normal.png e <nome>_orm.png.
Os tamanhos físicos (metros por repetição) ficam em assets/textures/textures.json,
lido pelo jogo (Mat.surface) para aplicar a escala certa.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from texlib import (blur, cavity_ao, cell_voronoi, col, grid, lerp, normal_from_height,
                    per_id, rng, save_set, smoothstep, spectral)

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "assets", "textures")
N = 1024
SETS = {}


def texture(name, tile_m, size=512, strength=1.0):
    def deco(fn):
        SETS[name] = (fn, tile_m, size, strength)
        return fn
    return deco


def finish(name, albedo, height, rough, metal=None, ao_mul=None, cavity=0.02):
    fn, tile_m, size, strength = SETS[name]
    nrm = normal_from_height(height, tile_m, strength)
    ao = cavity_ao(height, tile_m, cavity, 10.0) * 0.6 + 0.4
    if ao_mul is not None:
        ao = ao * ao_mul
    save_set(OUT, name, np.clip(albedo, 0, 1), nrm, np.clip(ao, 0, 1), np.clip(rough, 0, 1), metal, size)


# ------------------------------------------------------------------ alvenaria
@texture("brick", 1.2, 512, 1.4)
def brick():
    X, Y = grid(N); X *= 1.2; Y *= 1.2
    rows, per_row, bh, bw, m = 16, 5, 0.075, 0.24, 0.011
    jit = (spectral(N, 11, 1.4, 8) - 0.5) * 0.004
    row = np.floor(Y / bh).astype(int)
    off = np.where(row % 2 == 1, bw * 0.5, 0.0) + (row % 3 == 2) * 0.03
    xx = (X + off) % 1.2
    bi = np.floor(xx / bw).astype(int) % per_row
    u = xx % bw; v = Y % bh
    d = np.minimum.reduce([u - m / 2, bw - m / 2 - u, v - m / 2, bh - m / 2 - v]) + jit
    inside = d > 0
    bid = row * per_row + bi
    r1 = per_id(bid, 21, rows * per_row); r2 = per_id(bid, 22, rows * per_row)
    palette = np.array([col("8e4a33"), col("a35a3c"), col("7a3d2c"), col("9b5236"), col("6b3628"), col("b06a4a")])
    base = palette[np.floor(r1 * len(palette)).astype(int) % len(palette)]
    fine = spectral(N, 12, 1.0, 60)
    blot = spectral(N, 13, 2.0, 6)
    brick_col = base * (0.86 + 0.22 * fine[..., None]) * (0.92 + 0.14 * blot[..., None])
    brick_col *= (0.9 + 0.2 * r2)[..., None]
    burnt = smoothstep(0.6, 0.95, spectral(N, 14, 1.6, 10)) * 0.35
    brick_col = lerp(brick_col, col("3b2219"), burnt * (r2 > 0.6))
    mortar = col("a59c8e") * (0.82 + 0.25 * spectral(N, 15, 0.6, 80))[..., None]
    t = smoothstep(0.0, 0.003, d)
    albedo = lerp(mortar, brick_col, t)
    h = 0.006 + 0.008 * smoothstep(0.0, 0.006, d) + 0.0012 * fine + 0.002 * spectral(N, 16, 1.6, 12)
    h = np.where(inside, h, 0.002 + 0.0008 * spectral(N, 17, 0.5, 120))
    rough = lerp(0.97, 0.84, t) - 0.05 * fine
    finish("brick", albedo, h, rough, cavity=0.01)


@texture("plaster", 2.0, 512, 1.6)
def plaster():
    big = spectral(N, 31, 2.2, 2)
    trowel = spectral(N, 32, 1.7, 6)
    sand = spectral(N, 33, 0.3, 200)
    stain = spectral(N, 34, 2.4, 1)
    h = 0.0018 * trowel + 0.0004 * sand + 0.001 * big
    v = 0.9 + 0.06 * trowel + 0.05 * (sand - 0.5) - 0.07 * smoothstep(0.55, 1.0, stain)
    albedo = np.stack([v, v * 0.99, v * 0.975], -1) * 0.92
    rough = 0.88 + 0.08 * sand
    finish("plaster", albedo, h, rough, cavity=0.01)


@texture("concrete", 2.0, 512, 1.2)
def concrete():
    f1, f2, ids, _, _ = cell_voronoi(N, 140, 41, 0.9)
    pr = per_id(ids, 42, 140 * 140)
    pits = (f1 < 0.18) & (pr > 0.86)
    blot = spectral(N, 43, 2.2, 2)
    mid = spectral(N, 44, 1.4, 12)
    sand = spectral(N, 45, 0.2, 250)
    v = 0.52 + 0.08 * blot + 0.06 * mid + 0.06 * (sand - 0.5)
    albedo = np.stack([v, v * 0.985, v * 0.96], -1)
    albedo = np.where(pits[..., None], albedo * 0.55, albedo)
    h = 0.0012 * mid + 0.0004 * sand - 0.0015 * pits
    rough = 0.9 - 0.06 * blot
    finish("concrete", albedo, h, rough)


@texture("wood_siding", 2.0, 512, 1.5)
def wood_siding():
    X, Y = grid(N); Y2 = Y * 2.0
    board = 0.2
    ri = np.floor(Y2 / board).astype(int)
    v = (Y2 % board) / board
    grain = spectral(N, 51, 1.2, 4, aniso=(0.06, 1.0))
    h = 0.006 * v + 0.0008 * grain
    h -= 0.004 * (v < 0.04)
    shade = per_id(ri, 52, 20)
    base = np.array([0.93, 0.92, 0.88])
    albedo = base * (0.86 + 0.1 * grain + 0.06 * shade)[..., None]
    albedo *= (1.0 - 0.35 * (v < 0.05))[..., None]
    rough = 0.75 + 0.1 * grain
    finish("wood_siding", albedo, h, rough)


# ------------------------------------------------------------------ chão de rua
@texture("asphalt", 4.0, 1024, 1.0)
def asphalt():
    aggr = spectral(N, 61, 0.2, 180)
    stones = smoothstep(0.7, 0.8, aggr)
    mid = spectral(N, 62, 1.6, 6)
    big = spectral(N, 63, 2.4, 1)
    f1, f2, ids, _, _ = cell_voronoi(N, 5, 64, 1.0)
    edge = f2 - f1
    wob = spectral(N, 65, 1.6, 10)
    crack_mask = smoothstep(0.55, 0.75, spectral(N, 66, 2.0, 2))
    crack = (1.0 - smoothstep(0.004, 0.012 + 0.01 * wob, edge)) * crack_mask
    seal = (1.0 - smoothstep(0.02, 0.06, edge)) * crack_mask * (spectral(N, 67, 2.0, 3) > 0.5)
    v = 0.15 + 0.04 * mid + 0.03 * big + 0.12 * stones
    albedo = np.stack([v, v * 1.0, v * 1.03], -1)
    albedo = lerp(albedo, np.array([0.07, 0.07, 0.075]), np.clip(seal * 0.8 + crack, 0, 1))
    h = 0.002 * aggr + 0.0015 * stones + 0.0006 * mid - 0.004 * crack
    rough = 0.92 - 0.12 * stones - 0.25 * seal
    finish("asphalt", albedo, h, rough, cavity=0.008)


@texture("sidewalk", 2.4, 512, 1.3)
def sidewalk():
    X, Y = grid(N); X *= 2.4; Y *= 2.4
    slab = 0.6
    u = X % slab; v = Y % slab
    d = np.minimum.reduce([u, slab - u, v, slab - v])
    sid = (np.floor(X / slab) + 4 * np.floor(Y / slab)).astype(int)
    tone = per_id(sid, 71, 16)
    mid = spectral(N, 72, 1.5, 10); sand = spectral(N, 73, 0.2, 220)
    stain = spectral(N, 74, 2.2, 2)
    v0 = 0.6 + 0.06 * (tone - 0.5) + 0.05 * mid + 0.05 * (sand - 0.5) - 0.06 * smoothstep(0.6, 1, stain)
    albedo = np.stack([v0, v0 * 0.98, v0 * 0.94], -1)
    joint = 1.0 - smoothstep(0.003, 0.007, d)
    albedo = lerp(albedo, np.array([0.28, 0.27, 0.25]), joint * 0.8)
    h = 0.004 * smoothstep(0.0, 0.01, d) + 0.0008 * mid + 0.0003 * sand
    rough = 0.88 - 0.05 * mid
    finish("sidewalk", albedo, h, rough)


@texture("pedra_portuguesa", 2.0, 1024, 1.6)
def pedra_portuguesa():
    cells = 44
    f1, f2, ids, vx, vy = cell_voronoi(N, cells, 81, 0.85)
    X, Y = grid(N)
    # Centro da pedra para decidir a cor sem partir pedras (ondas de Copacabana).
    cx = X + vx / cells; cy = Y + vy / cells
    wave = (cy * 2.0 + 0.22 * np.sin(cx * 2 * np.pi * 2.0)) % 1.0
    black = wave < 0.5
    edge = f2 - f1
    stone = smoothstep(0.06, 0.2, edge)
    r = per_id(ids, 82, cells * cells)
    tex = spectral(N, 83, 0.8, 90)
    white_c = np.array([0.76, 0.74, 0.69]) * (0.88 + 0.12 * r)[..., None]
    black_c = np.array([0.27, 0.27, 0.28]) * (0.8 + 0.4 * r)[..., None]
    sc = np.where(black[..., None], black_c, white_c) * (0.9 + 0.12 * tex)[..., None]
    grout = np.array([0.42, 0.38, 0.32]) * (0.8 + 0.3 * tex)[..., None]
    albedo = lerp(grout, sc, stone)
    h = 0.008 * smoothstep(0.0, 0.35, edge) + 0.001 * tex
    rough = lerp(0.95, 0.7 - 0.1 * black, stone)
    finish("pedra_portuguesa", albedo, h, rough, cavity=0.008)


@texture("ground", 4.0, 512, 1.0)
def ground():
    soil = spectral(N, 91, 1.2, 20); big = spectral(N, 92, 2.0, 2)
    pebble = smoothstep(0.82, 0.88, spectral(N, 93, 0.3, 150))
    grassy = smoothstep(0.35, 0.7, spectral(N, 94, 1.8, 3))
    dirt = np.array([0.33, 0.25, 0.17]) * (0.8 + 0.3 * soil)[..., None]
    green = np.array([0.24, 0.27, 0.13]) * (0.75 + 0.4 * spectral(N, 95, 0.6, 60))[..., None]
    albedo = lerp(dirt, green, grassy)
    albedo = lerp(albedo, np.array([0.5, 0.47, 0.42]), pebble * (1 - grassy))
    h = 0.006 * soil + 0.004 * pebble + 0.004 * big
    rough = 0.95 - 0.1 * pebble
    finish("ground", albedo, h, rough)


@texture("dirt", 3.0, 512, 1.2)
def dirt():
    soil = spectral(N, 101, 1.3, 16); big = spectral(N, 102, 2.0, 2)
    f1, f2, ids, _, _ = cell_voronoi(N, 70, 103, 0.9)
    pr = per_id(ids, 104, 4900)
    peb = (1 - smoothstep(0.15, 0.32, f1)) * (pr > 0.8)
    dirt_c = np.array([0.42, 0.3, 0.2]) * (0.78 + 0.3 * soil + 0.1 * big)[..., None]
    albedo = lerp(dirt_c, np.array([0.55, 0.5, 0.44]) * (0.8 + 0.3 * pr)[..., None], peb)
    h = 0.006 * soil + 0.006 * peb + 0.004 * big
    finish("dirt", albedo, h, 0.95 - 0.15 * peb)


# ------------------------------------------------------------------ telhados
@texture("roof_tiles", 2.0, 1024, 1.0)
def roof_tiles():
    X, Y = grid(N); X *= 2.0; Y *= 2.0
    cw, rl = 0.2, 0.25   # largura do canal e comprimento visível da telha
    ri = np.floor(Y / rl).astype(int)
    off = np.where(ri % 2 == 1, 0.0, 0.0)
    ci = np.floor((X + off) / cw).astype(int)
    u = ((X + off) % cw) / cw
    v = (Y % rl) / rl
    prof = np.sin(u * np.pi)  # telha capa (convexa)
    tid = ri * 10 + ci % 10
    r = per_id(tid, 111, 100); r2 = per_id(tid, 112, 100)
    h = 0.03 * prof + 0.012 * v + 0.001 * spectral(N, 113, 1.0, 40)
    # borda inferior: espessura da telha sobreposta
    lip = smoothstep(0.92, 1.0, v)
    h -= 0.012 * lip
    gap = 1.0 - smoothstep(0.0, 0.06, np.minimum(u, 1 - u))
    base = lerp(col("a54a2a"), col("c46a3a"), r)[..., :] * (0.85 + 0.15 * r2)[..., None]
    grain = spectral(N, 114, 1.0, 50)
    albedo = base * (0.85 + 0.2 * grain)[..., None] * (0.75 + 0.3 * prof)[..., None]
    moss = smoothstep(0.62, 0.85, spectral(N, 115, 1.6, 6)) * (0.4 + 0.6 * gap)
    albedo = lerp(albedo, np.array([0.24, 0.26, 0.15]), moss * 0.55)
    albedo = lerp(albedo, albedo * 0.45, lip * 0.7 + gap * 0.5)
    lich = smoothstep(0.86, 0.9, spectral(N, 116, 0.6, 80))
    albedo = lerp(albedo, np.array([0.7, 0.68, 0.55]), lich * 0.6)
    rough = 0.8 + 0.12 * moss
    finish("roof_tiles", albedo, h, rough, cavity=0.03)


@texture("roof_fiber", 1.77, 512, 1.0)
def roof_fiber():
    X, Y = grid(N); X *= 1.77
    prof = np.sin(X / 0.177 * 2 * np.pi)
    dirt = spectral(N, 121, 1.8, 3, aniso=(1.0, 0.3))
    streak = spectral(N, 122, 1.0, 20, aniso=(1.0, 0.08))
    v = 0.6 + 0.05 * prof - 0.12 * dirt * (0.5 + 0.5 * streak)
    albedo = np.stack([v, v * 0.99, v * 0.95], -1)
    moss = smoothstep(0.7, 0.9, spectral(N, 123, 1.5, 5)) * (prof < 0)
    albedo = lerp(albedo, np.array([0.25, 0.27, 0.16]), moss * 0.6)
    h = 0.025 * prof + 0.0005 * spectral(N, 124, 0.4, 150)
    finish("roof_fiber", albedo, h, 0.9 - 0.05 * prof)


@texture("roof_flat", 3.0, 512, 1.2)
def roof_flat():
    grav = spectral(N, 131, 0.2, 200); stain = spectral(N, 132, 2.0, 2)
    peb = smoothstep(0.65, 0.8, grav)
    v = 0.33 + 0.12 * peb + 0.08 * (stain - 0.5)
    albedo = np.stack([v, v * 0.98, v * 0.96], -1)
    h = 0.003 * grav + 0.002 * peb
    finish("roof_flat", albedo, h, 0.93)


# ------------------------------------------------------------------ interiores
@texture("wood_floor", 2.0, 512, 1.3)
def wood_floor():
    X, Y = grid(N); X *= 2.0; Y *= 2.0
    pw = 0.143
    ri = np.floor(Y / pw).astype(int)
    r = rng(141)
    # cada fileira tem 2 emendas em posições aleatórias
    cuts = r.random((14, 2)) * 2.0
    cuts.sort(axis=1)
    c0 = cuts[ri % 14, 0]; c1 = cuts[ri % 14, 1]
    seg = np.where((X >= c0) & (X < c1), 1, 0)
    pid = ri * 2 + seg
    v = (Y % pw) / pw
    du = np.minimum(np.abs(X - c0), np.abs(X - c1))
    du = np.minimum(du, np.minimum(np.abs(X - c0 + 2.0), np.abs(X - c1 - 2.0)))
    grain_a = spectral(N, 142, 1.1, 3, aniso=(0.05, 1.0))
    grain_b = spectral(N, 143, 1.0, 30, aniso=(0.08, 1.0))
    sh = per_id(pid, 144, 28)
    ring = np.sin((Y * 60 + grain_a * 12 + sh * 20) ) * 0.5 + 0.5
    base = lerp(col("8a5a35"), col("b07d4f"), sh)
    albedo = base * (0.7 + 0.3 * grain_b + 0.12 * ring + 0.1 * grain_a)[..., None]
    seam = (1 - smoothstep(0.0, 0.03, np.minimum(v, 1 - v))) + (1 - smoothstep(0.0, 0.003, du))
    albedo = lerp(albedo, albedo * 0.35, np.clip(seam, 0, 1))
    h = 0.002 * (1 - np.clip(seam, 0, 1)) + 0.0003 * grain_b
    rough = 0.55 + 0.15 * grain_b + 0.2 * np.clip(seam, 0, 1)
    finish("wood_floor", albedo, h, rough)


@texture("porcelain", 1.2, 512, 1.0)
def porcelain():
    X, Y = grid(N); X *= 1.2; Y *= 1.2
    t = 0.6
    u = X % t; v = Y % t
    d = np.minimum.reduce([u, t - u, v, t - v])
    tid = (np.floor(X / t) + 2 * np.floor(Y / t)).astype(int)
    marble = spectral(N, 151, 1.6, 3)
    vein = 1 - smoothstep(0.0, 0.03, np.abs(spectral(N, 152, 1.8, 4) - 0.5))
    tone = per_id(tid, 153, 4)
    base = col("d8cfc0") * (0.95 + 0.05 * tone + 0.05 * marble)[..., None]
    albedo = lerp(base, base * 0.85, vein * 0.4)
    grout = 1 - smoothstep(0.0015, 0.0035, d)
    albedo = lerp(albedo, np.array([0.55, 0.52, 0.48]), grout)
    h = 0.002 * smoothstep(0.0, 0.004, d)
    rough = lerp(0.22, 0.9, grout)
    finish("porcelain", albedo, h, rough, cavity=0.005)


@texture("wall_tiles", 0.6, 512, 1.0)
def wall_tiles():
    X, Y = grid(N); X *= 0.6; Y *= 0.6
    t = 0.15
    u = X % t; v = Y % t
    d = np.minimum.reduce([u, t - u, v, t - v])
    tid = (np.floor(X / t) + 4 * np.floor(Y / t)).astype(int)
    tone = per_id(tid, 161, 16)
    base = np.array([0.9, 0.91, 0.9]) * (0.96 + 0.04 * tone)[..., None]
    grout = 1 - smoothstep(0.001, 0.0025, d)
    albedo = lerp(base, np.array([0.62, 0.6, 0.56]), grout)
    bev = smoothstep(0.0, 0.006, d)
    h = 0.002 * bev
    finish("wall_tiles", albedo, h, lerp(0.12, 0.85, grout), cavity=0.004)


@texture("wood", 1.0, 512, 1.0)
def wood():
    X, Y = grid(N)
    warp = spectral(N, 171, 1.6, 2, aniso=(0.25, 1.0))
    rings = np.sin((Y * 38 + warp * 9) * 2 * np.pi * 0.5) * 0.5 + 0.5
    fib = spectral(N, 172, 0.8, 40, aniso=(0.03, 1.0))
    base = lerp(col("9a6a3e"), col("c19364"), rings * 0.6 + fib * 0.4)
    albedo = base * (0.9 + 0.1 * spectral(N, 173, 2, 2))[..., None]
    h = 0.0006 * fib + 0.0003 * rings
    finish("wood", albedo, h, 0.55 + 0.15 * fib)


@texture("fabric", 0.5, 512, 2.0)
def fabric():
    X, Y = grid(N); X *= 0.5; Y *= 0.5
    k = 2 * np.pi / 0.004
    weave = (np.sin(X * k) * np.sin(Y * k)) * 0.5 + 0.5
    fuzz = spectral(N, 181, 0.5, 120)
    mott = spectral(N, 182, 1.6, 4)
    v = 0.78 + 0.08 * weave + 0.08 * fuzz + 0.05 * mott
    albedo = np.stack([v, v, v * 0.98], -1)
    h = 0.0004 * weave + 0.0002 * fuzz
    finish("fabric", albedo, h, 0.95)


@texture("metal_painted", 1.0, 512, 1.0)
def metal_painted():
    peel = spectral(N, 191, 1.0, 40)
    mott = spectral(N, 192, 2.0, 3)
    scratches = smoothstep(0.93, 0.97, spectral(N, 193, 1.0, 30, aniso=(0.05, 1.0)))
    v = 0.86 + 0.04 * mott - 0.05 * scratches
    albedo = np.stack([v, v, v], -1)
    h = 0.0001 * peel - 0.0002 * scratches
    rough = 0.42 + 0.1 * mott + 0.2 * scratches
    finish("metal_painted", albedo, h, rough)


@texture("metal_brushed", 1.0, 512, 1.0)
def metal_brushed():
    br = spectral(N, 201, 0.6, 60, aniso=(0.01, 1.0))
    mott = spectral(N, 202, 2.0, 2)
    v = 0.62 + 0.08 * br + 0.04 * mott
    albedo = np.stack([v, v * 1.0, v * 1.02], -1)
    h = 0.00005 * br
    finish("metal_brushed", albedo, h, 0.32 + 0.1 * br + 0.08 * mott, metal=np.full((N, N), 1.0))


@texture("corrugated", 1.0, 512, 1.0)
def corrugated():
    X, Y = grid(N)
    prof = np.sin(Y / 0.0333 * 2 * np.pi)
    rust = smoothstep(0.7, 0.9, spectral(N, 211, 1.6, 6))
    v = 0.6 + 0.05 * prof
    albedo = lerp(np.stack([v, v, v * 1.03], -1), np.array([0.42, 0.24, 0.13]), rust * 0.7)
    h = 0.006 * prof
    metal = 1.0 - rust
    finish("corrugated", albedo, h, 0.4 + 0.5 * rust, metal=metal)


# ------------------------------------------------------------------ roupas e personagens
@texture("cloth_knit", 0.25, 512, 1.2)
def cloth_knit():
    X, Y = grid(N); X *= 0.25; Y *= 0.25
    kx, ky = 2 * np.pi / 0.0028, 2 * np.pi / 0.0022
    col_v = np.abs(np.sin(X * kx * 0.5))
    loops = col_v * (0.6 + 0.4 * np.abs(np.sin(Y * ky + (np.floor(X * kx / np.pi) % 2) * 1.5)))
    fuzz = spectral(N, 221, 0.4, 150)
    mott = spectral(N, 222, 1.8, 3)
    v = 0.8 + 0.08 * loops + 0.05 * fuzz + 0.04 * mott
    h = 0.00035 * loops + 0.0001 * fuzz
    finish("cloth_knit", np.stack([v, v, v], -1), h, 0.92)


@texture("cloth_denim", 0.25, 512, 1.5)
def cloth_denim():
    X, Y = grid(N); X *= 0.25; Y *= 0.25
    k = 2 * np.pi / 0.0022
    twill = np.sin((X + Y) * k) * 0.5 + 0.5
    weft = smoothstep(0.55, 0.95, spectral(N, 231, 0.6, 120, aniso=(1.0, 0.15)))
    slub = spectral(N, 232, 1.0, 20, aniso=(1.0, 0.1))
    fade = spectral(N, 233, 2.0, 2)
    v = 0.62 + 0.18 * twill * (0.6 + 0.4 * slub) + 0.25 * weft * 0.5 + 0.1 * fade
    albedo = np.stack([v, v * 1.01, v * 1.03], -1)
    h = 0.0004 * twill + 0.0002 * slub
    finish("cloth_denim", albedo, h, 0.9)


@texture("cloth_canvas", 0.25, 512, 1.4)
def cloth_canvas():
    X, Y = grid(N); X *= 0.25; Y *= 0.25
    k = 2 * np.pi / 0.0035
    weave = (np.sin(X * k) * np.sin(Y * k)) * 0.5 + 0.5
    slub = spectral(N, 241, 1.0, 25, aniso=(0.15, 1.0)) * 0.5 + spectral(N, 242, 1.0, 25, aniso=(1.0, 0.15)) * 0.5
    v = 0.76 + 0.1 * weave + 0.08 * slub + 0.05 * spectral(N, 243, 2, 2)
    finish("cloth_canvas", np.stack([v, v * 0.99, v * 0.97], -1), 0.0005 * weave + 0.0003 * slub, 0.88)


@texture("leather", 0.3, 512, 1.4)
def leather():
    f1, f2, ids, _, _ = cell_voronoi(N, 90, 251, 0.95)
    grain = smoothstep(0.0, 0.25, f2 - f1)
    mott = spectral(N, 252, 1.8, 4)
    v = 0.7 + 0.1 * grain + 0.1 * mott
    h = 0.0004 * grain
    finish("leather", np.stack([v, v * 0.97, v * 0.94], -1), h, 0.55 + 0.2 * (1 - grain))


@texture("hair", 0.3, 512, 1.6)
def hair():
    strands = spectral(N, 261, 0.9, 80, aniso=(1.0, 0.03))
    clumps = spectral(N, 262, 1.6, 8, aniso=(1.0, 0.08))
    v = 0.55 + 0.35 * strands * (0.6 + 0.4 * clumps)
    h = 0.0008 * strands + 0.0012 * clumps
    finish("hair", np.stack([v, v, v], -1), h, 0.5 + 0.2 * (1 - strands))


@texture("rubber", 0.3, 256, 1.0)
def rubber():
    v = 0.85 + 0.08 * spectral(N, 271, 0.5, 100)
    finish("rubber", np.stack([v, v, v], -1), 0.0002 * spectral(N, 272, 0.8, 60), 0.85)


def main():
    names = sys.argv[1:] or list(SETS)
    meta_path = os.path.join(OUT, "textures.json")
    meta = {}
    if os.path.exists(meta_path):
        meta = json.load(open(meta_path))
    for name in names:
        fn, tile_m, size, strength = SETS[name]
        print("gerando", name, flush=True)
        fn()
        meta[name] = {"tile_m": tile_m, "size": size}
    os.makedirs(OUT, exist_ok=True)
    json.dump(dict(sorted(meta.items())), open(meta_path, "w"), indent=1)


if __name__ == "__main__":
    main()
