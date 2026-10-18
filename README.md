# Vila Aurora — Open World Life RPG (protótipo)

Um RPG de mundo aberto sobre **viver** numa cidade simulada: a profundidade sistêmica de
Project Zomboid, a liberdade urbana de GTA e a imersão de Red Dead Redemption 2 — sem zumbis,
com identidade, cidade e personagens originais.

Este repositório contém o **protótipo jogável** (Fases 0 e 1): um bairro pequeno, porém vivo,
onde é possível trabalhar num drive-thru ou como entregador, fazer compras, cuidar das necessidades
(inclusive frio, chuva e dor), dirigir, comprar casa, garagem e carro, consertar o próprio carro,
conversar, resolver uma missão de várias formas, reagir a eventos na rua e salvar o progresso.

- **Engine:** Godot 4.4 (GDScript) — abra `project.godot` e pressione F5.
- **Testes:** `GODOT=/caminho/do/godot tools/run_tests.sh`
  (283 verificações de unidade + testes de aceitação do protótipo e da Fase 1).

## Direção de arte

![Elenco](docs/img/arte_elenco.png)

Personagens com esqueleto e animações a partir de dados CC0 do MakeHuman, com roupas, cabelos
e acessórios gerados por script. Quatro carros modelados no Blender (hatch, sedã, picape, van).
Prédios, ruas e interiores com texturas PBR próprias (tijolo, reboco, telha, asfalto, pedra
portuguesa, madeira, tecido…). A luz muda com a hora e o clima, e o dourado aparece só no
amanhecer e no entardecer. Detalhes, licenças e limitações estão em
[`docs/DIRECAO_ARTE.md`](docs/DIRECAO_ARTE.md); créditos em
[`assets/CREDITS.md`](assets/CREDITS.md); como regerar os ativos em
[`tools/art/README.md`](tools/art/README.md).

| | |
|---|---|
| ![Frota](docs/img/arte_frota.png) | ![Carro](docs/img/arte_carro.png) |

![Horários e climas](docs/img/arte_horarios.png)

## Capturas (renderizador de compatibilidade/OpenGL)

| | |
|---|---|
| ![Casa por dentro](docs/img/01_casa_interior.png) | ![Mercadinho](docs/img/05_mercadinho_dentro.png) |
| ![Cozinha do drive-thru](docs/img/07_brasa_cozinha.png) | ![Trânsito](docs/img/08_cruzamento.png) |
| ![Noite](docs/img/09_noite.png) | ![Diálogo](docs/img/12_dialogo.png) |

## Documentação

- [`docs/DESIGN.md`](docs/DESIGN.md) — visão, escolha da engine, câmera e arte, arquitetura,
  plano de fases, riscos e critérios de conclusão.
- [`docs/PROTOTIPO.md`](docs/PROTOTIPO.md) — como rodar, controles, o que está implementado,
  como cada critério foi verificado e limitações conhecidas.
- [`docs/HISTORIA.md`](docs/HISTORIA.md) — estrutura da história principal (planejada).
- [`docs/DIRECAO_ARTE.md`](docs/DIRECAO_ARTE.md) — auditoria, pesquisa de ativos, modelos,
  materiais, desempenho e limitações da direção de arte.

## Estrutura

```
assets/      modelos (carros, personagens), texturas PBR e créditos
data/        conteúdo em JSON (itens, NPCs, diálogos, missões, lojas, emprego, veículos, mapa, traduções)
src/         código (autoload, systems, world, actors, vehicles, ui, audio, util)
tests/       testes de unidade, aceitação e capturas de tela
tools/       gerador do mapa, script de testes e pipeline de arte (tools/art)
docs/        documentação
```

Licença: MIT (ver `LICENSE`) para o código e os ativos próprios; dados do MakeHuman em CC0 (ver `assets/CREDITS.md`).
