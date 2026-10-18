#!/usr/bin/env python3
"""Gera data/map/vila_aurora.json — o bairro do protótipo.

O JSON é o formato que o jogo lê (WorldBuilder). Este script existe para manter
posições coerentes (casas espelhadas, árvores e postes distribuídos) sem editar
centenas de coordenadas à mão. Rode: python3 tools/build_map.py

Convenções: x = leste, z = sul (Godot). "rot" em graus no eixo Y; rot 0 = frente
virada para o norte (-z), 90 = oeste, 180 = sul, -90 = leste. Lados de parede:
n, s, e, w. Coordenadas de mobília/spots de prédio são locais ao centro do prédio.
"""
import json, os

ROADS = {"xs": [-60, 0, 60], "zs": [-60, 0, 60], "width": 9.0, "sidewalk": 3.0,
         "lane_offset": 1.75, "speed_limit": 11.0, "signals": [[0, 0]]}

def mirror(f):
    """Espelha um móvel de uma casa com porta ao sul para porta ao norte."""
    g = dict(f)
    x, z = f["pos"]
    g["pos"] = [x, -z]
    g["rot"] = (180 - f.get("rot", 0)) % 360
    return g

def rot_w(f):
    """Gira o layout de uma casa (porta ao sul) para porta a oeste, mantendo a casa 9x8:
    troca os eixos e escala para caber (layout aproximado e coerente)."""
    g = dict(f)
    if f["type"] == "partition":
        g["from"] = [-f["from"][1] * 1.0, f["from"][0] * 0.85]
        g["to"] = [-f["to"][1] * 1.0, f["to"][0] * 0.85]
        return g
    x, z = f["pos"]
    g["pos"] = [round(-z * 1.0, 2), round(x * 0.85, 2)]
    g["rot"] = (f.get("rot", 0) - 90) % 360
    return g

def house(bid, name, center, w, door_side, owner, style, extra_furniture=None, containers=None, prefix=None):
    p = prefix or bid
    hw = w / 2.0
    c = containers or {}
    f = [
        {"type": "bed", "pos": [-hw + 1.0, -2.9], "rot": 0, "id": f"cama_{p}", "owner": owner, "spot": f"{p}_cama"},
        {"type": "wardrobe", "pos": [-hw + 2.7, -3.65], "rot": 180,
         "container": dict({"id": f"guarda_roupa_{p}", "name": "Guarda-roupa", "capacity": 60, "owner": owner}, **c.get("wardrobe", {}))},
        {"type": "nightstand", "pos": [-hw + 2.0, -3.6], "rot": 180},
        {"type": "partition", "from": [hw - 3.2, -4.0], "to": [hw - 3.2, -1.7]},
        {"type": "shower", "pos": [hw - 0.8, -3.2], "rot": 0, "id": f"chuveiro_{p}", "owner": owner},
        {"type": "toilet", "pos": [hw - 2.4, -3.5], "rot": 180},
        {"type": "fridge", "pos": [hw - 0.5, 0.2], "rot": 90,
         "container": dict({"id": f"geladeira_{p}", "name": "Geladeira", "capacity": 25, "refrigerated": True, "owner": owner}, **c.get("fridge", {}))},
        {"type": "stove", "pos": [hw - 0.5, 1.3], "rot": 90, "id": f"fogao_{p}", "owner": owner},
        {"type": "sink", "pos": [hw - 0.5, 2.4], "rot": 90, "id": f"pia_{p}"},
        {"type": "table", "pos": [hw - 2.9, 1.9], "rot": 0, "spot": f"{p}_cozinha"},
        {"type": "chair", "pos": [hw - 2.9, 1.1], "rot": 180},
        {"type": "chair", "pos": [hw - 2.9, 2.7], "rot": 0},
        {"type": "sofa", "pos": [-hw + 0.75, 1.8], "rot": -90, "id": f"sofa_{p}", "spot": f"{p}_sofa"},
        {"type": "tv", "pos": [-hw + 3.3, 1.8], "rot": 90},
        {"type": "rug", "pos": [-hw + 2.1, 1.8], "rot": 0},
        {"type": "plant", "pos": [hw - 3.6, 3.5], "rot": 0},
    ]
    f += extra_furniture or []
    if door_side == "w":
        f = [rot_w(x) for x in f]
    if door_side == "n":
        f = [mirror(x) if x["type"] != "partition" else
             dict(x, **{"from": [x["from"][0], -x["from"][1]], "to": [x["to"][0], -x["to"][1]]}) for x in f]
    door_offset = -2.0 if w >= 10 else 0.0
    windows = [{"side": door_side, "offset": 2.4 if w >= 10 else 2.6, "width": 1.6},
               {"side": {"s": "n", "n": "s", "w": "e"}[door_side], "offset": -2.0, "width": 1.4},
               {"side": "n" if door_side == "w" else "e", "offset": 0.0, "width": 1.2}, {"side": "s" if door_side == "w" else "w", "offset": 0.5, "width": 1.2}]
    return {
        "id": bid, "name": name, "type": "house", "center": center, "size": [w, 8.0], "height": 2.9,
        "style": style, "owner": owner, "private": True,
        "doors": [{"id": f"porta_{bid}", "side": door_side, "offset": door_offset, "width": 1.1, "locked": True,
                   "keys": [owner], "label": "Porta"}],
        "windows": windows, "furniture": f,
    }

buildings = []

# ---------------------------------------------------------------- NW: residencial
buildings.append(house("casa_jogador", "Casa nº 8", [-42, -17], 10, "s", "owner",
    {"wall": "#e9dcc3", "roof": "#8a3d30", "trim": "#f6f1e7", "floor": "#a7835a", "accent": "#3f6e8c"},
    extra_furniture=[{"type": "note", "pos": [2.1, 1.9], "rot": 0, "id": "bilhete_boas_vindas", "note": "boas_vindas"}],
    containers={"wardrobe": {"contents": {"mochila": 1, "celular_velho": 1, "casaco": 1, "capa_chuva": 1, "camiseta_branca": 1}},
                "fridge": {"contents": {"pao": 1, "agua": 2, "queijo": 1, "banana": 3, "miojo": 2}}}, prefix="jogador"))
buildings.append(house("casa_lucia", "Casa da Dona Lúcia (nº 10)", [-20, -17], 9, "s", "lucia",
    {"wall": "#d8e3c8", "roof": "#6b4a3a", "trim": "#ffffff", "floor": "#9c7b56", "accent": "#7a5ea8"},
    containers={"fridge": {"private": True, "contents": {"queijo": 1, "banana": 4, "agua": 2}}}, prefix="lucia"))
buildings.append(house("casa_marcos", "Casa do Marcos (nº 12)", [-40, -42], 9, "n", "marcos",
    {"wall": "#cfd6dc", "roof": "#3e4a57", "trim": "#e7ecef", "floor": "#7d6a58", "accent": "#a33b3b"},
    extra_furniture=[{"type": "chest", "pos": [-0.8, 3.4], "rot": 180,
                      "container": {"id": "bau_marcos", "name": "Baú do Marcos", "capacity": 40, "owner": "marcos",
                                    "contents": {"encomenda_lucia": 1, "relogio_antigo": 1, "lata_aluminio": 3}}}],
    containers={"fridge": {"contents": {"refrigerante": 3, "pao": 1}}}, prefix="marcos"))
buildings.append(house("casa_silva", "Casa dos Silva (nº 14)", [-20, -42], 9, "n", "paula",
    {"wall": "#f0d2b8", "roof": "#7a2f2f", "trim": "#ffffff", "floor": "#a08060", "accent": "#2f7a5f"},
    containers={"fridge": {"contents": {"agua": 3, "sanduiche": 1}}}, prefix="silva"))

buildings.append(house("casa_5", "Casa nº 5 (à venda)", [-46, -29], 9, "w", "owner",
    {"wall": "#e6d3b3", "roof": "#5f6b4a", "trim": "#f3eee2", "floor": "#9a7a55", "accent": "#5f8a6a"}, prefix="casa5"))

# ---------------------------------------------------------------- NE: comércio
buildings.append({
    "id": "mercadinho", "name": "Mercadinho Bom Preço", "type": "shop", "center": [22, -18], "size": [14, 10], "height": 3.4,
    "style": {"wall": "#e6e0d2", "roof": "#5a6470", "trim": "#2d7d46", "floor": "#a9a397", "accent": "#2d7d46", "sign": "MERCADINHO BOM PREÇO"},
    "owner": "antonio", "private": False, "shop": "mercadinho",
    "doors": [{"id": "porta_mercadinho", "side": "s", "offset": -3.0, "width": 1.6, "locked": False,
               "hours": [7, 21], "keys": ["antonio"], "label": "Porta do mercadinho", "glass": True}],
    "windows": [{"side": "s", "offset": 2.5, "width": 5.0, "low": True}, {"side": "e", "offset": 0, "width": 2.0}],
    "furniture": [
        {"type": "shelf", "pos": [-5.2, -1.2], "rot": 90, "len": 4.0},
        {"type": "shelf", "pos": [-2.6, -1.2], "rot": 90, "len": 4.0},
        {"type": "shelf", "pos": [0.0, -1.2], "rot": 90, "len": 4.0},
        {"type": "fridge_display", "pos": [3.5, -4.55], "rot": 180, "len": 5.0},
        {"type": "shelf", "pos": [6.55, -1.0], "rot": 90, "len": 2.4, "color": "#e8f4f8", "label": "Farmácia"},
        {"type": "counter", "id": "balcao_mercadinho", "pos": [4.0, 2.0], "rot": 0, "len": 2.6, "shop": "mercadinho", "staff_spot": "mercadinho_balcao", "register": True},
        {"type": "crate", "pos": [-6.2, -4.2], "rot": 0},
    ],
    "spots": {
        "mercadinho_balcao": {"pos": [4.0, 1.1], "rot": 180, "pose": "stand"},
        "mercadinho_cliente": {"pos": [-3.9, 0.6], "rot": 90, "pose": "stand"},
        "mercadinho_cliente_2": {"pos": [-1.3, -2.5], "rot": -90, "pose": "stand"},
        "mercadinho_fila": {"pos": [4.0, 3.2], "rot": 0, "pose": "stand"},
        "mercadinho_quartinho": {"pos": [-6.0, -3.0], "rot": 0, "pose": "stand", "hidden": True},
    },
})
buildings.append({
    "id": "penhores", "name": "Penhores do Beto", "type": "shop", "center": [42, -18], "size": [9, 8], "height": 3.2,
    "style": {"wall": "#d9c48f", "roof": "#4b3b2a", "trim": "#7a5a2a", "floor": "#8c7558", "accent": "#b5832a", "sign": "PENHORES DO BETO"},
    "owner": "beto", "private": False, "shop": "penhores",
    "doors": [{"id": "porta_penhores", "side": "s", "offset": 0.0, "width": 1.2, "locked": False, "hours": [9, 19],
               "keys": ["beto"], "label": "Porta da loja de penhores"}],
    "windows": [{"side": "s", "offset": 2.6, "width": 1.6, "low": True}, {"side": "s", "offset": -2.6, "width": 1.6, "low": True}],
    "furniture": [
        {"type": "counter", "id": "balcao_penhores", "pos": [0.0, -0.4], "rot": 0, "len": 3.0, "shop": "penhores", "staff_spot": "penhores_balcao", "register": True},
        {"type": "shelf", "pos": [-3.9, -1.0], "rot": 90, "len": 3.0, "color": "#6b5235"},
        {"type": "shelf", "pos": [3.9, -1.0], "rot": 90, "len": 3.0, "color": "#6b5235"},
        {"type": "shelf", "pos": [0.0, -3.6], "rot": 180, "len": 4.0, "color": "#6b5235"},
    ],
    "spots": {
        "penhores_balcao": {"pos": [0.0, -1.3], "rot": 180, "pose": "stand"},
        "penhores_cliente": {"pos": [0.6, 0.7], "rot": 0, "pose": "stand"},
    },
})

buildings.append({
    "id": "garagem_3", "name": "Garagem nº 3 (à venda)", "type": "garage", "center": [45, -30], "size": [7, 6.5], "height": 3.2,
    "style": {"wall": "#c8c2b4", "roof": "#4b4f57", "trim": "#e4e0d6", "floor": "#7a7d82", "accent": "#7a8a99"},
    "owner": "owner", "private": True,
    "doors": [{"id": "portao_garagem_3", "side": "e", "offset": 0.0, "width": 3.6, "locked": True, "keys": ["owner"], "label": "Portão da garagem", "rollup": True, "manual": True}],
    "windows": [{"side": "n", "offset": 0.0, "width": 1.2}],
    "furniture": [
        {"type": "workbench", "id": "bancada_garagem_3", "pos": [-2.8, -1.2], "rot": 90, "len": 2.6},
        {"type": "crate", "pos": [-2.9, 2.4], "rot": 0, "container": {"id": "prateleira_garagem_3", "name": "Prateleira da garagem", "capacity": 80, "owner": "owner"}},
    ],
    "spots": {},
})

# ---------------------------------------------------------------- SE: drive-thru
buildings.append({
    "id": "brasa", "name": "Brasa Burger Drive-Thru", "type": "restaurant", "center": [26, 26], "size": [14, 10], "height": 3.6,
    "style": {"wall": "#e8d2b0", "roof": "#b8432f", "trim": "#e8a33a", "floor": "#b0805f", "accent": "#b8432f", "sign": "BRASA BURGER"},
    "owner": "sandra", "private": False, "job": "brasa",
    "doors": [{"id": "porta_brasa", "side": "n", "offset": -4.0, "width": 1.6, "locked": False, "hours": [10, 22],
               "keys": ["sandra", "kleber", "player_job"], "label": "Porta do Brasa Burger", "glass": True}],
    "windows": [{"side": "n", "offset": 0.5, "width": 4.0, "low": True}, {"side": "w", "offset": 0.0, "width": 4.0, "low": True},
                {"side": "e", "offset": 1.0, "width": 1.3, "drive": True}],
    "staff_zone": [-0.6, -5.0, 7.0, 5.0],
    "furniture": [
        {"type": "counter", "id": "balcao_brasa", "pos": [-1.0, -1.4], "rot": 90, "len": 5.2, "shop": "brasa_balcao", "staff_spot": "brasa_balcao_func", "register": True},
        {"type": "table", "pos": [-5.4, 1.4], "rot": 0, "spot": "brasa_mesa_1"},
        {"type": "chair", "pos": [-5.4, 0.6], "rot": 180},
        {"type": "chair", "pos": [-5.4, 2.2], "rot": 0},
        {"type": "table", "pos": [-5.4, 3.8], "rot": 0, "spot": "brasa_mesa_2"},
        {"type": "chair", "pos": [-5.4, 4.5], "rot": 0},
        {"type": "grill", "pos": [2.2, -4.45], "rot": 180, "id": "estacao_chapa", "station": "chapa"},
        {"type": "fryer", "pos": [4.3, -4.45], "rot": 180, "id": "estacao_fritadeira", "station": "fritadeira"},
        {"type": "soda_machine", "pos": [6.5, -2.4], "rot": 90, "id": "estacao_refri", "station": "refri"},
        {"type": "drive_window", "pos": [6.55, 1.0], "rot": 90, "id": "janela_drive"},
        {"type": "time_clock", "pos": [0.6, 4.75], "rot": 0, "id": "relogio_ponto"},
        {"type": "desk", "pos": [3.6, 3.9], "rot": 0},
        {"type": "kitchen_counter", "pos": [2.0, -0.9], "rot": 0, "len": 3.0},
        {"type": "crate", "pos": [6.3, 4.3], "rot": 0,
         "container": {"id": "estoque_brasa", "name": "Estoque do Brasa", "capacity": 50, "owner": "sandra", "contents": {"refrigerante": 6, "agua": 4}}},
    ],
    "spots": {
        "brasa_balcao_func": {"pos": [-0.25, -1.4], "rot": 90, "pose": "stand"},
        "brasa_escritorio": {"pos": [3.6, 3.1], "rot": 180, "pose": "stand"},
        "brasa_cliente": {"pos": [-2.0, -1.4], "rot": -90, "pose": "stand"},
    },
})

# ---------------------------------------------------------------- SW: oficina, prédio
buildings.append({
    "id": "oficina", "name": "Oficina do Rafael", "type": "garage", "center": [-42, 44], "size": [12, 10], "height": 4.2,
    "style": {"wall": "#b7bcc2", "roof": "#3b4048", "trim": "#e0b521", "floor": "#7d8288", "accent": "#e0b521", "sign": "OFICINA DO RAFAEL"},
    "owner": "rafael", "private": False,
    "doors": [{"id": "portao_oficina", "side": "s", "offset": 0.0, "width": 4.2, "locked": False, "hours": [7.5, 18],
               "keys": ["rafael", "marcos"], "label": "Portão da oficina", "rollup": True}],
    "windows": [{"side": "e", "offset": 0.0, "width": 2.0}],
    "furniture": [
        {"type": "lift", "pos": [-2.2, -0.5], "rot": 0},
        {"type": "counter", "id": "balcao_oficina", "pos": [3.0, 2.6], "rot": 0, "len": 2.0, "shop": "oficina", "staff_spot": "oficina_rafael", "register": True},
        {"type": "workbench", "pos": [3.0, -4.4], "rot": 180, "len": 3.0},
        {"type": "chest", "pos": [5.4, -1.0], "rot": 90, "color": "#b33a2a",
         "container": {"id": "caixa_ferramentas_rafael", "name": "Caixa de ferramentas", "capacity": 30, "owner": "rafael", "contents": {"radio_velho": 1, "lata_aluminio": 2}}},
        {"type": "tire_stack", "pos": [-5.2, -4.0], "rot": 0},
        {"type": "tire_stack", "pos": [-5.2, -3.0], "rot": 0},
    ],
    "spots": {
        "oficina_rafael": {"pos": [2.5, 1.5], "rot": 180, "pose": "stand"},
        "oficina_marcos": {"pos": [-2.2, 1.6], "rot": 0, "pose": "work"},
        "oficina_fundos": {"pos": [4.5, -3.5], "rot": 0, "pose": "stand", "hidden": True},
    },
})
buildings.append({
    "id": "predio", "name": "Edifício Aurora (residencial)", "type": "apartment", "center": [-16, 44], "size": [12, 10], "height": 6.4,
    "style": {"wall": "#c9b3a0", "roof": "#594a42", "trim": "#efe6dc", "floor": "#8c7a6a", "accent": "#54708c"},
    "owner": "", "private": True, "floors": 2, "closed_interior": True,
    "doors": [{"id": "porta_predio", "side": "s", "offset": 0.0, "width": 1.4, "locked": True,
               "keys": ["sandra", "kleber", "beto", "julia", "pedestre"], "label": "Portaria (somente moradores)", "glass": True}],
    "windows": [{"side": "s", "offset": -3.6, "width": 1.6}, {"side": "s", "offset": 3.6, "width": 1.6},
                {"side": "e", "offset": -2, "width": 1.4}, {"side": "e", "offset": 2, "width": 1.4},
                {"side": "w", "offset": -2, "width": 1.4}, {"side": "w", "offset": 2, "width": 1.4}],
    "furniture": [],
    "spots": {"predio_interior": {"pos": [0.0, 0.0], "rot": 0, "pose": "stand", "hidden": True}},
})

# ---------------------------------------------------------------- props e áreas
props = []
spots = {
    "calcada_lucia": {"pos": [-20.0, -10.6], "rot": 180, "pose": "stand"},
    "jardim_marcos": {"pos": [-37.5, -49.0], "rot": 0, "pose": "stand"},
    "ponto_onibus": {"pos": [-30.0, 6.2], "rot": 0, "pose": "stand"},
    "brasa_entrada": {"pos": [19.5, 18.5], "rot": 0, "pose": "stand"},
    "praca_conversa_1": {"pos": [-19.5, 28.0], "rot": -60, "pose": "stand"},
    "praca_conversa_2": {"pos": [-18.2, 28.8], "rot": 120, "pose": "stand"},
    "posto_frentista": {"pos": [30.0, -38.5], "rot": 180, "pose": "stand"},
    "porta_silva": {"pos": [-20.0, -47.6], "rot": 180, "pose": "stand"},
    "portaria_predio": {"pos": [-16.0, 50.6], "rot": 0, "pose": "stand"},
    "oficina_entrega": {"pos": [-36.0, 51.0], "rot": 0, "pose": "stand"},
    "lixeira_mercadinho_spot": {"pos": [12.8, -10.8], "rot": -90, "pose": "work"},
}
# Praça
props.append({"type": "fountain", "pos": [-31, 22]})
benches = [("praca_banco_1", -37.5, 22, -90), ("praca_banco_2", -24.5, 22, 90), ("praca_banco_3", -31, 15.5, 180),
           ("praca_banco_4", -31, 28.5, 0), ("praca_banco_5", -46, 31, 0), ("praca_banco_6", -16, 14, 180)]
for sid, x, z, r in benches:
    props.append({"type": "bench", "pos": [x, z], "rot": r, "spot": sid, "id": sid})
props.append({"type": "trash", "pos": [-26.5, 15.0], "rot": 0,
              "container": {"id": "lixeira_praca", "name": "Lixeira da praça", "capacity": 10, "owner": "", "private": False,
                            "contents": {"lata_aluminio": 2}, "refill": {"lata_aluminio": 4}}})
props.append({"type": "trash", "pos": [12.8, -12.0], "rot": 0,
              "container": {"id": "lixeira_mercadinho", "name": "Lixeira do mercadinho", "capacity": 10, "owner": "", "private": False,
                            "contents": {"lata_aluminio": 3, "banana": 1}, "refill": {"lata_aluminio": 5}}})
props.append({"type": "trash", "pos": [17.0, 18.0], "rot": 0,
              "container": {"id": "lixeira_brasa", "name": "Lixeira do Brasa", "capacity": 10, "owner": "", "private": False,
                            "contents": {"lata_aluminio": 2}, "refill": {"lata_aluminio": 3}}})
props.append({"type": "bus_stop", "pos": [-30, 6.6], "rot": 0})
props.append({"type": "doormat", "pos": [-40, -46.9], "rot": 0, "id": "tapete_marcos", "hidden_item": "chave_marcos"})
props.append({"type": "pump", "pos": [27, -42], "rot": 0, "id": "bomba_1"})
props.append({"type": "pump", "pos": [33, -42], "rot": 0, "id": "bomba_2"})
props.append({"type": "canopy", "pos": [30, -42], "size": [12, 8]})
props.append({"type": "kiosk", "pos": [-44, 13.5], "rot": 180})
props.append({"type": "mailbox", "pos": [-45.5, -8.3], "id": "caixa_correio_8", "property": "casa_jogador"})
props.append({"type": "sale_sign", "pos": [-52.0, -25.5], "rot": -90, "id": "placa_casa_5", "property": "casa_5"})
props.append({"type": "sale_sign", "pos": [51.6, -34.3], "rot": 90, "id": "placa_garagem_3", "property": "garagem_3"})
props.append({"type": "sale_sign", "pos": [-24.0, 52.0], "rot": 180, "id": "placa_carro_usado", "vehicle": "carro_usado"})
props.append({"type": "mailbox", "pos": [-22.5, -8.3]})
props.append({"type": "sign", "pos": [44.5, 8.6], "rot": 180, "text": "DRIVE-THRU ➜"})
props.append({"type": "sign", "pos": [-12.5, 9.0], "rot": 180, "text": "PRAÇA DA AURORA"})

# Cercas vivas nos quintais
for a, b in [((-47.5, -12.6), (-47.5, -7.8)), ((-36.4, -12.6), (-36.4, -9.5)), ((-29.6, -12.6), (-29.6, -7.8)),
             ((-15.0, -12.6), (-15.0, -7.8)), ((-25.2, -12.6), (-25.2, -7.8)),
             ((-47.5, -52.2), (-47.5, -46.6)), ((-30, -52.2), (-30, -46.6)), ((-15, -52.2), (-15, -46.6))]:
    props.append({"type": "hedge", "from": list(a), "to": list(b)})

# Árvores: calçadas e periferia
trees = set()
for c in ROADS["xs"]:
    for z in range(-50, 51, 14):
        for s in (-1, 1):
            x = c + s * 6.6
            if abs(z) % 60 > 9:
                trees.add((round(x, 1), z + 3))
for c in ROADS["zs"]:
    for x in range(-50, 51, 14):
        for s in (-1, 1):
            z = c + s * 6.6
            if abs(x) % 60 > 9:
                trees.add((x + 5, round(z, 1)))
# remove árvores na frente de entradas (portas, garagem, drive-thru)
blocked = [(-44, -7.5), (-32.5, -7.5), (-20, -7.5), (19, -7.5), (42, -7.5), (-40, -52.5), (-20, -52.5),
           (-42, 52.5), (-16, 52.5), (45, 7.5), (13, 7.5), (30, -52.5), (-11.5, -52.5), (11, -7.5), (-33, 52.5),
           (53, -30), (-53, -29), (-27, 52.5)]
def ok_tree(x, z):
    return all(abs(x - bx) > 4.5 or abs(z - bz) > 4.0 for bx, bz in blocked)
for x, z in sorted(trees):
    if ok_tree(x, z):
        props.append({"type": "tree", "pos": [x, z], "variant": int((x * 7 + z * 13)) % 3})
for i in range(36):
    import math
    a = i / 36 * math.tau
    r = 76 + (i % 3) * 3
    props.append({"type": "tree", "pos": [round(math.cos(a) * r, 1), round(math.sin(a) * r, 1)], "variant": i % 3})
for x, z in [(-46, 14), (-14, 31), (-46, 24), (-16, 22), (-38, 31.5), (-24, 13), (-42, -27), (-17, -28), (-33, -33), (38, 45), (24, 46), (48, -33)]:
    props.append({"type": "tree", "pos": [x, z], "variant": (x + z) % 3})

# Postes de luz
for c in ROADS["xs"]:
    for z in range(-45, 46, 30):
        props.append({"type": "streetlight", "pos": [c + 6.9, z], "rot": -90})
for c in ROADS["zs"]:
    for x in range(-45, 46, 30):
        props.append({"type": "streetlight", "pos": [x, c - 6.9], "rot": 180})

areas = [
    {"type": "park", "rect": [-51.5, 9.5, -9.0, 34.5]},
    {"type": "plaza", "rect": [-37.0, 16.5, -25.0, 27.5]},
    {"type": "path", "rect": [-51.5, 20.8, -9.0, 23.2]},
    {"type": "path", "rect": [-32.2, 9.5, -29.8, 34.5]},
    {"type": "asphalt", "rect": [-35.0, -21.0, -30.0, -7.5]},
    {"type": "asphalt", "rect": [-14.0, -52.5, -9.0, -38.0]},
    {"type": "asphalt", "rect": [20.0, -52.5, 40.0, -36.0]},
    {"type": "asphalt", "rect": [10.0, 33.5, 33.0, 52.5]},
    {"type": "asphalt", "rect": [-48.0, 49.0, -36.0, 52.5]},
    {"type": "asphalt", "rect": [-36.0, 39.0, -30.0, 52.5]},
    {"type": "asphalt", "rect": [8.0, -24.0, 14.5, -7.5]},
    {"type": "asphalt", "rect": [48.5, -32.0, 55.5, -28.0]},
    {"type": "asphalt", "rect": [-30.5, 49.0, -23.5, 52.5]},
    {"type": "drive_lane", "points": [[42, 1.75], [45.5, 6], [46, 12], [46, 33], [44, 37.5], [39.5, 37.5], [37, 34], [37, 27], [37, 18], [35, 15], [16, 15], [13, 12], [12.5, 7], [14.5, 2.5]]},
]

vehicles = [
    {"id": "carro_jogador", "type": "brisa", "pos": [-32.5, -15.5], "rot": 180, "owner": "player", "fuel": 14, "color": "#2f6db5", "locked": False, "trunk": {"agua": 1}},
    {"id": "van_rafael", "type": "vanza", "pos": [-33.0, 46.0], "rot": 0, "owner": "rafael", "color": "#e8e4d8"},
    {"id": "carro_sandra", "type": "sedan", "pos": [14.5, 43.0], "rot": 90, "owner": "sandra", "color": "#7d2b3a"},
    {"id": "carro_paula", "type": "sedan", "pos": [-11.5, -43.0], "rot": 0, "owner": "paula", "color": "#3d7a50"},
    {"id": "carro_usado", "type": "sedan", "pos": [-27.0, 50.8], "rot": 90, "owner": "rafael", "color": "#6d8f6a", "fuel": 8, "damage": 35,
     "for_sale": {"price": 700, "seller": "rafael"}},
    {"id": "picape_antonio", "type": "picape", "pos": [11.2, -19.0], "rot": 180, "owner": "antonio", "color": "#c7b48a"},
]

drivethru = {
    "path": [[33.5, 1.75], [42, 1.75], [45.5, 6], [46, 12], [46, 33], [44, 37.5], [39.5, 37.5], [37, 34], [37, 30], [37, 27],
             [37, 18], [35, 15], [16, 15], [13, 12], [12.5, 7], [14.5, 2.5], [18, 1.75]],
    "window_index": 9, "join": {"from": [0, 0], "to": [60, 0]}, "divert_after_x": 20.0,
}

notes = {
    "boas_vindas": {
        "title": "Bilhete na mesa",
        "body": "Bem-vindo(a) à Vila Aurora!\n\nA casa é sua enquanto o aluguel estiver em dia: R$ 120 por semana, cobrado todo domingo. "
                "A chave do Brisa está no contato — o tanque está quase na reserva, o posto fica na rua de cima.\n\n"
                "O Brasa Burger, aqui embaixo, está contratando. A Sandra (gerente) fica lá das 8h30 às 22h.\n"
                "O mercadinho do seu Antônio abre às 7h e fecha para o almoço ao meio-dia.\n\n"
                "Ah — a Dona Lúcia, da casa 10, anda preocupada com alguma coisa. Ela é um amor, dá um oi pra ela.\n\n— Seu Geraldo (senhorio)"
    }
}

data = {
    "name": "Vila Aurora", "city": "Porto Serra", "bounds": [-85, -85, 85, 85], "roads": ROADS,
    "buildings": buildings, "props": props, "spots": spots, "areas": areas, "vehicles": vehicles,
    "drivethru": drivethru, "notes": notes,
    "jog_route": [[-48.5, 11.5], [-12, 11.5], [-12, 33], [-48.5, 33]],
    "wander_spots": ["praca_banco_5", "ponto_onibus", "mercadinho_cliente_2", "praca_conversa_1", "brasa_entrada", "penhores_cliente"],
    "start": {"player_pos": [-44.0, -18.6], "player_rot": 180, "money": 60, "hour": 7, "minute": 30, "player_items": {"agua": 1, "camiseta_azul": 1}, "player_equipped": ["camiseta_azul"]},
}

out = os.path.join(os.path.dirname(__file__), "..", "data", "map", "vila_aurora.json")
with open(out, "w", encoding="utf-8") as fh:
    json.dump(data, fh, ensure_ascii=False, indent=1)
print("ok:", os.path.normpath(out), len(props), "props")
