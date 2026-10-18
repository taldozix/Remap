# Protótipo "Vila Aurora" — o que está pronto, como testar e o que falta

Versão: protótipo 0.1 (Fase 0 — os 3 pilares). Engine: Godot 4.4.1.

Este documento é o relatório honesto do estado atual: o que funciona, como foi verificado e quais são
as limitações conhecidas.

## Como rodar

1. Instale o [Godot 4.4](https://godotengine.org/download) (versão padrão, não a .NET).
2. Abra a pasta do repositório no Godot (Importar → `project.godot`) e pressione **F5**,
   ou pela linha de comando: `godot --path .`
3. Testes automatizados (sem janela):
   ```bash
   GODOT=/caminho/do/godot tools/run_tests.sh          # unidade + aceitação
   GODOT=/caminho/do/godot tools/run_tests.sh unit     # só unidade (~5 s)
   GODOT=/caminho/do/godot tools/run_tests.sh acceptance  # só aceitação (~3 min)
   ```
4. Capturas de tela automáticas (precisa de janela ou Xvfb):
   `godot --path . res://tests/VisualCheck.tscn -- --screens=/pasta/de/saida`

O projeto usa o renderizador **Forward+** e cai automaticamente para **Compatibilidade (OpenGL 3)**
quando não há Vulkan. Toda a validação visual deste protótipo foi feita no renderizador de
compatibilidade (o caminho de hardware mais modesto).

## Controles (padrão — todos remapeáveis em Opções → Controles)

| Ação | Tecla |
|---|---|
| Andar / acelerar, frear, virar | W A S D ou setas |
| Correr | Shift |
| Interagir (ação principal do objeto em foco) | E |
| Outras ações do objeto em foco | 1, 2, 3, 4 |
| Entrar/sair do veículo | F |
| Freio de mão / buzina | Espaço / H |
| Inventário / diário / mapa | Tab / J / M |
| Girar câmera / zoom | Z, X / roda do mouse (ou + e −) |
| Pausa (salvar, carregar, opções) | Esc |
| Salvamento rápido / diagnóstico | F5 / F3 |

## Critérios de aprovação do protótipo

Todos os 9 critérios são verificados automaticamente por `tests/AcceptanceTest.tscn`, que roda o
mundo real (física, malha de navegação, trânsito, interface) e usa os mesmos caminhos de código do
jogador: o personagem anda pelas ações de input, o foco de interação é calculado como no HUD, e as
janelas de loja, diálogo, contêiner e sono são as mesmas da interface.

| # | Critério | Como funciona no jogo | Verificação automática |
|---|---|---|---|
| 1 | Sair de casa e explorar | Porta com chave e trinco; corte de paredes ao entrar/sair; calçadas, praça, comércio | Anda até a porta, abre pelo foco de interação, sai correndo; jogador fora do prédio; correr gera fadiga |
| 2 | NPCs realizando atividades | 10 NPCs com agenda (dormir, trabalhar, comprar, sentar, correr, pegar ônibus, conversar) + pedestres + trânsito | Às 8h30 a Lúcia está no banco da praça, o Antônio no balcão, o Marcos na oficina; às 9h30 a Lúcia vai a pé ao mercadinho pela navegação; há carros circulando |
| 3 | Dirigir um veículo | Carro próprio com aceleração, frenagem, ré, inércia, derrapagem, combustível, dano | Entra no carro, acelera e freia, o carro anda >6 m, consome combustível, sai, posição salva no estado do mundo |
| 4 | Entrar em um estabelecimento | Lojas abrem e trancam pelo horário; interior acessível | Mercadinho destrancado às 10h40, porta aberta pelo foco, jogador entra andando |
| 5 | Trabalhar no drive-thru e receber | Entrevista com a Sandra, ponto, carros do trânsito entram na fila, pedidos com paciência, chapa/fritadeira com tempo de preparo (pode queimar), refri, entrega na janela, salário + bônus + gorjeta | Contratação pelo diálogo, ponto de entrada, carro do trânsito faz pedido real, preparo nas estações, entrega, cliente vai embora, ponto de saída paga o turno, ganha XP de culinária |
| 6 | Comprar alimentos e cuidar das necessidades | Balcão só funciona com o atendente presente e no horário; estoque real; comer reduz fome | Compra sanduíche e água (R$ 60 → R$ 47), come pelo inventário (fome 60 → 32) |
| 7 | Voltar para casa, descansar, guardar objetos | Geladeira (conserva comida), guarda-roupa, cama (dormir N horas, salvamento automático) | Guarda água na geladeira, dorme 8 h, sono 70 → 0, salvamento automático criado |
| 8 | Conversar e iniciar uma missão | Diálogo com escolhas e testes de habilidade; missão "A Encomenda Extraviada" com 7 desfechos | Lúcia na calçada à tarde, missão iniciada, marcador no Marcos, suborno de R$ 40, entrega à Lúcia, missão concluída, relação sobe |
| 9 | Salvar, sair e carregar | Salvamento JSON versionado, escrita atômica, backup, migração | Salva, zera o estado, carrega e confere dinheiro, necessidades, data/hora, geladeira, guarda-roupa, carro, combustível, missão, emprego, relações, inventário, posição, porta e uma consequência (desconto do Seu Antônio) |

Último resultado: **75 verificações, 0 falhas** (aceitação) e **218 verificações, 0 falhas** (unidade).

## O que está implementado

### Pilar 1 — um bairro pequeno, mas vivo
- Bairro de ~130 × 130 m gerado a partir de `data/map/vila_aurora.json` (fonte: `tools/build_map.py`):
  9 cruzamentos, faixas, faixas de pedestre, calçadas, 4 casas, mercadinho, loja de penhores, posto,
  drive-thru com pista, oficina, prédio residencial, praça com chafariz, ponto de ônibus.
- **Interiores acessíveis** com corte de paredes e telhado (estilo Project Zomboid), inclusive quando
  o jogador fica atrás de um prédio.
- **NPCs** (`data/npcs.json`): Dona Lúcia, Marcos, Seu Antônio, Beto, Sandra, Kleber, Rafael, Júlia,
  Tião, Paula + 6 pedestres. Cada um tem casa, trabalho, agenda, personalidade e diálogo.
  - Abrem portas com as próprias chaves; pegam ônibus (somem no ponto e voltam à tarde).
  - Compram no mercadinho de verdade (o estoque diminui) e só quando o dono está no balcão.
  - Conversam em pares na praça, com fofocas que mudam conforme o que você fez.
  - Reagem a esbarrões, ao seu carro em alta velocidade, ao cheiro (higiene), à buzina e à chuva
    (vão para casa). O dono reage se você entra na casa dele; a equipe reclama se você entra na
    cozinha do Brasa sem trabalhar lá.
  - Simulação em níveis: perto (física + animação), longe (deslocamento simplificado e mais
    rápido), oculto (dentro de prédios fechados), salto de tempo (vão direto ao lugar da agenda).
- **Trânsito**: grafo de faixas de mão dupla, semáforo no cruzamento central, preferência nos
  demais, distância do carro da frente, parada para pedestres e para o jogador, buzina, densidade
  configurável. Clientes do drive-thru saem do trânsito e voltam para ele.
- **Veículo dirigível** e carros estacionados de NPCs (trancados; dá para arrombar — é crime).
- Ciclo de dia e noite, clima (limpo, nublado, chuva com piso molhado), postes e janelas acesos.

### Pilar 2 — a simulação de Project Zomboid
- **Necessidades**: fome, sede, sono, fadiga, higiene e saúde, com taxas por atividade e efeitos
  reais (velocidade de locomoção, impossibilidade de correr, lentidão nas tarefas do trabalho,
  perda de saúde, desmaio com conta de R$ 80 no posto de saúde).
- **Inventário** limitado por peso (12 kg; mochila +8 kg), sobrecarga deixa lento. Comida perecível
  que estraga (geladeira desacelera 4×); comida estragada causa enjoo.
- **Mundo interativo**: portas (chave, trinco, bater), geladeira, guarda-roupa, baú, caixas,
  lixeiras (latinhas renovam diariamente), fogão (cozinhar macarrão), pia (beber/lavar), chuveiro,
  sofá/cadeira/banco (sentar e esperar), cama (dormir/descansar), bilhete, bomba de combustível,
  tapete (com a chave do Marcos embaixo), itens no chão (largar e pegar — persistentes).
- **Dinheiro**: compra (com desconto de freguês), venda no penhor, salário, gorjetas, aluguel semanal
  (vira dívida se faltar), multas, combustível, conserto do carro na oficina.
- **Profissão funcional**: drive-thru (detalhes na tabela acima), com desempenho, advertências,
  demissão (abandono, clientes perdidos, crime visto pela gerente) e promoções com aumento.
- **Moradia** alugada com armazenamento persistente.
- **Salvamento**: 5 espaços (automático, rápido e 3 manuais), metadados, backup `.bak`,
  validação de arquivo corrompido, migração de versões.

### Pilar 3 — liberdade de GTA e RPG
- Dirigir, explorar, conversar, trabalhar e ignorar a missão à vontade.
- **Diálogos** orientados a dados com condições, efeitos e testes de habilidade com chance visível
  ("[Persuasão 54%]"). Interações rápidas (cumprimentar, comprar) não abrem cenas; conversas com
  personagens importantes aproximam a câmera e pausam o relógio. Esc encerra naturalmente.
- **Missão "A Encomenda Extraviada"** com várias soluções:
  persuadir o Marcos (teste social), subornar (R$ 40), intimidar (teste de condicionamento — é crime
  de ameaça), descobrir com o Tião que a chave está debaixo do tapete e pegar a caixa no baú
  (furto, com ou sem testemunhas), ou comprar o remédio no mercadinho. Ao entregar, mentir ou
  contar a verdade à Lúcia. São 7 desfechos.
- **Consequências observáveis**: relações e memórias dos NPCs, falas diferentes (o Marcos
  desconfiado, a Lúcia decepcionada se viu você invadir), desconto no mercadinho, bolo diário da
  Lúcia, recusa de atendimento, fofoca na praça, multas no dia seguinte, demissão.
- **Lei**: furto, invasão de domicílio, ameaça, atropelamento (com 30 s para prestar socorro e
  reduzir a pena; fugir gera "omissão de socorro"), batida de trânsito, furto de veículo.
  Testemunhas reais (linha de visão, acordadas, no raio).
- **Habilidades por prática**: culinária (cozinha mais rápido, janela maior antes de queimar),
  social (chance de persuasão e gorjetas), direção (controle e menos dano), condicionamento
  (corrida mais rápida e menos cansativa).

### Interface, opções e áudio
- Menu principal com criação de personagem (nome, pele, cabelo, camisa) e prévia 3D.
- HUD contextual: relógio, dinheiro, clima e local; *moodles* só quando relevantes; prompt de
  interação com teclas; barra de progresso de ações; painel do turno; painel do carro; marcadores
  de missão (setas na borda da tela); notificações.
- Janelas: inventário, contêiner, loja, diálogo, diário (missões, personagem, habilidades,
  relações com memórias, trabalho e casa, ocorrências), mapa, pausa, sono, mensagens.
- **Opções que funcionam e persistem** (`user://settings.cfg`): resolução, modo de tela, escala de
  renderização, sombras, efeitos (SSAO/glow/reflexos no Forward+), distância de visão/simulação,
  densidade de pedestres e trânsito, V-Sync, limite de FPS, filtro anisotrópico, MSAA; volumes
  geral/música/efeitos/vozes/ambiente; idioma (pt-BR/inglês na interface), legendas, tamanho do
  texto, sensibilidade da câmera, dicas; remapeamento de todas as teclas.
- **Áudio** 100% original, sintetizado em tempo de execução (passos, portas, motor com rotação,
  buzina, batida, caixa registradora, chapa, chuva, cidade, música do menu). Qualquer arquivo em
  `assets/audio/<id>.ogg|wav` substitui o som sintetizado automaticamente.

## Limitações conhecidas (versões simplificadas, documentadas)

- **Arte placeholder**: personagens, carros e móveis são primitivas geradas por código, com
  animação procedural. A arquitetura permite trocá-los por modelos (`Humanoid`, `CarVisual`,
  `WorldBuilder._furniture_visual`).
- **Retratos de diálogo** são iniciais coloridas, não rostos.
- **Combate, polícia em campo e perseguições** não existem ainda (Fase 5). Crimes geram
  multas/reputação/memórias, não prisões.
- **Compra de imóveis e de veículos** não está no protótipo (a estrutura `PropertySystem` já
  separa "alugada" de "própria"). Há apenas a casa alugada e o carro do jogador.
- **Temperatura corporal e troca de roupa** não foram implementadas.
- **Turnos não são salvos no meio**: não é possível salvar durante um turno (o jogo avisa).
- **Posições exatas dos NPCs não são salvas**: ao carregar, cada NPC aparece onde a agenda manda
  naquele horário (resultado coerente, ver arquitetura). Ferimentos e memórias são salvos.
- **Pedestres atravessam a rua em qualquer ponto** (os carros param para eles); faixas de pedestre
  ainda não guiam a navegação.
- **Carros do trânsito não podem ser roubados com motorista**; só carros estacionados.
- **Tradução para inglês** cobre menus e HUD básicos; diálogos e itens estão só em português.
- **Desempenho não foi medido em hardware real** (o ambiente de desenvolvimento usa renderização
  por software). O painel F3 mostra FPS e contadores de simulação para medir no hardware-alvo.
- **Escala de tempo**: 1 minuto de jogo = 1 segundo real (um dia = 24 min). As agendas foram
  ajustadas para o tempo de deslocamento a pé nessa escala.

## Placeholders a substituir (organizados)

| O quê | Onde | Substituição prevista |
|---|---|---|
| Modelos de personagens | `src/actors/Humanoid.gd` | Cena `.glb` com esqueleto e animações; manter a API `set_pose()` |
| Veículos | `src/vehicles/CarVisual.gd` | Modelos por tipo em `assets/vehicles/`, mantendo rodas/porta/luzes |
| Mobília e props | `WorldBuilder._furniture_visual`, `_build_prop` | Cenas por tipo em `assets/props/` |
| Sons | `src/audio/Audio.gd` | Arquivos em `assets/audio/<id>.ogg` (automático) |
| Retratos | `src/ui/DialoguePanel.gd` | Texturas em `assets/portraits/<npc>.png` |

## Próximos passos recomendados (Fase 1 — validação)

1. Sessões de jogo com pessoas reais; medir FPS no hardware-alvo e definir metas.
2. Balancear taxas de necessidades, salário, preços e aluguel com base nessas sessões.
3. Feedback: sons de interface por contexto, animações de entrar/sair do carro, ícones de moodles.
4. Clique do mouse para escolher alvos de interação em cômodos cheios.
5. Só então expandir: segundo bairro com carregamento por setores, mais profissões, compra de imóveis.
