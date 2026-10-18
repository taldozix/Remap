# História principal — "Os Livros da Aurora"

Duração-alvo: 6 a 9 horas na primeira conclusão. Estrutura em 5 atos, 18 missões principais,
3 caminhos e 4 desfechos. A história pode ser ignorada por longos períodos: cada missão principal
só começa quando o jogador procura o personagem certo, e os prazos são em dias de jogo, não em
minutos reais.

> Estado: **planejada, não implementada** no protótipo. O protótipo contém apenas a missão
> independente "A Encomenda Extraviada", que já apresenta Lúcia, Marcos e Tião e usa os mesmos
> sistemas (diálogos de dados, regras, missões por etapas, testemunhas, memória dos NPCs) que a
> história vai usar.

## Premissa

Você chega à Vila Aurora para recomeçar: perdeu o emprego numa cidade grande e alugou a casa 8
sem conhecer ninguém. Na primeira semana, chega uma carta sem remetente endereçada ao **antigo**
morador da casa: *"Os livros não podem ficar na Aurora. Eles sabem."*

O antigo morador, **Ivo Salles**, era o dono da livraria-sebo que funcionava no térreo do Edifício
Aurora e desapareceu há seis meses. Os "livros" são cadernos de contabilidade que provam que a
construtora **Maré Alta** comprou fiscais e forjou laudos de risco para desapropriar a Vila Aurora e
erguer um condomínio de luxo na orla.

A história é sobre um bairro comum sendo vendido aos pedaços — e sobre o que você está disposto a
fazer, a quem ajudar e por quanto.

## Personagens

| Personagem | Papel | Motivação | Segredo |
|---|---|---|---|
| **Você** (nome escolhido) | Protagonista | Recomeçar; pagar as contas | Tem dívidas com um ex-sócio na cidade antiga (opcional, define um evento do ato 3) |
| **Dona Lúcia** | Ex-líder comunitária | Proteger o bairro onde criou os filhos | Foi ela quem escreveu a carta |
| **Marcos** | Mecânico ranzinza | Manter a cabeça baixa e a casa | A ex-esposa, **Renata**, é advogada da Maré Alta; ele sabe mais do que diz |
| **Tião** | Morador da praça | Sobreviver; ser visto como gente | Era arquiteto da prefeitura; assinou o laudo verdadeiro (que sumiu) |
| **Sandra** | Gerente do Brasa | Salvar a lanchonete endividada | Recebe uma oferta generosa da Maré Alta para vender o ponto |
| **Seu Antônio** | Dono do mercadinho | Aposentar-se em paz | Já vendeu o imóvel em segredo, com cláusula de silêncio |
| **Beto** | Penhorista | Lucro | Comprou objetos do Ivo depois do desaparecimento — inclusive uma chave de cofre |
| **Rafael** | Dono da oficina | Expandir o negócio | Guinchou o carro do Ivo na noite do desaparecimento |
| **Júlia** | Estudante de enfermagem | Formar-se | Estagia no hospital onde o Ivo deu entrada sem documento |
| **Vítor Maranhão** | Diretor da Maré Alta | Fechar o empreendimento antes da eleição | Financia a campanha do vereador **Otávio Lins** |
| **Inspetora Célia Prado** | Polícia Civil | Fazer o caso andar apesar das pressões | Investiga a Maré Alta há anos sem provas |
| **Ivo Salles** | O desaparecido | Expor o esquema | Está vivo, internado como indigente após um "acidente" |

## Estrutura

### Ato 1 — Chegada (≈ 1h–1h30)
Objetivo de design: ensinar a vida no bairro e plantar o mistério sem pressa.

1. **"Casa Nova"** — bilhete do senhorio; arrumar a casa; conhecer três vizinhos (qualquer três).
   *Sistemas:* necessidades, inventário, diálogos. *Desbloqueia:* a carta (no 2º dia, ao dormir).
2. **"Carta Errada"** — ler a carta; perguntar sobre Ivo Salles. Cada NPC conta uma versão
   (Antônio: "foi embora devendo"; Beto: "comprei umas coisas dele"; Lúcia desconversa).
   *Requisito:* ter lido a carta. *Conclusão:* falar com 4 pessoas sobre Ivo ou achar o caderno de
   endereços no porão da casa 8.
3. **"Vistoria"** — fiscais da prefeitura marcam casas com tinta: "área de risco". Escolha:
   deixar marcar, discutir (teste social) ou fotografar a dupla (celular).
   *Consequência:* a foto vira prova no ato 3; discutir muda a reputação com a prefeitura.

### Ato 2 — Rumores (≈ 2h)
4. **"O Cofre do Sebo"** — Beto tem a chave de um cofre de aluguel. Comprar (R$ 300 — exige ter
   trabalhado), trocar por um favor sujo (receptação) ou furtar da loja.
5. **"Guincho às Duas"** — Rafael guinchou o carro do Ivo. Convencê-lo a mostrar a ficha
   (relação ≥ 20 ou consertar a van dele — habilidade de mecânica, Fase 4).
6. **"Ferimentos Sem Nome"** — Júlia lembra de um paciente sem documento. Entrar no hospital
   (horário de visita, disfarce de entregador ou acompanhando Júlia se a relação ≥ 30).
7. **"A Oferta"** (paralela) — Sandra recebe proposta da Maré Alta; pede sua opinião. A escolha
   decide se o Brasa fecha no ato 4 (você perde o emprego) ou vira ponto de resistência.

*Fim do ato:* você encontra Ivo vivo, sem memória recente, e um bilhete de Lúcia no bolso dele.

### Ato 3 — A Rede (≈ 2h–2h30)
O jogador escolhe um caminho principal (pode mudar uma vez):

- **Comunidade (legal):** organizar uma assembleia na praça, coletar assinaturas porta a porta
  (reputação), levar provas à Inspetora Prado e a um jornal. Missões: *"Assembleia"*,
  *"Porta a Porta"*, *"Pauta"*.
- **Acordo (dinheiro):** trabalhar para Vítor Maranhão convencendo vizinhos a vender, com comissão.
  Missões: *"Corretor"*, *"Sem Volta"*, *"Lista Negra"* (entregar quem resiste).
- **Sombra (crime):** chantagem e sabotagem com Beto: invadir o escritório da obra, apagar câmeras,
  vender os cadernos ao maior lance. Missões: *"Chave Mestra"*, *"Canteiro"*, *"Leilão"*.

Eventos sistêmicos do ato: incêndio criminoso numa casa vazia (se a reputação da vila estiver
baixa, os vizinhos suspeitam de você), visita do ex-sócio cobrando a dívida, Marcos revela Renata.

### Ato 4 — Ruptura (≈ 1h30)
15. **"Noite de Despejo"** — a prefeitura tenta lacrar casas. Resolução conforme o caminho:
    resistência pacífica, ajudar a despejar ou provocar um caos que vire notícia.
16. **"O Laudo de Tião"** — recuperar o laudo verdadeiro no arquivo municipal (furtivo, social ou
    com ajuda da Inspetora).
17. **"Quem Paga"** — Vítor oferece um valor alto pelo seu silêncio, proporcional ao seu dinheiro
    acumulado (o jogo lembra se você ficou rico).

### Ato 5 — Desfecho (≈ 1h)
18. **"Audiência Pública"** — cena final na câmara (diálogos com enquadramento): as provas que você
    reuniu, as pessoas que você ajudou e quem lhe deve favores definem o resultado.

## Desfechos

| Desfecho | Condição | Resultado observável no mundo |
|---|---|---|
| **A Aurora Fica** | Caminho Comunidade + laudo + ≥ 6 aliados | Obra cancelada; praça reformada; Lúcia vira síndica; aluguel congelado; Tião volta a trabalhar |
| **Vendida, mas Justa** | Comunidade sem todas as provas, ou Acordo com consciência (protegeu Lúcia e Tião) | Metade do bairro vira condomínio; indenizações justas; alguns NPCs se mudam (somem do mapa) |
| **Maré Alta** | Caminho Acordo completo | Você fica rico e recebe um apartamento na orla; o bairro muda: novos NPCs, preços altos, antigos vizinhos hostis |
| **Dono da Rua** | Caminho Sombra completo | Vítor preso, mas você controla o mercado paralelo da vila; reputação baixa, polícia de olho, novas atividades ilegais |

O jogo continua depois de qualquer desfecho: o mundo muda, a vida segue.

## Diálogos essenciais (amostras)

**Lúcia, ao ser confrontada sobre a carta (ato 2):**
> — Fui eu, sim. Achei que o Ivo ainda morava aí... Quando vi você chegando com aquelas caixas,
> pensei: mais um que vai embora no primeiro aperto. Me desculpa.
> **[Social 3]** "A senhora sabe onde ele está." → ela revela o hospital
> **[Mentir]** "Eu já sei de tudo." → teste; falhar fecha a pista da Júlia
> "Não quero me envolver." → ato 2 continua só pela trilha do Beto

**Vítor Maranhão (ato 4):**
> — Você entende de números, já vi o seu extrato. Ninguém aqui vai lembrar do seu nome daqui a
> dez anos. Mas o seu saldo, esse lembra.
> "Quanto?" → oferta escala com o dinheiro do jogador
> **[Gravar no celular]** → prova extra (exige celular no inventário)
> "A Aurora não está à venda." → fecha o caminho Acordo de vez

**Tião (ato 4, se a relação ≥ 40):**
> — Eu assinei o laudo certo, sabia? Solo firme, nenhuma rachadura. Rasgaram na minha frente.
> Depois me rasgaram também. Se for lá buscar, vai de dia. De noite tem cachorro.

## Requisitos de progressão

- Ato 2 abre no 3º dia de jogo **e** após "Carta Errada".
- Ato 3 exige ter encontrado Ivo **ou** reunido 2 de 3 pistas (cofre, guincho, hospital).
- Ato 4 começa 4 dias após a escolha do caminho (o mundo avisa: cartazes, fofoca, fiscais).
- Missões principais nunca exigem crime: todo objetivo tem uma rota legal, ainda que mais lenta ou
  mais cara (trabalhar, convencer, pagar).
- Missões secundárias continuam disponíveis durante toda a história.

## Ganchos de missões secundárias (expansão)

- *"Caixa Dois"* — Antônio pede ajuda com o estoque antes de fechar (trabalho temporário).
- *"Corrida da Praça"* — Júlia organiza uma corrida (condicionamento).
- *"Peça Rara"* — Rafael procura uma peça vendida pelo Beto (negociação).
- *"Lanche Fiado"* — Kleber dá comida fiado a Tião escondido da Sandra (escolha moral).
- Eventos dinâmicos já suportados pelo motor de regras: acidentes de trânsito, discussões em
  público, pedidos de ajuda condicionados a horário, local e reputação.
