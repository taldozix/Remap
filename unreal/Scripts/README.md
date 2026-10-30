# Scripts de conteúdo

O jogo roda sem nenhum `.uasset`. Nesse caso, o `UVilaAssets` usa cores sólidas e formas
básicas. Os materiais, as texturas, os carros, os personagens e o mapa `/Game/Vila/Maps/Vila`
saem do `build_content.py`, que roda dentro do Unreal Editor 5.5.

## Pipeline

1. **Blender**: `tools/art/blender/export_unreal.py`. Já rodou, e a saída está no
   repositório. Só é preciso rodar de novo se os `.glb` de `assets/` mudarem.
2. **Unreal Editor**: `Scripts/build_content.py` importa tudo para `/Game/Vila`.

### 1. Exportar do Blender (opcional)

```bash
uv venv blendenv --python 3.13 && uv pip install --python blendenv "bpy==5.2.*" numpy pillow
blendenv/bin/python tools/art/blender/export_unreal.py            # tudo
blendenv/bin/python tools/art/blender/export_unreal.py sedan m    # só um carro e um sexo
```

O script gera:

| Saída | Conteúdo |
|---|---|
| `unreal/ArtSource/vehicles/<modelo>/<Parte>.glb` | uma malha por parte, com o pivô na origem |
| `unreal/ArtSource/characters/<m\|f>/Body.glb` | esqueleto, corpo, 10 animações e 5 shape keys |
| `unreal/ArtSource/characters/<m\|f>/<Peça>.glb` | roupas e cabelos com o mesmo esqueleto, sem animação |
| `unreal/ArtSource/textures/leaf.png` | cartão de folhas do `M_Leaf` |
| `unreal/Content/Data/art/vehicles_layout.json` | pivôs e medidas dos carros |

Os eixos já saem no padrão do Unreal:

- **Carros:** frente em +X, direita em +Y, Z para cima e origem no chão. A porta
  `Door_FL` tem o pivô na dobradiça e as rodas têm o pivô no centro do cubo.
- **Personagens:** frente em +Y, como o manequim da Epic, e raiz nos pés. O
  `UVilaHumanoid` gira o componente com yaw -90 para a frente ficar em +X.

A conta da conversão está no cabeçalho do `export_unreal.py`.

O `vehicles_layout.json` é lido por `UVilaAssets::VehicleLayout()`. Ele está em cm, no espaço
do carro:

```json
{"sedan": {"parts": {"Wheel_FL": {"location": [138, -78, 31], "rotation": [0, 0, 0]}, ...},
           "slots": {"Body": ["paint", "glass", ...]}, "wheel_radius": 31.75,
           "length": 446.3, "width": 209.0, "height": 148.15,
           "bounds_min": [...], "bounds_max": [...],
           "nominal_length": 440.0, "nominal_wheel_radius": 31.0}}
```

- `rotation` está no formato `[pitch, yaw, roll]`.
- `length`, `width` e `height` são medidos na malha. A largura conta os retrovisores.
- `nominal_*` são as medidas do `CarVisual.gd`. O Godot escala o modelo por
  `tamanho / MODEL_LENGTH`.

### 2. Gerar o conteúdo no Unreal

**Pelo Editor:** abra `VilaAurora.uproject` e use **Tools › Execute Python Script…** com
`Scripts/build_content.py`. Também dá para rodar no console Python (**Output Log › Python**):

```
py "<repo>/unreal/Scripts/build_content.py" --force
```

**Pela linha de comando:**

```bash
UnrealEditor-Cmd <repo>/unreal/VilaAurora.uproject -run=pythonscript \
  -script="<repo>/unreal/Scripts/build_content.py"
```

No Windows, use `UnrealEditor-Cmd.exe`. Se o comando não repassar os argumentos ao script,
defina `VILA_BUILD_ARGS="--force --only=materials"` no ambiente.

Opções:

- `--force` refaz o que já existe. Os materiais e as instâncias são limpos e reescritos no
  mesmo asset, para não quebrar referências. As malhas e os personagens são reimportados.
- `--only=etapa,...` escolhe as etapas. São elas: `textures`, `mpc`, `materials`, `surfaces`,
  `vehicles`, `characters` e `level`. A ordem padrão é essa, e uma etapa depende das
  anteriores.
- `--import-task` importa os `.glb` só pelo `AssetImportTask`. Sem essa opção, o script usa
  o `InterchangeManager` com o pipeline do script e só cai no `AssetImportTask` se essa API
  falhar. Use a opção se o caminho principal se comportar mal na sua versão do motor.

O script é idempotente: sem `--force`, ele pula o que já existe. No fim, imprime um resumo
com os assets criados, os pulados, os avisos e os erros.

Antes de começar, o script confere se os parâmetros que o C++ grava (`CPP_PARAMS`) estão em
`MATERIAL_PARAMS`. Depois da etapa `materials`, ele lê os parâmetros de cada mestre no editor
e compara com essa lista. Um parâmetro faltando conta como erro no resumo.

**Dica:** a etapa `level` usa o `LevelEditorSubsystem`, que funciona melhor com o Editor
aberto. Se ela falhar na linha de comando, rode só essa etapa pelo Editor:
`--only=level`.

## O que é gerado

| Caminho | Conteúdo |
|---|---|
| `/Game/Vila/Textures/T_<set>_A / _N / _ORM` | Texturas de `assets/textures`. `_A` é sRGB. `_N` usa compressão de normal map com o verde invertido, porque as normais do Godot seguem o padrão OpenGL. `_ORM` é linear, com compressão de máscaras. |
| `/Game/Vila/Textures/T_skin_detail, T_skin_mask, T_eye, T_leaf` | Texturas da pele, do olho e das folhas. A máscara da pele não tem compressão nem mip e usa filtro *nearest*. |
| `/Game/Vila/Materials/MPC_Vila` | `Wetness`, `Night`, `Wind` (padrão 0,5) e `Snow`, mais o vetor `Focus` (xyz em cm; w = 1 liga o afinamento da copa) |
| `/Game/Vila/Materials/M_*` | Os 16 mestres. Os parâmetros estão em `MATERIAL_PARAMS`, no topo do script. |
| `/Game/Vila/Materials/Surfaces/MI_<set>` | Uma instância de `M_Surface` por conjunto de texturas, com `TileMeters = tile_m` de `textures.json` |
| `/Game/Vila/Vehicles/<modelo>/SM_<Parte>` | Peças dos carros. As instâncias por slot ficam em `/Game/Vila/Vehicles/Materials`. |
| `/Game/Vila/Characters/<m\|f>/SKEL_Human, SK_<Peça>, Anims/A_<anim>` | Personagens. As instâncias ficam em `/Game/Vila/Characters/Materials`. |
| `/Game/Vila/Maps/Vila` | Nível vazio com um `NavMeshBoundsVolume` sobre o mapa (`bounds` do JSON, 0–30 m de altura) e um `PostProcessVolume` global sem limites com o `M_Outline` |

Os materiais por slot seguem a mesma regra do Godot:

- **Carros:** `paint` usa `M_CarPaint` e `glass` usa `M_Glass`. `headlight`, `taillight` e
  `indicator` usam `M_Emissive`, e o `UVilaCarVisual` muda `Color`/`Intensity` por MID.
  `tire` usa `M_SurfaceLocal` com a textura `rubber`, em espaço do objeto porque a roda gira.
  Os demais slots usam `M_Color` com as cores do `make_cars.py` e a AO das cores de vértice.
- **Personagens:** `skin` usa `M_Skin`, `hair` usa `M_Hair` e `eye` usa `M_Eye`. `lash` e
  `metal` usam `M_Color`. `sole` e `strap` usam `M_Cloth` com as texturas de borracha e de
  lona. As roupas usam `M_Cloth` com o tecido e a escala de UV de `Humanoid.FABRIC`.

## Observações

- O `M_Outline` depende de `r.CustomDepth=3`, que já está no `DefaultEngine.ini`. Ele desenha o
  contorno onde `CustomStencil == 1`.
- O `M_Windows` começa com `Intensity = 0` e `NightDriven = 0`. O `AVilaBuilding::SetNight`
  grava `Intensity = noite × 1,4` no MID de cada prédio. Com `NightDriven = 1`, o MPC
  multiplicaria a noite de novo. Para uma MI estática que acende sozinha pelo MPC, use
  `NightDriven = 1` e `Intensity = 1,4`.
- O `M_Grass` e o `M_Leaf` multiplicam a cor pela `PerInstanceCustomData` 0..2, com padrão 1. O
  `UVilaGrass` e o `UVilaNature` gravam nela o tom de cada tufo e de cada copa.
- O `M_Surface` reproduz o `Mat.SURFACE_SHADER` com um nó Custom em HLSL:
  - mesma projeção por eixo e mesmo detile;
  - mesmo ruído de macrovariação;
  - mesmo efeito de molhado (`MPC_Vila.Wetness * Outdoor`, mais forte no topo).

  A normal sai em espaço do mundo.
- O `M_SurfaceLocal` repete a textura a cada `TileMeters * Scale` metros. O padrão de
  `TileMeters` é 1.
