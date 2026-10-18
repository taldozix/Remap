"""Leitura dos dados CC0 do MakeHuman/MPFB2: malha base, alvos (morphs), rig e pesos.

Os dados ficam fora do repositório (clone esparso de makehumancommunity/mpfb2, ver
tools/art/README.md); a variável MPFB_DATA aponta para src/mpfb/data.
Coordenadas convertidas para o Blender (Z para cima, metros) e giradas 180° em Z,
para o personagem olhar para +Y (vira -Z no Godot, a frente usada pelo jogo).
"""
import gzip
import json
import os

import numpy as np

DATA = os.environ.get("MPFB_DATA", "")


def _p(*a):
    return os.path.join(DATA, *a)


def obj_to_blender(v):
    """OBJ do MakeHuman (Y para cima, dm, frente +Z) -> Blender girado (frente +Y)."""
    v = np.asarray(v, dtype=float)
    out = np.empty_like(v)
    out[..., 0] = -v[..., 0] * 0.1
    out[..., 1] = v[..., 2] * 0.1
    out[..., 2] = v[..., 1] * 0.1
    return out


def load_obj():
    V, VT, F, FT, G = [], [], [], [], []
    cur = None
    with open(_p("3dobjs", "base.obj")) as fh:
        for line in fh:
            if line.startswith("v "):
                V.append([float(x) for x in line.split()[1:4]])
            elif line.startswith("vt "):
                VT.append([float(x) for x in line.split()[1:3]])
            elif line.startswith("g "):
                cur = line.split()[1].strip()
            elif line.startswith("f "):
                ps = line.split()[1:]
                F.append([int(p.split("/")[0]) - 1 for p in ps])
                FT.append([int(p.split("/")[1]) - 1 if "/" in p and p.split("/")[1] else -1 for p in ps])
                G.append(cur)
    return np.array(V), np.array(VT), F, FT, G


def load_target(rel):
    """Retorna (índices, deltas no espaço OBJ)."""
    idx, d = [], []
    with gzip.open(_p("targets", rel + ".target.gz"), "rt") as fh:
        for line in fh:
            if not line.strip() or line.startswith("#"):
                continue
            p = line.split()
            idx.append(int(p[0]))
            d.append([float(p[1]), float(p[2]), float(p[3])])
    return np.array(idx, dtype=int), np.array(d)


def target_delta(rel, n):
    out = np.zeros((n, 3))
    i, d = load_target(rel)
    if len(i):
        out[i] = d
    return out


def vertex_groups():
    g = json.load(open(_p("mesh_metadata", "basemesh_vertex_groups.json")))
    out = {}
    for k, ranges in g.items():
        ids = []
        for a, b in ranges:
            ids.extend(range(a, b + 1))
        out[k] = np.array(ids, dtype=int)
    return out


def rig(name="game_engine"):
    return json.load(open(_p("rigs", "standard", f"rig.{name}.json")))


def weights(name="game_engine"):
    return json.load(open(_p("rigs", "standard", f"weights.{name}.json")))["weights"]
