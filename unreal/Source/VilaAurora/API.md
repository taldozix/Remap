# VilaAurora (módulo de jogo Unreal): contrato entre as partes

Este módulo é o port da camada de jogo do Godot (`src/actors`, `src/world`,
`src/vehicles`, `src/ui`, `src/audio`, `src/util`). A simulação continua no núcleo C++
**VilaCore** (`unreal/Source/VilaCore`, namespace `vila`), compilado e testado fora do
Unreal: 283 testes, portados de `tests/TestRunner.gd`. Este módulo **não** reimplementa
regras de jogo. Ele chama `vila::game()`, `vila::clock()`, `vila::events()`,
`vila::data()`, `vila::settings()` e `vila::saves()`, como o Godot chamava os autoloads
`Game`, `Clock`, `Events`, `Data`, `Settings` e `Saves`.

**Versão-alvo:** Unreal Engine 5.5 (deve compilar também na 5.6). A UI é feita só em C++
e Slate, sem Blueprints. Os únicos `.uasset` vêm do `Scripts/build_content.py`. Sem esse
conteúdo, tudo precisa funcionar com as formas básicas do motor (`UVilaAssets` já faz o
fallback).

## Regras para todo o código

1. **API pública do UE 5.5.** Inclua todos os cabeçalhos que usar (IWYU) e não conte com
   `CoreMinimal` para trazer mais nada. Use `UPROPERTY()` + `TObjectPtr<>` para todo
   ponteiro de `UObject` guardado em membro. Em `TArray`/`TMap`, use `UPROPERTY()` ou
   `TWeakObjectPtr`. Nada de `ConstructorHelpers` para assets do `/Game`: carregue em
   runtime via `UVilaAssets` (o conteúdo pode não existir).
2. **Coordenadas:** sempre via `VilaConv.h`. O estado do núcleo e o JSON usam metros, Y para
   cima e mapas `[x, z]`. O Unreal usa cm, Z para cima e X para frente. Use `ToUE` /
   `FromUE` / `JsonPos` / `YawFromGodot`, e nunca converta à mão.
3. **Texto visível:** passe por `VilaConv::Tr("texto em português")`, que equivale ao
   `tr()` do Godot e consulta `data/i18n/en.json`. As chaves são exatamente as strings
   portuguesas do GDScript original.
4. **Strings:** `VilaConv::S(std::string)` → `FString` e `VilaConv::U(FString)` →
   `std::string`, ambas em UTF-8.
5. **Sinais do núcleo** (`vila::events().X.connect(...)`): guarde o id devolvido e chame
   `disconnect(id)` em `EndPlay`. Os sinais são globais e sobrevivem à troca de nível.
6. **Port fiel:** cada classe corresponde a um arquivo `.gd`. Mantenha o comportamento,
   os números (tempos, distâncias, preços) e os textos. Converta metros para cm só na
   fronteira.
7. **Melhorias do Unreal** são bem-vindas: Lumen, sombras virtuais, Nanite onde couber,
   física Chaos para colisões simples, navmesh do Recast, Enhanced Input, Slate. Elas não
   podem mudar regras de jogo.
8. **Proibido:** ativos de GTA, Red Dead Redemption ou Project Zomboid. Nada de
   download de conteúdo.
9. **Log:** use `UE_LOG(LogVila, ...)`, declarado em `VilaAurora.h`.
10. **Comentários:** em português, no estilo dos arquivos existentes.
11. **Compilação:** este módulo **não é compilado neste ambiente** (não há Unreal aqui).
    Escreva com cuidado redobrado, revise cada assinatura da API do motor que usar e
    prefira APIs estáveis e conhecidas.

## Arquivos compartilhados (já escritos; não altere sem necessidade)

| Arquivo | Conteúdo |
|---|---|
| `VilaAurora.h` | `LogVila` |
| `VilaConv.h` | conversões de espaço, texto e cor |
| `VilaAssets.h/.cpp` | `UVilaAssets` (`UGameInstanceSubsystem`): malhas básicas (`Cube()` etc.: pivô no centro, 100 cm), `Surface(key, tint, bInstanceTint)`, `SurfaceLocal`, `Color`, `Emissive`, `Glass`, `Master(name)`, `MakeMID`, `MakeColorMID`, `SetGlobal(ctx, "Wetness"/"Night"/"Wind", v)`, `VehiclePart(model, part)`, `VehicleLayout()`, `CharacterPiece(sex, piece)`, `CharacterAnim(sex, anim)`, `CharacterSkeleton(sex)`, `DataDir()` |
| `VilaInteractable.h` | `FVilaAction` + `IVilaInteractable` (`GetInteractPoint`, `GetInteractTitle`, `GetInteractRadius`, `GetFocusPriority`, `GetActions(Player)`, `DoAction(Player, Id)`, `GetBuildingId`, `SetFocused`) |
| `VilaGameInstance.h/.cpp` | Inicializa o núcleo, aplica as opções e cuida do fluxo `StartNewGame` / `LoadSlot` / `ReturnToMenu` / `QuitGame`. `GetFlow()`: `EVilaFlow::Menu` ou `Playing` |
| `VilaGameMode.h/.cpp` | Cria o `AVilaWorld`. `PlayerController` = `AVilaPlayerController`, `HUD` = `AVilaHUD`, sem pawn padrão |

## Partes e donos

Cada parte escreve só os seus arquivos (`Public/X.h` e `Private/X.cpp`). As assinaturas
listadas são **obrigatórias e exatas**: outras partes as chamam. Cada parte pode acrescentar
o que precisar.

### W1: construção do mundo

Arquivos: `VilaWorldBuilder`, `VilaBuilding`, `VilaMeshBatch`, `VilaGrass`, `VilaNature`,
`VilaProcMesh`.
Porta de: `WorldBuilder.gd`, `Building.gd`, `GrassField.gd`, `Nature.gd`,
`MeshBatcher.gd`, `ChunkBatcher.gd`, `FoliageBatcher.gd` e as partes de geometria de
`Mat.gd`.

```cpp
// VilaWorldBuilder.h
USTRUCT() struct FVilaSpot {
  GENERATED_BODY()
  UPROPERTY() FString Id;
  UPROPERTY() FVector Location = FVector::ZeroVector;  // cm, no chão
  UPROPERTY() float Yaw = 0.f;                          // graus UE (já convertido)
  UPROPERTY() FString Pose;                             // "stand", "sit", "lie", "work"... (como no Godot)
  UPROPERTY() bool bHidden = false;
  UPROPERTY() FString BuildingId;
  UPROPERTY() TWeakObjectPtr<AVilaFurniture> Furniture; // móvel associado, se houver
};
UCLASS() class VILAAURORA_API UVilaWorldBuilder : public UObject {
public:
  // Monta toda a geometria da vila como componentes do World (ou de atores AVilaBuilding),
  // gera colisão e chama UNavigationSystemV1::Build/dirty no fim.
  void Build(AVilaWorld* World, const vila::Json& Map);
  float GroundHeight(float X, float Y) const;   // cm (UE). O chão é plano (0) fora de rampas.
  UPROPERTY() TMap<FString, FVilaSpot> Spots;
  UPROPERTY() TMap<FString, TObjectPtr<AVilaBuilding>> Buildings;
  UPROPERTY() TMap<FString, TObjectPtr<AVilaDoor>> Doors;
  UPROPERTY() TMap<FString, TObjectPtr<AVilaFurniture>> Furniture;  // id do móvel e id do contêiner
  UPROPERTY() TArray<TObjectPtr<ULocalLightComponent>> Streetlights;  // DayNight liga/desliga
  TArray<FBox2D> SidewalkRects;                 // cm (UE XY), usados por NPCs e grama
};
// VilaBuilding.h — um ator por prédio, com os componentes de malha dele.
UCLASS() class VILAAURORA_API AVilaBuilding : public AActor {
public:
  void Setup(const vila::Json& Def);  // o builder chama antes de adicionar a geometria
  FString BuildingId;
  vila::Json Def;
  bool Contains(const FVector& P, float MarginCm = 0.f) const;  // dentro do retângulo do prédio
  void SetCut(bool bOn, const FVector& ViewDir);  // corte: esconde telhado e paredes de frente à câmera
  void SetNight(float Amount);                   // janelas acesas e luzes internas (0..1)
};
```

O builder **cria** portas e móveis (`SpawnActor`) e depois chama o `Setup` deles, que é da
parte W2:

- **Porta:** o ator fica no vão, origem embaixo e no centro da abertura, com +X apontando
  para fora do prédio (a normal da parede).
- **Móvel:** origem no pé do móvel. O yaw vem do `"rot"` do JSON (`YawFromGodot`), e o
  visual do móvel é montado **pelo builder** (como no Godot).

### W2: o mundo de jogo

Arquivos: `VilaWorld`, `VilaDoor`, `VilaFurniture`, `VilaGroundItem`, `VilaDeliveryDrop`.
Porta de: `GameWorld.gd`, `Door.gd`, `Furniture.gd`, `GroundItem.gd`, `DeliveryDrop.gd`,
`Interactable.gd`.

```cpp
// VilaWorld.h — implementa vila::IWorld (todos os métodos de World.h).
UCLASS() class VILAAURORA_API AVilaWorld : public AActor, public vila::IWorld {
public:
  static AVilaWorld* Get(const UObject* WorldContext);  // o único da partida (ou nullptr)
  // estado
  UPROPERTY() TObjectPtr<UVilaWorldBuilder> Builder;
  UPROPERTY() TObjectPtr<AVilaPlayerCharacter> Player;  // nullptr no menu
  UPROPERTY() TObjectPtr<AVilaCameraRig> CameraRig;
  UPROPERTY() TObjectPtr<AVilaTrafficSystem> Traffic;
  UPROPERTY() TObjectPtr<AVilaEnvironment> Environment;
  UPROPERTY() TMap<FString, TObjectPtr<AVilaNPC>> NPCs;
  UPROPERTY() TMap<FString, TObjectPtr<AVilaVehicle>> Vehicles;
  FString PlayerBuilding;          // prédio onde o jogador está ("" = rua)
  bool bReady = false;
  // consultas (port de GameWorld.gd)
  bool UIBlocking() const;                         // HUD com painel modal aberto
  float CameraYaw() const;
  float GroundHeight(float X, float Y) const;
  bool IsInsideBuilding(const FVector& P, const FString& BuildingId) const;
  FString BuildingAt(const FVector& P) const;
  TArray<AVilaNPC*> NPCsNear(const FVector& P, float RadiusCm) const;
  TArray<FVector> FindPath(const FVector& From, const FVector& To) const;  // navmesh; reta se falhar
  float NPCLodDistance() const;                    // cm
  AVilaNPC* ChatPartner(AVilaNPC* Npc) const;
  vila::Json ConversationLines() const;
  FString ShopUnavailableReason(const FString& ShopId) const;  // "" = pode comprar
  AVilaFurniture* CounterForStaff(const FString& SpotId) const;
  AVilaNPC* StaffAt(const FString& SpotId) const;
  AVilaVehicle* PlayerVehicleNear(const FVector& P, float RadiusCm) const;
  AVilaVehicle* NearestVehicle(const FVector& P, float RadiusCm) const;
  const FVilaSpot* Spot(const FString& Id) const;
  TArray<FVector> MarkerPositions() const;         // marcadores de missão (HUD/mapa)
  // ações (port de GameWorld.gd)
  void DropItem(int32 Index, int32 Qty);           // do inventário do jogador para o chão
  void PickGroundItem(AVilaGroundItem* Item);
  void StartDialogue(AVilaNPC* Npc);
  void DeliverDriveOrder();
  void ShowShiftSummary(const vila::Json& Summary);
  void DoSleep(int32 Hours, const FString& BedId);
  void OnNPCHit(AVilaNPC* Npc, float Severity);
  void OnNPCHelped(AVilaNPC* Npc);
  void OnHorn(const FVector& P);
  void OnKnock(const FString& BuildingId);
  void OnPlayerUsedKey(const FString& BuildingId);
  void OnContainerTake(const FString& ContainerId);
  void RefreshDelivery();
  TArray<FVilaAction> ExtraNPCActions(AVilaNPC* Npc);           // ações de eventos sobre NPCs
  bool DoExtraNPCAction(AVilaNPC* Npc, const FString& ActionId);
  // vila::IWorld: player_outdoors, is_player_in_job_area, on_player_fainted, on_shift_ended,
  // find_witnesses (traço de linha de visão), is_vehicle_near_spot, refresh_vehicle,
  // sync_to_state, save_block_reason, start_event, end_event.
};
// VilaDoor.h
UCLASS() class VILAAURORA_API AVilaDoor : public AActor, public IVilaInteractable {
public:
  void Setup(const vila::Json& Def, AVilaWorld* World, const FString& BuildingId, float WallThicknessCm, const FLinearColor& Color);
  FString DoorId;
  bool IsLocked() const;
  bool IsOpen() const;
  void SetOpen(bool bOpen, bool bInstant = false);
  bool PlayerHasKey() const;
  bool NpcHasKey(const FString& NpcId) const;
};
// VilaFurniture.h
UCLASS() class VILAAURORA_API AVilaFurniture : public AActor, public IVilaInteractable {
public:
  void Setup(const vila::Json& Def, AVilaWorld* World, const FString& BuildingId);
  FString Kind, ItemId, OwnerId, ContainerId;
  vila::Json Def;
  float SeatHeightCm = 45.f;
  FVector Forward() const;
  FTransform SeatTransform() const;
  // O builder pendura aqui os componentes visuais do móvel (para portas de armário/geladeira etc.).
  USceneComponent* GetVisualRoot() const;
};
// VilaGroundItem.h:   void Setup(const vila::Json& Entry, AVilaWorld* World);  vila::Json Entry;
// VilaDeliveryDrop.h: void Setup(AVilaWorld* World, const FString& Label);
```

O ciclo do `AVilaWorld` em `BeginPlay` segue o `GameWorld._ready()`:

1. `vila::game().world = this`.
2. Monta o mundo (`Builder->Build`), cria o `AVilaEnvironment`, a grama, os itens no chão e os
   veículos.
3. No fluxo `Playing`, cria o jogador e a câmera e faz o `PlayerController` possuir o
   jogador.
4. Espera a navmesh e só então cria os NPCs e o trânsito.
5. Põe `vila::clock().running = true` e emite `world_ready`.
6. Trata o `pending_load` (mensagem de boas-vindas ou "Jogo carregado.").

O `Tick` chama `vila::clock().process(DeltaSeconds)` e roda o resto do `_process` do
Godot. No fluxo `Menu`, o mundo é só cenário: não cria jogador, a câmera orbita a praça e
o relógio corre acelerado, sem simulação (`vila::game().world` continua `nullptr`).

O `EndPlay` faz `vila::game().world = nullptr` e desconecta os sinais.

### A: atores e controle

Arquivos: `VilaPlayerCharacter`, `VilaNPC`, `VilaHumanoid`, `VilaAnimInstance`,
`VilaCameraRig`, `VilaPlayerController`.
Porta de: `Player.gd`, `NPC.gd`, `Humanoid.gd`, `CameraRig.gd`, mais o input
(`Settings` → Enhanced Input).

```cpp
// VilaPlayerCharacter.h
UCLASS() class VILAAURORA_API AVilaPlayerCharacter : public ACharacter {
public:
  // estado do jogador, como no Godot:
  //   "free", "driving", "sitting", "lying", "sleeping", "rest", "bed"
  FString State = TEXT("free");
  UPROPERTY() TObjectPtr<AVilaVehicle> Vehicle;      // veículo atual, se dirigindo
  UPROPERTY() TObjectPtr<UVilaHumanoid> Humanoid;
  void RebuildVisual();                              // aparência/roupas do PlayerState
  void CycleTarget();
  AActor* GetFocus() const;                          // alvo IVilaInteractable atual
  TArray<FVilaAction> GetFocusActions() const;       // ações do alvo (para o HUD)
  void Perform(AActor* Target, const FString& ActionId);
  // Ação com tempo (barra de progresso no HUD). Callback ao concluir; interrompida se o jogador se mover.
  void StartTimed(const FString& Label, float Seconds, TFunction<void()> OnDone, const FString& Sound = FString());
  bool IsTimedActive() const;
  float TimedProgress() const;                       // 0..1
  FString TimedLabel() const;
  void SitOn(AVilaFurniture* F);
  void RestOnBed(AVilaFurniture* F);
  void StandUp();
  void BeginSleep();
  void EndSleep();
  void EnterVehicle(AVilaVehicle* V);
  void ExitVehicle();
  void SyncToState();                                // posição/rotação/veículo -> vila::game().player
};
// VilaNPC.h
UCLASS() class VILAAURORA_API AVilaNPC : public ACharacter, public IVilaInteractable {
public:
  void Setup(const FString& NpcId, const vila::Json& Def, AVilaWorld* World);
  FString NpcId;
  vila::Json Def;
  FString DisplayName() const;
  bool IsSleeping() const;
  bool CanWitness() const;
  void BeginTalk();
  void EndTalk();
  void Bark(const FString& Text);                    // balão de fala (o HUD desenha)
  void BarkLater(const FString& Text, float Delay);
  FString CurrentBark() const;                       // "" sem balão
  void GoTo(const FString& SpotId);
  void KnockDown(const FVector& FromDir, float Severity, bool bByPlayer, float Duration = -1.f);
  void HelpUp();
  void OnBumped(AActor* By);
  bool HasDoorAccess(const AVilaDoor* Door) const;
  FString HomeSpot() const;
  FString BuildingAtSpot() const;
};
// VilaHumanoid.h — personagem em peças: SK_Body líder + roupas/cabelo via SetLeaderPoseComponent;
// morphs heavy/thin/old/face_a/face_b; esconde regiões da pele pelo parâmetro HideBits do M_Skin.
// Fallback sem conteúdo: boneco de cápsulas/cubos animado proceduralmente.
UCLASS() class VILAAURORA_API UVilaHumanoid : public USceneComponent {
public:
  void Build(const vila::Json& Appearance);          // mesmo formato do Godot (visual_appearance)
  void SetPose(const FString& Pose);                 // "idle","walk","run","talk","work","eat","search","sit","lie","knocked"
  void SetMoveSpeed(float CmPerSec);                 // escolhe idle/walk/run e a velocidade da animação
};
// VilaCameraRig.h — câmera isométrica que segue o jogador ou o veículo (CameraRig.gd), com
// SpringArm + CineCamera. A profundidade de campo de miniatura vem de settings.tilt_shift.
UCLASS() class VILAAURORA_API AVilaCameraRig : public AActor {
public:
  UPROPERTY() TObjectPtr<AActor> Target;
  void Snap();
  FVector ViewDir() const;      // direção câmera -> alvo, no plano XY, normalizada
  float MoveYaw() const;        // yaw (graus UE) usado para "cima" no input de movimento
  UCameraComponent* GetCamera() const;
  void SetMenuOrbit(const FVector& Center, float RadiusCm);  // modo menu principal
};
// VilaPlayerController.h — cria UInputAction/UInputMappingContext em runtime a partir de
// vila::settings().bindings (os nomes de tecla do JSON são nomes de FKey) e os recria em
// Settings::changed("input"). Roteia para o jogador, para o veículo ou para o HUD.
UCLASS() class VILAAURORA_API AVilaPlayerController : public APlayerController {
public:
  // Para a UI: suspende o controle do jogo quando há painel modal (mostra o cursor).
  void SetUIMode(bool bModal);
  bool IsActionDown(const FString& Action) const;   // "run", "handbrake"...
  FVector2D GetMoveAxis() const;                    // move_left/right/up/down combinados (-1..1)
};
```

### V: veículos e trânsito

Arquivos: `VilaVehicle`, `VilaCarVisual`, `VilaTrafficSystem`, `VilaTrafficCar`.
Porta de: `Vehicle.gd`, `CarVisual.gd`, `TrafficSystem.gd`, `TrafficCar.gd`.

A física é a mesma direção arcade do Godot: aceleração, freio, ré, freio de mão,
combustível, dano e colisões com atropelamento. Ela é cinemática, com sweep (`SetActorLocation`
com `bSweep`) ou com `UPawnMovementComponent` próprio. Não use o Chaos Vehicle, que pede
assets de física.

```cpp
// VilaVehicle.h
UCLASS() class VILAAURORA_API AVilaVehicle : public APawn, public IVilaInteractable {
public:
  void Setup(const FString& VehicleId, AVilaWorld* World);   // estado em vila::game().world_state.vehicles[id]
  FString VehicleId;
  float Speed = 0.f;                    // cm/s, com sinal (ré negativa)
  UPROPERTY() TObjectPtr<AVilaPlayerCharacter> Driver;
  FString DisplayName() const;
  float FuelCapacity() const;
  FString OwnerId() const;
  bool IsPlayers() const;
  void SetDriver(AVilaPlayerCharacter* D);   // nullptr = desce
  FVector ExitPosition() const;
  TArray<FVilaAction> GetDriverActions(AVilaPlayerCharacter* Player);
  void SetDriveInput(float Throttle, float Steer, bool bHandbrake);  // o controller chama a cada frame
  void Horn();
  void SaveState();
  void RefreshFromState();
};
// VilaTrafficSystem.h
UCLASS() class VILAAURORA_API AVilaTrafficSystem : public AActor {
public:
  void Setup(AVilaWorld* World, const vila::Json& Map);
  UPROPERTY() TArray<TObjectPtr<AVilaTrafficCar>> Cars;
  FString LightState(const FString& Axis) const;   // "green", "yellow" ou "red"
  bool NearNode(const FVector& P, float RadiusCm) const;
  AVilaTrafficCar* SpawnCustomer();                 // cliente do drive-thru
  void RemoveCar(AVilaTrafficCar* Car);
};
// VilaTrafficCar.h — AActor cinemático; void OnHitByPlayer(float Impact);  FString CustomerId;
```

### U: interface (Slate)

Arquivos: `VilaHUD` e `UI/*` (`SVilaHUDOverlay`, `SVilaMainMenu`, `SVilaPauseMenu`,
`SVilaOptionsMenu`, `SVilaSaveSlots`, `SVilaInventoryPanel`, `SVilaContainerPanel`,
`SVilaDialoguePanel`, `SVilaShopPanel`, `SVilaJournalPanel`, `SVilaMapPanel`,
`SVilaCharacterCreator`), mais `VilaUIStyle` (o `UITheme.gd`).
Porta de: `UIManager.gd`, `UITheme.gd`, `MainMenu.gd`, `PauseMenu.gd`, `OptionsMenu.gd`,
`SaveSlots.gd`, `InventoryPanel.gd`, `ContainerPanel.gd`, `DialoguePanel.gd`,
`ShopPanel.gd`, `JournalPanel.gd`, `MapPanel.gd`, `MarkerLayer.gd`.

```cpp
// VilaHUD.h — AHUD que adiciona o overlay Slate ao viewport (GEngine->GameViewport->AddViewportWidgetContent).
UCLASS() class VILAAURORA_API AVilaHUD : public AHUD {
public:
  static AVilaHUD* Get(const UObject* WorldContext);
  bool IsModalOpen() const;
  bool IsDialogueOpen() const;
  void OpenContainer(const FString& ContainerId, const FString& Title);
  void OpenShop(const FString& ShopId);
  void OpenDialogue(AVilaNPC* Npc);
  void OpenPause();
  void OpenSleep(const FString& BedId, float Quality);
  void OpenPanel(const FString& Kind);     // "inventory", "journal", "map"
  void ShowMessage(const FString& Title, const FString& Body);
  void Fade(bool bToBlack);                // fade de 0,4 s (desmaio, sono)
  void CloseCurrent();
  void CloseAll();
  void ShowMainMenu();                     // fluxo Menu
};
```

O HUD escuta `vila::events()`: `notify`, `request_dialogue`, `request_container`,
`request_shop`, `request_message`, `request_sleep` e outros.

Ele desenha:

- necessidades e moodles;
- relógio, dia e clima;
- dinheiro;
- notificações;
- o prompt de interação com as ações do foco (`Player->GetFocusActions()`, teclas
  `option_1..4`);
- a barra de ação temporizada;
- balões de fala dos NPCs (projetados com `ProjectWorldLocationToScreen`);
- marcadores de missão (`AVilaWorld::MarkerPositions`);
- o painel de diagnóstico (F3);
- velocímetro e combustível ao dirigir.

O `AVilaPlayerController` encaminha as teclas de pausa, inventário, diário e mapa para o
HUD.

### E: ambiente e áudio

Arquivos: `VilaEnvironment`, `VilaWeather`, `VilaAudio`.
Porta de: `DayNight.gd`, `PostFX.gd`, `Audio.gd` e o clima (chuva).

```cpp
// VilaEnvironment.h — sol (ADirectionalLight) + lua, SkyAtmosphere, VolumetricCloud,
// ExponentialHeightFog (névoa volumétrica), SkyLight em captura em tempo real e PostProcess
// global (exposição, grading por hora do dia, DOF de miniatura, bloom). Respeita a regra do
// usuário: entre 9h e 15h a luz é neutra, nunca amarelada.
UCLASS() class VILAAURORA_API AVilaEnvironment : public AActor {
public:
  void Setup(AVilaWorld* World);
  void ApplySettings();
  void SetMood(const FLinearColor& Highlight, const FLinearColor& Shadow, float Saturation);  // PostFX.set_mood
  float NightAmount() const;            // 0 dia … 1 noite
  void SetMenuMode(bool bMenu);
};
// VilaAudio.h — UGameInstanceSubsystem. Usa o arquivo /Game/Vila/Audio/<id> se existir; senão
// sintetiza o som proceduralmente (port de Audio._synth) num USoundWaveProcedural.
UCLASS() class VILAAURORA_API UVilaAudio : public UGameInstanceSubsystem {
public:
  static UVilaAudio* Get(const UObject* WorldContext);
  void Play(const FString& Id, float VolumeDb = 0.f, float Pitch = 1.f);
  void PlayAt(const FString& Id, const FVector& Location, float VolumeDb = 0.f, float Pitch = 1.f);
  void PlayMusic(const FString& Id);
  void StopMusic();
  void SetAmbient(const FString& Id, bool bOn, float VolumeDb = -8.f);
  void StopAllAmbient();
  void ApplyVolumes();                 // vila::settings() audio.master/music/sfx/voice/ambient
  // Motor contínuo de um veículo (tom segue a rotação). Devolve o componente para o veículo controlar.
  UAudioComponent* CreateEngineLoop(AActor* Owner);
};
```

Ids de som usados pelo jogo:

- `bad`, `cash`, `city`, `crash`;
- `door_close`, `door_locked`, `door_open`;
- `drink`, `drop`, `eat`, `engine`, `horn`;
- `music_menu`, `notify`, `order`, `pickup`;
- `rain`, `sizzle`, `step`, `ui_click`.

### C: conteúdo

Arquivos: `tools/art/blender/export_unreal.py` e `unreal/Scripts/build_content.py`.
Ver o cabeçalho de `VilaAssets.h`: caminhos, nomes de materiais e parâmetros.

## Ordem de chamada entre as partes (resumo)

- **Mundo e construção:**
  - `AVilaGameMode` cria o `AVilaWorld`.
  - O `AVilaWorld` monta o `UVilaWorldBuilder` (W1). O builder cria `AVilaBuilding`, os
    `AVilaDoor` e os `AVilaFurniture` (W2).
  - O `AVilaWorld` cria o `AVilaEnvironment` (E), os `AVilaVehicle` (V), o
    `AVilaPlayerCharacter` (A), o `AVilaCameraRig` (A), os `AVilaNPC` (A) e o
    `AVilaTrafficSystem` (V).
- **Entrada:** `AVilaPlayerController` (A).
  - Se o jogador estiver a pé, chama o `AVilaPlayerCharacter`.
  - Se estiver dirigindo, chama `AVilaVehicle::SetDriveInput`.
  - Se houver um painel aberto, chama o `AVilaHUD` (U).
- **Interface:** o HUD (U) lê o núcleo e o `AVilaWorld::Get(this)` para consultas.
