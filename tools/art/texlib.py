"""Biblioteca de síntese de texturas PBR tileáveis (numpy).

Tudo é gerado de forma determinística (sementes fixas) e é perfeitamente repetível
em mosaico: ruídos espectrais usam só frequências inteiras e o Voronoi usa
distância toroidal. Saída: albedo (sRGB), normal (convenção OpenGL, Y+) e ORM
(R = oclusão, G = rugosidade, B = metálico), como o Godot espera.
"""
import numpy as np
from PIL import Image


def rng(seed):
    return np.random.default_rng(seed)


def spectral(n, seed, beta=1.8, fmin=1, fmax=None, aniso=(1.0, 1.0)):
    """Ruído fBm tileável por síntese espectral. Retorna [0, 1]."""
    r = rng(seed)
    white = r.standard_normal((n, n))
    F = np.fft.fft2(white)
    ky = np.fft.fftfreq(n) * n
    kx = np.fft.fftfreq(n) * n
    KX, KY = np.meshgrid(kx / aniso[0], ky / aniso[1])
    k = np.sqrt(KX ** 2 + KY ** 2)
    k[0, 0] = 1.0
    amp = k ** (-beta)
    amp[k < fmin] = 0.0
    if fmax is not None:
        amp *= np.clip((fmax * 1.25 - k) / (fmax * 0.25), 0.0, 1.0)
    out = np.real(np.fft.ifft2(F * amp))
    return norm(out)


def norm(a):
    lo, hi = np.percentile(a, 0.5), np.percentile(a, 99.5)
    return np.clip((a - lo) / max(hi - lo, 1e-9), 0.0, 1.0)


def grid(n):
    v = (np.arange(n) + 0.5) / n
    return np.meshgrid(v, v)  # X (colunas), Y (linhas)


def voronoi(n, pts, rows_per_chunk=64, aspect=(1.0, 1.0)):
    """Voronoi toroidal: retorna F1, F2 (em unidades do tile) e índice do mais próximo."""
    X, Y = grid(n)
    f1 = np.empty((n, n)); f2 = np.empty((n, n)); idx = np.empty((n, n), dtype=np.int32)
    px = pts[:, 0][None, None, :]; py = pts[:, 1][None, None, :]
    for r0 in range(0, n, rows_per_chunk):
        xs = X[r0:r0 + rows_per_chunk, :, None]; ys = Y[r0:r0 + rows_per_chunk, :, None]
        dx = (xs - px + 0.5) % 1.0 - 0.5
        dy = (ys - py + 0.5) % 1.0 - 0.5
        d = np.sqrt((dx * aspect[0]) ** 2 + (dy * aspect[1]) ** 2)
        part = np.partition(d, 1, axis=2)
        f1[r0:r0 + rows_per_chunk] = part[:, :, 0]
        f2[r0:r0 + rows_per_chunk] = part[:, :, 1]
        idx[r0:r0 + rows_per_chunk] = np.argmin(d, axis=2)
    return f1, f2, idx


def jitter_points(cells, seed, jitter=0.8):
    r = rng(seed)
    g = (np.stack(np.meshgrid(np.arange(cells), np.arange(cells)), -1).reshape(-1, 2) + 0.5) / cells
    return (g + (r.random(g.shape) - 0.5) * jitter / cells) % 1.0


def blur(a, radius):
    """Desfoque gaussiano tileável via FFT."""
    n = a.shape[0]
    k = np.fft.fftfreq(n)
    KX, KY = np.meshgrid(k, k)
    g = np.exp(-2 * (np.pi ** 2) * (radius ** 2) * (KX ** 2 + KY ** 2))
    if a.ndim == 3:
        return np.stack([np.real(np.fft.ifft2(np.fft.fft2(a[..., c]) * g)) for c in range(a.shape[2])], -1)
    return np.real(np.fft.ifft2(np.fft.fft2(a) * g))


def normal_from_height(h_m, tile_m, strength=1.0):
    """h_m: altura em metros. Retorna normal RGB [0,1] (OpenGL, Y+ para cima)."""
    n = h_m.shape[0]
    px = tile_m / n
    dx = (np.roll(h_m, -1, 1) - np.roll(h_m, 1, 1)) / (2 * px)
    drow = (np.roll(h_m, -1, 0) - np.roll(h_m, 1, 0)) / (2 * px)
    nx = -dx * strength
    ny = drow * strength
    nz = np.ones_like(nx)
    l = np.sqrt(nx ** 2 + ny ** 2 + nz ** 2)
    return np.stack([nx / l, ny / l, nz / l], -1) * 0.5 + 0.5


def cavity_ao(h_m, tile_m, radius_m=0.02, strength=12.0):
    n = h_m.shape[0]
    r = radius_m / tile_m * n / n  # raio em fração do tile
    b = blur(h_m, radius_m / tile_m)
    occ = np.clip(1.0 - (b - h_m) * strength / max(radius_m, 1e-4), 0.0, 1.0)
    return occ


def lerp(a, b, t):
    t = np.asarray(t)
    if t.ndim == 2 and (np.ndim(a) in (1, 3) or np.ndim(b) in (1, 3)):
        t = t[..., None]
    return a + (b - a) * t


def col(hexstr):
    h = hexstr.lstrip('#')
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)])


def srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def save_set(outdir, name, albedo, normal, ao, rough, metal=None, size=None, jpg_albedo=False):
    import os
    os.makedirs(outdir, exist_ok=True)
    rough = np.broadcast_to(np.asarray(rough, dtype=float), ao.shape)
    metal = np.broadcast_to(np.asarray(0.0 if metal is None else metal, dtype=float), ao.shape)

    def to8(a):
        return (np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)
    imgs = {
        "albedo": Image.fromarray(to8(albedo)),
        "normal": Image.fromarray(to8(normal)),
        "orm": Image.fromarray(to8(np.stack([ao, rough, metal], -1))),
    }
    for k, im in imgs.items():
        if size and im.size[0] != size:
            im = im.resize((size, size), Image.LANCZOS)
        im.save(f"{outdir}/{name}_{k}.png", optimize=True)


def cell_voronoi(n, cells, seed, jitter=0.9, aspect=(1.0, 1.0), cells_y=None):
    """Voronoi em grade com jitter (rápido, tileável). Coordenadas em unidades de célula.
    Retorna F1, F2, id da célula vencedora e o vetor até o ponto (dx, dy)."""
    cy_n = cells_y or cells
    r = rng(seed)
    jit = (r.random((cy_n, cells, 2)) - 0.5) * jitter
    X, Y = grid(n)
    X = X * cells; Y = Y * cy_n
    ix = np.floor(X).astype(int); iy = np.floor(Y).astype(int)
    f1 = np.full((n, n), 9.0); f2 = np.full((n, n), 9.0)
    ids = np.zeros((n, n), dtype=np.int64)
    vx = np.zeros((n, n)); vy = np.zeros((n, n))
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            cx = ix + ox; cyy = iy + oy
            wx = cx % cells; wy = cyy % cy_n
            px = cx + 0.5 + jit[wy, wx, 0]
            py = cyy + 0.5 + jit[wy, wx, 1]
            dx = (px - X) * aspect[0]; dy = (py - Y) * aspect[1]
            d = np.sqrt(dx * dx + dy * dy)
            closer = d < f1
            f2 = np.where(closer, f1, np.minimum(f2, d))
            ids = np.where(closer, wy * cells + wx, ids)
            vx = np.where(closer, dx, vx); vy = np.where(closer, dy, vy)
            f1 = np.where(closer, d, f1)
    return f1, f2, ids, vx, vy


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def per_id(ids, seed, count=None):
    """Valor aleatório estável por id (0..1)."""
    r = rng(seed)
    m = int(ids.max()) + 1 if count is None else count
    table = r.random(m)
    return table[ids]
