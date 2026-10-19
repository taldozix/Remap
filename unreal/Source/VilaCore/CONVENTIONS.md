# VilaCore — convenções do port GDScript → C++

O núcleo é C++17 puro (sem Unreal), em `namespace vila`. Cada arquivo GDScript de
`src/systems/` (raiz do repositório) vira `Public/Vila/<Classe>.h` + `Private/<Classe>.cpp`.

## Regra principal: port fiel, linha a linha
- Mesmos nomes de classe, métodos, campos e constantes do GDScript (snake_case).
  Métodos "privados" (`_algo`) viram `private:` com o mesmo nome sem o `_` inicial
  e com sufixo nenhum (ex.: `_changed()` → `changed_()` é proibido; use `on_changed()`
  só se colidir). Preferência: manter o nome sem o sublinhado.
- Mesma lógica, mesmas mensagens (strings em português passadas por `tr()`), mesmos
  valores padrão. Não "melhorar" regras de jogo: os testes comparam o comportamento.

## Tipos
| GDScript | C++ |
|---|---|
| `Dictionary`, `Array` (dinâmicos, vindos/indo para JSON) | `Json` (nlohmann::ordered_json) |
| `String` | `std::string` |
| `int` | `int` |
| `float` | `double` |
| `bool` | `bool` |
| `Vector3` | `Vec3` |
| `RandomNumberGenerator` | `Rng&` (parâmetro) / `Rng` (campo) |
| `Callable` (predicado) | `std::function<bool(const Json&)>` |
| `Inventory` (pode ser null) | `Inventory*` ; não-null → `Inventory&` |
| `PlayerState`, `Relations`, `Needs` | `PlayerState&`, `Relations&`, `Needs&` |

- Retorno `Dictionary`/`Array` → `Json`. Parâmetro `Dictionary`/`Array` → `const Json&`.
- Campos `Dictionary` → `Json x = Json::object();`, `Array` → `Json x = Json::array();`.
- Pilhas de inventário continuam Json: `{"id": ..., "qty": ..., "age": ...}`.

## Acesso a Json (Core.h)
- `d.get("k", padrão)` → `jstr/jnum/jint/jbool(d, "k", padrão)`; subobjeto `jobj(d,"k")`,
  array `jarr(d,"k")`; existência `has(d,"k")`; valor bruto `jget(d,"k")` (null se faltar).
- `str(x)` → `to_str(x)`, `float(x)` → `to_num(x)`, `int(x)` → `to_int(x)`, `bool(x)`/if → `truthy(x)`.
- `"%s %d" % [a, b]` → `fmt("%s %d", {a, b})`. `tr("...")` → `tr("...")`.
- `push_warning` → `push_warning`.
- Iterar dicionário: `for (const auto& [k, v] : d.items())`.

## Singletons (autoloads)
- `Data` → `data()` (Vila/Data.h): `data().item(id)`, `data().map()`...
- `Clock` → `clock()` (Vila/Clock.h)
- `Events` → `events()` (Vila/Events.h): `events().notify.emit(texto, "good")`.
  Sinais que levavam `Node` levam o id (`std::string`).
- `Game` → `game()` (Vila/Game.h): `game().player`, `game().relations`, `game().world_state`...
  `Game.world.call(...)` → `game().world` é `IWorld*` (Vila/World.h).
- Nos headers dos sistemas, NÃO inclua Game.h (dependência circular): use forward
  declarations e inclua Game.h só no .cpp.

## Headers existentes (não alterar a interface sem necessidade)
Core.h, Data.h, Clock.h, Events.h, World.h, Game.h, Saves.h, Settings.h.

## Compilar e testar
```
cmake -S unreal/Source/VilaCore/Tests -B /tmp/vilabuild && cmake --build /tmp/vilabuild -j4
/tmp/vilabuild/vila_tests unreal/Content/Data
```
Para checar só um arquivo: `g++ -std=c++17 -fsyntax-only -Iunreal/Source/VilaCore/Public unreal/Source/VilaCore/Private/X.cpp`.
