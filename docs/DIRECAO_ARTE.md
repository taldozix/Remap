# Direção de arte — "diorama vivo"

Referência enviada: cena de fim de tarde, vista de cima em três-quartos, com grama alta e densa,
trilha de terra com pedrinhas, árvores de folhagem amarela, névoa quente ao fundo e um forte
desfoque de miniatura nas bordas — a sensação de olhar uma maquete viva.

> O vídeo do Instagram não pôde ser aberto do ambiente de desenvolvimento (domínio bloqueado);
> a análise abaixo foi feita a partir da imagem enviada.

## O que torna a referência bonita

| Elemento | Na referência | Princípio para o jogo |
|---|---|---|
| Câmera | Alta, 3/4, longe dos personagens (pequenos na tela) | Personagens pequenos num mundo detalhado: o protagonista é o lugar |
| Desfoque de miniatura | Faixas desfocadas em cima e embaixo; foco nítido no meio | Tilt-shift como identidade visual, ajustável nas opções |
| Luz | Sol baixo, contraluz, sombras longas, tons dourados | **Só no amanhecer e no entardecer.** O resto do dia tem luz branca e céu azul; à noite, frio; com chuva, cinza |
| Vegetação | Grama em milhares de lâminas, variação de tom em manchas | Volume e quantidade valem mais que detalhe por objeto |
| Árvores | Copas feitas de muitas folhas, não de esferas | Folhagem por "cartões" de folhas com recorte alfa e volume suave |
| Chão | Terra com pedrinhas, bordas irregulares, sulcos | Materiais com borda orgânica — nada de retângulos perfeitos |
| Atmosfera | Névoa que suaviza o fundo | Neblina leve que muda de cor com o céu |

## O que já está implementado (passada visual 1)

- **Grama em tufos** (`src/world/GrassField.gd`): ~69 mil tufos de 11 lâminas na densidade média,
  em blocos de 20 m com distância de visibilidade; não nasce em ruas, calçadas, prédios e trilhas
  (grade de ocupação de 0,5 m). Vento por shader, manchas mais altas e mais secas por ruído,
  escurecimento na raiz (oclusão). Construção medida em ~0,2 s.
- **Árvores com folhas** (`src/util/FoliageBatcher.gd`, `Nature.leaf_texture()`): copas de ~180
  cartões de folhas por árvore, com textura de folhas gerada por código, normais esféricas
  (volume suave), balanço ao vento e sombras recortadas (luz salpicada no chão). Galhos e troncos
  afunilados; maioria verde, com árvores amarelas e douradas como destaque.
- **Trilhas de terra** (`Nature.dirt_material()`): pedrinhas, sulcos e bordas recortadas por
  ruído — na praça e numa trilha que contorna o bairro.
- **Pós-processamento** (`src/world/PostFX.gd`): desfoque de miniatura (tilt-shift),
  gradação de cor e vinheta. Fica abaixo da interface (o HUD nunca desfoca).
- **Luz por hora e clima** (`src/world/DayNight.gd`): o sol sobe até ~66° ao meio-dia (sombras
  curtas, luz branca) e só fica baixo e dourado perto das 6h e das 18h30. A gradação da câmera
  acompanha: dourada no amanhecer/entardecer, neutra de dia, fria à noite, cinza e dessaturada
  com nuvens e chuva (sombras quase sem contorno). Veja `docs/img/horarios_e_climas.png`.
- **Opções**: densidade da grama (sem/baixa/média/alta), desfoque de miniatura
  (desligado/sutil/forte), gradação de cor e vinheta. Tudo persiste.

Validação: capturas automáticas em 8 combinações de hora e clima
(`tests/VisualTimes.tscn`) e regressão completa dos testes (283 de unidade, 75 + 60 de aceitação).

## O que ainda separa o jogo da referência

Honestamente, a referência usa arte feita à mão e iluminação mais cara. Para chegar lá:

| Lacuna | Caminho | Quando |
|---|---|---|
| Personagens de primitivas | Modelos low-poly estilizados com esqueleto e animações (Blender), mantendo `Humanoid.set_pose()` | Fase 6 (polimento), com artista 3D |
| Folhas e grama geradas por código | Atlas de folhas e lâminas pintados à mão; variação por espécie (ipê, oiti, sibipiruna) | Fase 6 |
| Iluminação indireta | No Forward+: SDFGI ou VoxelGI por setor, névoa volumétrica com raios de sol; luz indireta pré-calculada nos interiores | Após medir desempenho no hardware-alvo |
| Desfoque de profundidade real | Usar a profundidade da cena (DOF físico do Forward+) em vez de faixas fixas de tela | Opção "alta" no Forward+ |
| Materiais | Texturas PBR estilizadas (tijolo, reboco, telha, madeira) em vez de cores lisas | Fase 6 |
| Interação com a vegetação | Grama que se dobra sob o personagem e os carros | Fase 2 |
| Estações e clima | Ventania, neblina matinal, folhas caindo, poças | Fase 3 |

## Regras de consistência

1. Dourado é um momento do dia, não uma cor do jogo.
2. A leitura vem primeiro: personagens, portas e itens interativos sempre se destacam do cenário.
3. Todo efeito tem nível nas opções e um custo medido; nada que não possa ser desligado.
4. Arte nova entra como substituição de um placeholder com a mesma interface, sem mudar sistemas.
