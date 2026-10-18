# Vila Aurora — Documento de Design e Arquitetura

> Open World Life RPG: a profundidade sistêmica de Project Zomboid, a liberdade urbana de GTA e a
> imersão de Red Dead Redemption 2, numa cidade original e sem zumbis.

Este documento responde aos 8 pontos pedidos na primeira entrega: visão, engine, câmera e arte,
arquitetura, plano de fases, recorte inicial, riscos e critérios de conclusão. O que já está
implementado (e o que não está) fica em [`PROTOTIPO.md`](PROTOTIPO.md). A estrutura da história
principal está em [`HISTORIA.md`](HISTORIA.md).

---

## 1. Visão e identidade

**Premissa:** e se Project Zomboid fosse um RPG de mundo aberto numa cidade moderna e funcional?

**Cidade:** *Porto Serra*, uma cidade litorânea brasileira fictícia de porte médio. O primeiro bairro é a
**Vila Aurora**, um bairro residencial e de comércio popular. Depois vêm o Centro, a Orla, o Distrito
Industrial, os Altos da Serra (alto padrão) e a Rodovia.

**O jogador:** alguém que acabou de se mudar para uma casa alugada com R$ 60 no bolso. Pode virar um
trabalhador estável, um empreendedor, um colecionador de carros, um vizinho querido, um explorador
urbano, alguém que vive à margem ou um criminoso. Nada disso exige a história principal.

**O que torna o jogo único — quatro regras de design:**

1. **Tudo tem função.** Uma geladeira conserva comida; uma lixeira tem latinhas que o Beto compra; uma
   porta tem chave, horário e dono. Objeto só vira cenário quando não tem papel na simulação.
2. **Sistemas conversam entre si.** Cansaço deixa o trabalho mais lento → menos gorjeta → menos
   dinheiro para o aluguel. Ajudar a vizinha → ela conta para o dono do mercadinho → desconto.
3. **Consequências têm testemunhas.** O mundo só "sabe" o que alguém viu. Crimes sem testemunha deixam
   vítimas desconfiadas; crimes testemunhados viram multa, reputação e memória dos NPCs.
4. **A vida cotidiana é o conteúdo.** Necessidades criam decisões (comer agora ou trabalhar mais uma
   hora?), não obrigações a cada minuto.

**Tom:** crônica urbana brasileira — calorosa, irônica, às vezes dura. Gráfico estilizado, legível
de cima, com luz e clima que mudam o humor das ruas.

## 2. Engine: Godot 4 (4.4)

| Critério | Godot 4 | Unreal Engine 5 |
|---|---|---|
| Câmera isométrica/3/4 e arte estilizada | Excelente; 3D leve com renderizador Forward+ e fallback de compatibilidade (OpenGL) | Excelente, mas otimizado para realismo |
| Hardware modesto | Roda em GPUs integradas (renderizador de compatibilidade) | Exigente; Lumen/Nanite pedem GPU moderna |
| Muitas entidades simples | Bom com simulação em níveis (LOD de IA), servidores de baixo nível e MultiMesh | Muito bom (Mass Entity), porém complexo |
| Iteração e dados | GDScript, recarga rápida, conteúdo em JSON | C++/Blueprint, compilações longas |
| Custo e licença | MIT, sem royalties | Royalties acima do teto de receita |
| Testes automatizados | Roda headless; testes de unidade e de aceitação em CI | Possível, mais pesado |

**Escolha: Godot 4.4.** A escala do jogo (cidade compacta, densa em sistemas, visual estilizado) não
precisa do realismo da Unreal; precisa de iteração rápida, dados fáceis de editar, bom desempenho em
máquinas modestas e testes automatizados. Pontos fracos conhecidos do Godot — streaming de mundo
grande e multidões — são resolvidos pela arquitetura (setores, níveis de simulação) e não dependem da
engine. Se algum sistema virar gargalo (ex.: pathfinding de centenas de agentes), ele pode ser movido
para C++ via GDExtension sem trocar de engine.

## 3. Câmera e direção artística

- **Câmera 3/4 em perspectiva** (FOV 34°, inclinação 52°), girável em passos de 45° e com zoom.
  Perspectiva estreita dá a leitura de um jogo isométrico com profundidade real e iluminação 3D.
- **Corte de paredes estilo Project Zomboid:** ao entrar em um prédio (ou ficar atrás dele), o telhado
  some e as paredes voltadas para a câmera baixam. Interiores fazem parte do mesmo mundo, sem tela de
  carregamento.
- **Arte estilizada "maquete viva":** formas simples, cores quentes, materiais com shader procedural
  (asfalto com granulação, calçada de ladrilho, piso de tábua, grama com variação). Personagens
  low-poly com animação procedural (andar, correr, sentar, deitar, trabalhar, gesticular).
- **Luz e clima:** ciclo de dia e noite, céu procedural, sombras direcionais suaves, SSAO e glow
  (Forward+), janelas e postes que acendem à noite, chuva com piso molhado e reflexos (efeitos altos).
- **Desempenho primeiro:** geometria estática agrupada em blocos de 40 m (poucas chamadas de desenho,
  descarte por visibilidade), luzes sem sombra, efeitos escalonáveis nas opções.

Todos os modelos, sons, nomes e textos são originais. Os modelos atuais são gerados por código a partir
de primitivas — são *placeholders de qualidade*, organizados para serem substituídos por arte final
(ver "Placeholders" em `PROTOTIPO.md`).

## 4. Arquitetura técnica

### Camadas

```
data/                  Conteúdo (JSON): itens, NPCs, agendas, diálogos, missões, lojas, emprego,
                       veículos, mapa do bairro, falas ambientes, traduções.
src/autoload/          Serviços globais: Events (barramento), Data (banco de dados), Settings,
                       Clock (tempo), Game (estado da partida), Saves (salvamento), Audio.
src/systems/           Simulação em lógica pura (RefCounted, sem cena — testável headless):
                       Needs, Inventory, Skills, Relations, QuestSystem, DialogueRunner, Rules,
                       LawSystem, ShopSystem, DriveThruJob, PropertySystem, WorldState, PlayerState.
src/world/             Apresentação do mundo: WorldBuilder (gera o bairro a partir do JSON),
                       Building (corte de paredes), Door, Furniture, GroundItem, DayNight,
                       CameraRig, GameWorld (orquestra e faz consultas espaciais).
src/actors/            Player (controle), NPC (rotinas, reações), Humanoid (visual/animação).
src/vehicles/          Vehicle (física arcade), CarVisual, TrafficSystem (grafo de faixas,
                       semáforo), TrafficCar (IA de trânsito e clientes do drive-thru).
src/ui/                HUD e janelas (inventário, contêiner, loja, diálogo, diário, mapa,
                       pausa, opções, menu principal).
src/audio/             Gerenciador de áudio com síntese procedural e substituição por arquivos.
tests/                 Testes de unidade (sistemas) e de aceitação (o protótipo inteiro).
tools/                 Gerador do mapa, script de testes.
```

### Princípios

- **Simulação separada da apresentação.** O estado que importa (necessidades, inventários,
  contêineres, portas, veículos, missões, relações, crimes, emprego) vive em objetos de lógica pura
  dentro de `Game`. Os nós 3D leem e escrevem nesse estado. Por isso o salvamento é só
  `Game.to_dict()`, e os sistemas são testados sem abrir uma cena.
- **Barramento de eventos.** Sistemas emitem (`Events.notify`, `inventory_changed`,
  `request_dialogue`…), a interface e o mundo escutam. Nenhum sistema conhece o HUD.
- **Dados acima de código.** Um NPC novo é um bloco em `npcs.json` (aparência, agenda, diálogo).
  Um diálogo usa uma linguagem de condições e efeitos (`Rules`) compartilhada com as missões.
  Um prédio novo é uma entrada no mapa (tamanho, portas, janelas, móveis, pontos de NPC).
- **Interação por contrato.** Qualquer nó no grupo `interactable` com `get_actions()`/`do_action()`
  aparece no prompt contextual (portas, móveis, NPCs, veículos, itens no chão).
- **Simulação em níveis (LOD de IA).** NPCs perto: física, desvio e animação. Longe: deslocamento
  simplificado sem física. Dentro de prédios fechados: ocultos, só a agenda avança. Em saltos de
  tempo (dormir), os NPCs vão direto para onde a agenda manda — o resultado é coerente sem simular
  cada passo.
- **Tempo de jogo único.** `Clock` emite `time_advanced(minutos, salto)`; necessidades, comida,
  turno de trabalho, clima e rotinas consomem o mesmo sinal.

### Sistemas e dependências

| Sistema | Depende de | Usado por |
|---|---|---|
| Needs | — | Player (velocidade), Furniture (ações), Job (tempo de tarefas), Saves |
| Inventory | Data | Player, contêineres, porta-malas, lojas, emprego |
| Rules (condições/efeitos) | Game, Data | Diálogos, missões |
| DialogueRunner | Rules | DialoguePanel |
| QuestSystem | Rules, Data | Diálogos, HUD (marcadores) |
| LawSystem | Relations | GameWorld (testemunhas), lojas (recusa), emprego (demissão) |
| ShopSystem | Relations, Data | Balcões, NPCs (compram de verdade) |
| DriveThruJob | Data, Needs | Estações, TrafficCar (clientes), HUD |
| WorldState | Data | Portas, contêineres, veículos, itens no chão |
| TrafficSystem | mapa | Clientes do drive-thru, pedestres |

## 5. Plano de desenvolvimento (fases verificáveis)

A ordem pedida foi reorganizada para cumprir primeiro os **3 pilares do protótipo**, como determinado
na seção "Prioridade máxima". Cada fase termina com uma versão executável e testes automatizados.

| Fase | Entregas | Estado |
|---|---|---|
| **0 — Protótipo dos 3 pilares (Vila Aurora)** | Bairro vivo, necessidades, inventário, economia, drive-thru, casa persistente, direção, trânsito, diálogos, missão com múltiplas soluções, salvar/carregar, menus e opções | **Entregue nesta versão** |
| 1 — Validação e polimento do protótipo | Testes com jogadores, balanceamento (taxas de necessidades, salários, preços), feedback de som e animação, ajustes de legibilidade | Próxima |
| 2 — Profundidade da vida | Temperatura e roupas, culinária com receitas, ferimentos localizados, mais itens, guarda-roupa com troca de roupa, compra de móveis | |
| 3 — Cidade e mobilidade | Carregamento por setores, 2º e 3º bairros, ônibus, oficina com peças, motos e vans, garagens, multas de trânsito, polícia de trânsito | |
| 4 — Progressão e liberdade | Mais profissões (entregador, caixa, mecânico), compra e venda de imóveis, aluguel de imóveis próprios, reputação por bairro, relacionamentos (amizade/romance/rivalidade), traços de personagem | |
| 5 — Conflito e narrativa | Combate corpo a corpo e armas, polícia e perseguições, sistema judicial, história principal (6–9 h), missões secundárias e eventos dinâmicos | |
| 6 — Polimento e distribuição | Arte final, áudio final, animações, otimização medida no hardware-alvo, localização, builds distribuíveis | |

## 6. Recorte inicial (o protótipo)

Um bairro de ~130 × 130 m com 4 quarteirões, 9 cruzamentos e um semáforo:

- **Residencial:** sua casa (nº 8), casa da Dona Lúcia (nº 10), do Marcos (nº 12), dos Silva (nº 14).
- **Comércio:** Mercadinho Bom Preço, Penhores do Beto, posto de combustível.
- **Drive-thru:** Brasa Burger com cozinha, janela, fila de carros e escritório.
- **Praça da Aurora:** chafariz, bancos, lixeiras, ponto de ônibus, rota de corrida.
- **Oficina do Rafael** (conserto de carro) e **Edifício Aurora** (moradia de NPCs).
- **10 NPCs com agenda e personalidade** + pedestres com rotina simplificada + trânsito.

Ele representa o jogo final em escala reduzida: todos os pilares estão presentes e conectados.

## 7. Riscos técnicos e estratégias

| Risco | Estratégia |
|---|---|
| Escopo gigantesco | Fases com critérios de aceitação automatizados; nenhum conteúdo novo sem sistema validado |
| Desempenho com muitos NPCs/carros | Simulação em níveis; NPCs ocultos dentro de prédios; trânsito cinemático; densidade configurável; medir antes de prometer metas |
| Mundo grande | Mapa em JSON por bairro → carregamento por setores; geometria agrupada em blocos de 40 m |
| Navegação | NavigationMesh gerada a partir das colisões; portas fora da malha (abrem sozinhas para quem tem chave); destravamento de NPC preso |
| Acoplamento entre sistemas | Lógica pura + eventos + linguagem de regras de dados; testes de unidade por sistema |
| Saves quebrando entre versões | Campo `version`, migrações, carregamento por cima do estado inicial do mapa (conteúdo novo continua aparecendo), backup automático e escrita atômica |
| Arte e áudio | Gerados proceduralmente (originais) até a arte final; caminhos de substituição já previstos |
| Tempo de jogo x necessidades irritantes | Taxas em dados; moodles só quando relevantes; dormir/esperar aceleram o tempo |
| Renderizadores diferentes | Forward+ com fallback de compatibilidade testado (o protótipo foi validado no renderizador OpenGL) |

## 8. Critérios de conclusão de fase

Uma fase só é considerada concluída quando:

1. **Executa** sem erros de script do menu principal até salvar e carregar.
2. **Testes de unidade** de todos os sistemas novos passam (`tests/TestRunner.tscn`).
3. **Teste de aceitação** cobre os fluxos da fase usando o mundo real — física, navegação, interface —
   e passa (`tests/AcceptanceTest.tscn`).
4. **Salvar → sair → carregar** preserva tudo que a fase introduziu (verificado no teste).
5. **Opções** novas funcionam e persistem.
6. **Documentação** de status atualizada: o que funciona, o que foi testado, o que é limitado.
7. **Nenhum recurso existente regride** (os testes antigos continuam passando).

Critérios específicos do protótipo (os 9 pedidos) e como cada um é verificado: ver `PROTOTIPO.md`.
