# Vila Aurora no Unreal Engine 5

Port do protótipo Godot para o **Unreal Engine 5.5** (C++), com as melhorias que o motor
permite. O protótipo Godot continua na raiz do repositório, jogável e testado, e é a
referência de comportamento.

## Estado: o que foi verificado e o que não foi

| Parte | Onde | Verificação |
|---|---|---|
| Núcleo de simulação (necessidades, inventário, lojas, emprego, missões, diálogos, lei, propriedades, eventos, saves…) | `unreal/Source/VilaCore` | **Compilado e testado aqui.** Os 283 testes de `tests/TestRunner.gd` foram portados para `unreal/Tests/VilaCore` e passam (g++ e CMake, também com AddressSanitizer e UBSan). |
| Módulo de jogo (mundo, atores, veículos, trânsito, interface, ambiente, áudio) | `unreal/Source/VilaAurora` | **Nunca compilado.** Este ambiente não tem o Unreal Engine, que é distribuído pela Epic mediante conta e não pode ser baixado aqui. O código foi escrito contra a API pública do UE 5.5 e passou por revisão adversarial de cada parte. Um verificador estático confere as chamadas entre as partes: `python3 unreal/Tools/check_module.py`. Ainda assim, **a primeira compilação no Unreal provavelmente vai acusar erros pontuais** (nomes de API, includes) a corrigir. |
| Exportação de arte para o Unreal | `tools/art/blender/export_unreal.py` | **Executado aqui** (Blender 5.2/bpy). Pivôs, eixos, ossos, animações e shape keys foram conferidos reimportando os arquivos. |
| Criação de conteúdo no editor | `unreal/Scripts/build_content.py` | **Nunca executado no Unreal.** Foi testado apenas contra um módulo `unreal` simulado, o que confere a lógica do script, não a API. |
| Jogo rodando | — | **Não foi possível executar nem testar o jogo no Unreal.** Nada aqui afirma que ele roda. |

## Arquitetura

```
unreal/
  VilaAurora.uproject         projeto (UE 5.5; plugins Enhanced Input, Interchange, Python)
  Config/                     Lumen, sombras virtuais, iluminação dinâmica, navmesh dinâmica…
  Source/VilaCore/            núcleo C++ puro (namespace vila), sem UObject: o "cérebro" do jogo
  Source/VilaAurora/          módulo de jogo (UObject/Slate) que usa o núcleo
    API.md                    contrato entre as partes do módulo
  Content/Data/               JSON do jogo (cópia de data/: tools/sync_unreal_data.sh)
  ArtSource/                  carros e personagens em peças, exportados do Blender
  Scripts/build_content.py    gera /Game/Vila (texturas, materiais, malhas, mapa) no editor
  Tests/VilaCore/             testes do núcleo (CMake)
  Tools/check_module.py       verificador estático do módulo
```

### Núcleo (VilaCore)

O núcleo é o port linha a linha de `src/systems` e dos autoloads (`Game`, `Clock`, `Events`,
`Data`, `Settings`, `Saves`), com os mesmos nomes do GDScript. Correspondência de tipos:

| GDScript | VilaCore |
|---|---|
| Dicionários e arrays | `nlohmann::ordered_json` |
| Sinais | `Signal<...>` |
| `RandomNumberGenerator` | `Rng` |
| `tr()` | `vila::tr()` |
| `"%s" % [...]` | `vila::fmt()` |

O módulo do Unreal implementa `vila::IWorld` para responder às consultas espaciais
(testemunhas, área de trabalho, veículos, eventos).

Por ser C++ padrão, o núcleo compila fora do Unreal e é testado aqui:

```bash
cmake -S unreal/Tests/VilaCore -B build/vilacore && cmake --build build/vilacore -j
build/vilacore/vila_tests unreal/Content/Data      # 283 verificações
```

### Módulo de jogo (VilaAurora)

Correspondência com o Godot. Cada classe é o port de um arquivo `.gd`:

| Godot | Unreal |
|---|---|
| `GameWorld.gd` | `AVilaWorld`: monta tudo e implementa `vila::IWorld` |
| `WorldBuilder.gd`, `Building.gd`, `MeshBatcher`/`ChunkBatcher`/`FoliageBatcher` | `UVilaWorldBuilder`, `AVilaBuilding`, `UVilaMeshBatch` (instâncias HISM), `UVilaProcMesh` (malhas geradas em runtime) |
| `GrassField.gd`, `Nature.gd` | `AVilaGrass`, `UVilaNature` |
| `Door.gd`, `Furniture.gd`, `GroundItem.gd`, `DeliveryDrop.gd`, `Interactable.gd` | `AVilaDoor`, `AVilaFurniture`, `AVilaGroundItem`, `AVilaDeliveryDrop`, `IVilaInteractable` |
| `Player.gd`, `NPC.gd`, `Humanoid.gd`, `CameraRig.gd` | `AVilaPlayerCharacter`, `AVilaNPC`, `UVilaHumanoid` + `UVilaAnimInstance`, `AVilaCameraRig` |
| teclas do `Settings` | `AVilaPlayerController`: Enhanced Input criado em código a partir dos atalhos |
| `Vehicle.gd`, `CarVisual.gd`, `TrafficSystem.gd`, `TrafficCar.gd` | `AVilaVehicle`, `UVilaCarVisual`, `AVilaTrafficSystem`, `AVilaTrafficCar` |
| `DayNight.gd`, `PostFX.gd`, chuva | `AVilaEnvironment`, `UVilaWeather` |
| `Audio.gd` | `UVilaAudio`: sons sintetizados em `USoundWaveProcedural` ou arquivos em `/Game/Vila/Audio` |
| `src/ui/*` | `AVilaHUD` + widgets Slate em `UI/` (HUD, menu principal, criação de personagem, pausa, opções, saves, inventário, contêiner, diálogo, loja, diário, mapa) |

## O que melhora em relação ao Godot

- **Iluminação:** iluminação global e reflexos com **Lumen**, sombras virtuais (VSM) e luz
  100% dinâmica. Não há lightmaps, porque o mundo é montado em runtime. O ray tracing por
  hardware é opcional, nas opções.
- **Céu e clima:** céu físico (Sky Atmosphere), nuvens volumétricas, névoa de altura com
  névoa volumétrica e SkyLight em tempo real.
  - O ciclo do `DayNight.gd` foi mantido, e a regra de luz neutra entre 9h e 15h foi
    preservada.
  - O clima muda gradualmente. A chuva molha ruas e calçadas pelo parâmetro global
    `Wetness`.
- **Câmera:** profundidade de campo de câmera real para o efeito "miniatura" (tilt-shift) e
  anti-aliasing temporal **TSR**.
- **Materiais:** materiais PBR com projeção em caixa, o mesmo shader do Godot reescrito em
  HLSL. Os carros usam pintura com clear coat; a pele usa subsurface; a folhagem tem vento.
  O foco de interação ganha contorno no pós-processo.
- **Personagens:** esqueleto real com mistura entre animações (crossfade nativo, sem
  Animation Blueprint), roupas em peças com *leader pose* e morph targets para o corpo e o
  rosto.
- **Navegação:** navmesh **Recast** dinâmica para os NPCs, gerada depois que a vila é
  montada.
- **Controles:** **Enhanced Input**. Os atalhos remapeados no menu valem na hora, sem
  assets.
- **Desempenho:** milhares de peças em poucos componentes instanciados (HISM), com
  distância de corte por tamanho e escalabilidade do motor ligada às opções.
- **Opções de vídeo novas:** qualidade geral, iluminação global (desligada / Lumen /
  Lumen + RT), reflexos (SSR / Lumen), anti-aliasing (nenhum / FXAA / TSR), nuvens
  volumétricas e desfoque de movimento, além das opções que já existiam.
- **Menu principal:** usa a vila viva ao fundo, com a câmera orbitando.

As regras de jogo, os números, os textos e os diálogos são os mesmos do Godot, porque vêm do
mesmo núcleo e do mesmo `data/`.

## Como compilar e jogar (numa máquina com o Unreal)

1. **Instale:**
   - Unreal Engine 5.5, pelo Epic Games Launcher ou a partir do código.
   - No Windows, o Visual Studio 2022 com o workload "Game development with C++". No
     Linux, o toolchain do motor.
2. **Gere e compile o projeto:**
   - Clique com o botão direito em `unreal/VilaAurora.uproject` e escolha **Generate
     Visual Studio project files**.
   - Abra a solução e compile **VilaAuroraEditor / Development Editor / Win64**.
   - Alternativa pela linha de comando:
     `Engine/Build/BatchFiles/Build.bat VilaAuroraEditor Win64 Development -Project="…/VilaAurora.uproject"`.
3. **Gere o conteúdo:**
   - Abra o projeto no editor.
   - Rode **Tools › Execute Python Script…** com `Scripts/build_content.py`. Ele cria
     texturas, materiais, carros, personagens e o mapa `/Game/Vila/Maps/Vila`.
   - Detalhes e opções (`--force`, `--only=`) estão em `unreal/Scripts/README.md`.
4. **Jogue:** aperte **Play**. O jogo abre no menu principal.
   - Sem o passo 3 ele ainda abre, mas com cores sólidas no lugar das texturas e bonecos
     simples no lugar dos personagens, e mostra um aviso no menu.
   - **Sem o mapa, ele não abre:** o `DefaultEngine.ini` aponta para
     `/Game/Vila/Maps/Vila`, que só existe depois do passo 3. Rode pelo menos o
     `--only=level` do script.
5. **Empacote:** use **Platforms › Windows › Package Project**. Os JSON de `Content/Data`
   já estão configurados para irem junto.

Ao mudar algo em `data/`, rode `tools/sync_unreal_data.sh` para copiar para
`unreal/Content/Data`.

### Corrigindo a primeira compilação

Como o módulo nunca passou por um compilador, a primeira compilação provavelmente vai
apontar erros. As causas mais prováveis são nomes ou assinaturas de API que mudaram entre
versões, includes faltando e conversões float/double.

A forma mais rápida de fechar isso é rodar o Claude Code **na sua máquina**, onde o Unreal
está instalado, com acesso ao repositório e à compilação. Pode ser pelo app desktop ou
conectando esta sessão com `claude remote-control`. Assim é possível compilar, ler os erros
e corrigir em ciclo, e depois testar o jogo de verdade.

## Limitações conhecidas

- O módulo de jogo e o script do editor **não foram compilados nem executados**; veja a
  tabela de estado acima.
- **Valores calibrados no escuro:** foram estimados sem ver o resultado e precisam de
  ajuste no motor. Entram aqui:
  - as intensidades de luz (lux do sol, lúmens dos postes e das lâmpadas);
  - a exposição e as cores do grading;
  - as distâncias de corte;
  - as velocidades das animações.
- **Textos sem inglês:** a tradução para inglês cobre os menus, como no Godot. O resto
  continua em português nos dois motores.
- **Fonte dos letreiros:** os letreiros usam `UTextRenderComponent` com a fonte padrão do
  motor, que pode não ter todos os glifos (por exemplo `➜`).
- **Sem Animation Blueprint:** a mistura de animações é feita em C++ (`UVilaAnimInstance`).
  Se a API do proxy de animação recusar compilar, há um modo alternativo sem mistura
  (`VILA_ANIM_PROXY 0`).
- **Áudio sintetizado:** os sons continuam sendo gerados pelo próprio jogo, com as mesmas
  receitas do Godot. Arquivos em `/Game/Vila/Audio/<id>` os substituem automaticamente.

## Dúvidas de API registradas para a primeira compilação

<!-- preenchido a partir das revisões -->
