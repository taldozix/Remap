# Pipeline de arte

Os ativos em `assets/` são gerados por estes scripts (reprodutíveis, sementes fixas).

## Requisitos

```bash
uv venv blendenv --python 3.13 && uv pip install --python blendenv bpy==5.2.* numpy pillow
```

Para os personagens, é preciso um clone esparso dos dados CC0 do MPFB2:

```bash
git clone --depth 1 --filter=blob:none --no-checkout https://github.com/makehumancommunity/mpfb2.git
cd mpfb2 && git sparse-checkout set --no-cone '/LICENSE*' '/src/mpfb/data/3dobjs/' \
  '/src/mpfb/data/rigs/standard/*game_engine.json' '/src/mpfb/data/targets/macrodetails/' \
  '/src/mpfb/data/mesh_metadata/' && git checkout
```

## Comandos

| O quê | Comando | Saída |
|---|---|---|
| Texturas PBR | `blendenv/bin/python tools/art/make_textures.py [nome ...]` | `assets/textures/*_{albedo,normal,orm}.png`, `textures.json` |
| Carros | `blendenv/bin/python tools/art/blender/make_cars.py [hatch sedan pickup van] [--preview]` | `assets/vehicles/car_*.glb` |
| Personagens | `MPFB_DATA=mpfb2/src/mpfb/data blendenv/bin/python tools/art/blender/make_characters.py m f --skin --export` | `assets/characters/human_{m,f}.glb`, `skin_detail.png`, `skin_mask.png`, `eye.png` |
| Exportação para o Unreal | `blendenv/bin/python tools/art/blender/export_unreal.py` | `unreal/ArtSource/{vehicles,characters}/…/*.glb`, `unreal/Content/Data/art/vehicles_layout.json` (depois rode `unreal/Scripts/build_content.py` no editor; ver `unreal/Scripts/README.md`) |
| Opções de importação | `godot --headless --import` e depois `python3 tools/art/fix_imports.py` e `--import` de novo | `.import` com VRAM e mipmaps |

Pré-visualização dos personagens:

```bash
--preview --anim=walk:0.25 --only=Top_tee,Bottom_pants,Shoes,Hair_short
```

A variável `CAM=x,y,z` muda a câmera.

## Convenções que o jogo espera

**Carros:**
- frente em +Y no Blender (-Z no Godot), origem no chão;
- nós `Wheel_FL/FR/RL/RR` (o filho `*_spin` gira) e `Door_FL` com pivô na dobradiça;
- materiais `paint`, `headlight`, `taillight`, `indicator`, `glass`…

**Personagens:**
- uma malha por peça (`Body`, `Top_*`, `Bottom_*`, `Hair_*`, `Shoes`, `Cap`…);
- shape keys `heavy`, `thin`, `old`, `face_a`, `face_b`;
- animações `idle`, `walk`, `run`, `talk`, `work`, `eat`, `search`, `sit`, `lie`, `knocked`;
- a máscara de pele (canal R) codifica as regiões do corpo que cada roupa esconde (ver
  `Anatomy.region` e `Humanoid.HIDE_BITS`).
