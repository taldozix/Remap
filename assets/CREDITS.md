# Créditos e licenças dos ativos

Todos os ativos desta pasta são originais do projeto (gerados pelos scripts em
`tools/art/`) **ou** derivados de dados com licença que permite uso comercial.
Nenhum ativo de Project Zomboid, GTA ou Red Dead Redemption foi usado.

| Ativo | Origem | Licença |
|---|---|---|
| `characters/human_m.glb`, `characters/human_f.glb` (malha do corpo, morphs de peso/idade/rosto, esqueleto "game_engine" e pesos de pele) | Dados do **MakeHuman / MPFB2** (makehumancommunity/mpfb2, `src/mpfb/data`: `3dobjs/base.obj`, `targets/macrodetails`, `rigs/standard/*game_engine*`) | **CC0 1.0** (ver `LICENSE.ASSETS.md` do MPFB2: "assets ... released under CC0"). O código do MakeHuman/MPFB (AGPL/GPL) **não** foi usado nem incluído. |
| Roupas, cabelos, barba, acessórios, olhos (texturas `characters/*.png`) e animações dos personagens | Gerados por `tools/art/blender/make_characters.py` a partir das malhas de ajuda CC0 acima | Do projeto (MIT) |
| `vehicles/car_*.glb` (hatch, sedã, picape, van) | Modelados proceduralmente por `tools/art/blender/make_cars.py` (Blender) | Do projeto (MIT) — desenhos originais, sem marcas reais |
| `textures/*` (tijolo, reboco, concreto, asfalto, pedra portuguesa, telhas, pisos, madeira, tecidos, couro, metal…) | Sintetizadas por `tools/art/make_textures.py` (numpy) | Do projeto (MIT) |

Ferramentas usadas (não distribuídas): Blender 5.2 como módulo Python (`bpy`, GPL — só
gera os arquivos; a saída não herda a licença), numpy e Pillow.
