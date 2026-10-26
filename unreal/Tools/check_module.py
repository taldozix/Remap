#!/usr/bin/env python3
"""Verificação estática do módulo VilaAurora (sem o Unreal disponível).

Não substitui o compilador. Pega os erros de integração mais comuns entre partes escritas
em paralelo:
  1. #include "X.h" local que não existe;
  2. cabeçalho com UCLASS/USTRUCT/UENUM/UINTERFACE sem "<Nome>.generated.h" como último include;
  3. chamada `Var->Metodo(` / `Classe::Get(..)->Metodo(` em tipos do próprio módulo
     (AVila*/UVila*/SVila*/IVila*/FVila*) cujo método não está declarado na classe nem nas
     bases do módulo — se a classe herda do motor, o método vai para uma lista "verificar à
     mão" (pode ser API do Unreal);
  4. classes do módulo usadas mas nunca declaradas;
  5. métodos declarados num cabeçalho e sem definição em nenhum .cpp (possível erro de link).
Uso: python3 unreal/Tools/check_module.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "Source"
MOD = ROOT / "VilaAurora"
CORE = ROOT / "VilaCore" / "Public"

HEADERS = sorted(MOD.rglob("*.h"))
SOURCES = sorted(MOD.rglob("*.cpp"))
OWN = re.compile(r"\b([AUSIF]Vila\w+)\b")


def strip_comments(s: str) -> str:
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    s = re.sub(r'"(?:\\.|[^"\\])*"', '""', s)
    return s


def class_bodies(text: str):
    """Devolve (nome, bases, corpo) de cada class/struct declarada."""
    out = []
    for m in re.finditer(r"\b(class|struct)\s+(?:\w+_API\s+)?(\w+)\s*(?:final\s*)?(:[^{;]*)?\{", text):
        name, bases = m.group(2), m.group(3) or ""
        i, depth = m.end(), 1
        while i < len(text) and depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        out.append((name, [b.strip().split()[-1] for b in bases.lstrip(":").split(",") if b.strip()], text[m.end():i - 1]))
    return out


def members(body: str):
    # remove corpos de funções inline aninhados para não capturar chamadas como declarações
    flat, depth = [], 0
    for ch in body:
        if ch == "{":
            depth += 1
            continue
        if ch == "}":
            depth -= 1
            flat.append(";")
            continue
        if depth == 0:
            flat.append(ch)
    flat = "".join(flat)
    names = set(re.findall(r"\b(\w+)\s*\(", flat))
    names |= set(re.findall(r"\b(\w+)\s*(?:=[^;]*)?;", flat))
    names |= set(re.findall(r"\b(\w+)\s*\[", flat))
    return names - {"UPROPERTY", "UFUNCTION", "GENERATED_BODY", "if", "for", "while", "return", "sizeof"}


classes, bases, problems, manual = {}, {}, [], []
member_types = {}
SMART_PTR = {"Get", "Pin", "Reset", "IsValid", "ToSharedRef", "ToSharedPtr", "IsStale"}
# Métodos do motor muito comuns (AActor/UActorComponent/UPrimitiveComponent...): não listar.
ENGINE_COMMON = set("""GetActorLocation SetActorLocation GetActorRotation SetActorRotation GetActorTransform SetActorTransform
GetActorForwardVector GetActorRightVector GetActorUpVector GetWorld Destroy IsHidden SetActorHiddenInGame SetActorTickEnabled
SetActorEnableCollision GetRootComponent AddInstanceComponent FinishSpawning DispatchBeginPlay HasActorBegunPlay IsActorBeingDestroyed
GetCapsuleComponent GetCharacterMovement GetMesh GetController GetLevel GetTimerManager SetupAttachment RegisterComponent
SetRelativeLocation SetRelativeRotation SetRelativeTransform SetRelativeScale3D SetWorldLocation SetWorldRotation SetVisibility
SetHiddenInGame SetCollisionEnabled SetCollisionProfileName SetCollisionResponseToAllChannels SetCollisionResponseToChannel
SetStaticMesh SetMaterial SetCanEverAffectNavigation SetMobility SetCastShadow GetComponentLocation GetComponentRotation
GetComponentTransform DestroyComponent AttachToComponent DetachFromComponent SetRenderCustomDepth SetCustomDepthStencilValue
GetName GetClass IsA Implements GetOwner SetIntensity SetLightColor SetText SetTextRenderColor SetWorldSize
SetHorizontalAlignment SetVerticalAlignment AddMovementInput Possess UnPossess SetViewTargetWithBlend SetComponentTickEnabled
MarkRenderStateDirty GetForwardVector GetRightVector GetUpVector K2_DestroyActor SetActorScale3D SetActorRelativeLocation
SetActorRelativeRotation AttachToActor DetachFromActor SetOwner GetGameInstance GetFirstPlayerController OverlapAnyTestByObjectType
LineTraceSingleByChannel SweepSingleByChannel SpawnActor SpawnActorDeferred IsValid IsValidLowLevel MoveIgnoreActorAdd
IgnoreActorWhenMoving SetBoxExtent SetCapsuleSize SetSphereRadius AddInstance AddInstances ClearInstances SetCustomDataValue
GetInstanceCount UpdateInstanceTransform BatchUpdateInstancesTransforms SetCullDistances SetNumCustomDataFloats""".split())
for h in HEADERS:
    t = strip_comments(h.read_text(encoding="utf-8"))
    for name, b, body in class_bodies(t):
        classes.setdefault(name, set()).update(members(body))
        mt = member_types.setdefault(name, {})
        body = re.sub(r"\b(TArray|TMap|TSet|TFunction|TOptional|TArrayView)\s*<[^;(){}]*?>", "Container", body)
        for mm in re.finditer(r"(?:TObjectPtr<|TWeakObjectPtr<|TSharedPtr<|TSharedRef<)?\b([AUSIF]Vila\w+)\b>?\s*\*?\s*(\w+)\s*(?:=[^;]*)?;", body):
            mt[mm.group(2)] = mm.group(1)
        bases.setdefault(name, []).extend(b)
    # 2. generated.h
    if re.search(r"\b(UCLASS|USTRUCT|UENUM|UINTERFACE)\s*\(", t):
        incs = re.findall(r'#include\s+"([^"]+)"', h.read_text(encoding="utf-8"))
        want = h.stem + ".generated.h"
        if not incs or incs[-1] != want:
            problems.append(f"{h.relative_to(ROOT)}: o último include deve ser \"{want}\"")

for c in SOURCES:
    for name, b, body in class_bodies(strip_comments(c.read_text(encoding="utf-8"))):
        classes.setdefault(name, set()).update(members(body))
        bases.setdefault(name, []).extend(b)

local = {p.name for p in HEADERS} | {str(p.relative_to(MOD / "Public")) for p in HEADERS if (MOD / "Public") in p.parents}
local |= {str(p.relative_to(MOD / "Private")) for p in MOD.rglob("*.h") if (MOD / "Private") in p.parents}
core = {str(p.relative_to(CORE)) for p in CORE.rglob("*.h")}


def all_members(cls, seen=None):
    seen = seen or set()
    if cls in seen:
        return set(), False
    seen.add(cls)
    ms, engine = set(classes.get(cls, set())), False
    for b in bases.get(cls, []):
        b = b.replace("public", "").strip()
        if b in classes:
            sub, e = all_members(b, seen)
            ms |= sub
            engine |= e
        else:
            engine = True
    return ms, engine


used_classes = set()
for f in HEADERS + SOURCES:
    raw = f.read_text(encoding="utf-8")
    t = strip_comments(raw)
    rel = f.relative_to(ROOT)
    # 1. includes locais
    for inc in re.findall(r'#include\s+"([^"]+)"', raw):
        if inc.endswith(".generated.h") or inc.startswith("Vila/") and inc in core:
            continue
        if inc.startswith("Vila/") and inc not in core:
            problems.append(f"{rel}: include do núcleo inexistente \"{inc}\"")
        elif re.match(r"(UI/)?[AUSIF]?Vila\w*\.h$", inc) and inc not in local and Path(inc).name not in local:
            problems.append(f"{rel}: include local inexistente \"{inc}\"")
    used_classes |= set(OWN.findall(t))
    # 3. chamadas: tipos inferidos por função (parâmetros, locais e membros da classe)
    DECL = re.compile(r"(?:TObjectPtr<|TWeakObjectPtr<|TSharedPtr<|TSharedRef<|TStrongObjectPtr<)?\b([AUSIF]Vila\w+)\b>?\s*(?:const\s*)?[*&]?\s*(\w+)\s*(?=[=;,)\[{:])")
    CAST = re.compile(r"\b(?:auto|const auto)\s*\*?\s*(\w+)\s*=\s*(?:Cast|CastChecked|NewObject|SpawnActor\w*)\s*<\s*([AUSIF]Vila\w+)\s*>")
    for fm in re.finditer(r"\b(\w+)::(~?\w+)\s*\(([^)]*)\)[^;{]*\{", t):
        owner, start = fm.group(1), fm.end()
        depth, i = 1, start
        while i < len(t) and depth:
            depth += {"{": 1, "}": -1}.get(t[i], 0)
            i += 1
        body = fm.group(3) + ";" + t[start:i]
        # contêineres (TArray<FVilaX> Arr) não são o tipo do elemento
        body = re.sub(r"\b(TArray|TMap|TSet|TFunction|TOptional|TInlineComponentArray|TArrayView)\s*<[^;(){}]*?>", "Container", body)
        vars_ = dict(member_types.get(owner, {}))
        for m in DECL.finditer(body):
            vars_[m.group(2)] = m.group(1)
        for m in CAST.finditer(body):
            vars_[m.group(1)] = m.group(2)
        for m in re.finditer(r"\b(\w+)\s*(?:->|\.)\s*(\w+)\s*\(", body):
            var, meth = m.groups()
            cls = vars_.get(var)
            if not cls or cls not in classes:
                continue
            ms, engine = all_members(cls)
            if meth in SMART_PTR or meth not in ms and engine and meth in ENGINE_COMMON:
                continue
            if meth not in ms:
                (manual if engine else problems).append(f"{rel}: {cls}::{meth} não declarado" + (" (pode ser do motor)" if engine else ""))
    for m in re.finditer(r"\b([AUS]Vila\w+)::Get\([^()]*\)\s*->\s*(\w+)", t):
        cls, meth = m.groups()
        if cls in classes:
            ms, engine = all_members(cls)
            if meth not in ms:
                (manual if engine else problems).append(f"{rel}: {cls}::{meth} não declarado" + (" (pode ser do motor)" if engine else ""))

# 4. classes usadas e não declaradas (ignora gerados/macros e aliases `using X = ...`)
for f in HEADERS + SOURCES:
    for a in re.findall(r"\busing\s+([AUSIF]Vila\w+)\s*=", f.read_text(encoding="utf-8")):
        classes.setdefault(a, set())
for c in sorted(used_classes - set(classes)):
    if c.endswith("_API") or c.startswith("FVilaCore"):
        continue
    problems.append(f"classe/struct usada mas não declarada: {c}")

# 5. métodos fora de linha sem definição
defs = set()
for s in SOURCES:
    defs |= set(re.findall(r"\b(\w+)::(~?\w+)\s*\(", strip_comments(s.read_text(encoding="utf-8"))))
for h in HEADERS:
    t = strip_comments(h.read_text(encoding="utf-8"))
    for name, _, body in class_bodies(t):
        if name.startswith("S"):  # widgets Slate: Construct etc. definidos no .cpp, checados igual
            pass
        flat, depth = [], 0
        for ch in body:
            if ch == "{":
                if depth == 0:
                    flat.append("{")  # corpo inline: a declaração já é a definição
                depth += 1
            elif ch == "}":
                depth -= 1
                flat.append(";")
            elif depth == 0:
                flat.append(ch)
        for decl in "".join(flat).split(";"):
            d = decl.strip()
            if "{" in d:
                continue
            if "(" not in d or "=" in d.split("(")[0] or d.endswith("= 0") or "= default" in d or "= delete" in d:
                continue
            if re.match(r"^(UPROPERTY|UFUNCTION|GENERATED_BODY|SLATE_|DECLARE_|friend|using|typedef|template)", d):
                continue
            m = re.search(r"(~?\w+)\s*\([^()]*(\([^()]*\)[^()]*)*\)\s*(const)?\s*(override|final)?\s*$", d)
            if not m:
                continue
            meth = m.group(1)
            if meth in ("SLATE_BEGIN_ARGS", "SLATE_END_ARGS", "SLATE_ARGUMENT", "SLATE_EVENT", "SLATE_ATTRIBUTE"):
                continue
            if (name, meth) not in defs and meth != name.lstrip("~"):
                problems.append(f"{h.relative_to(ROOT)}: {name}::{meth} declarado sem definição em .cpp")
            elif meth == name and (name, name) not in defs and not re.search(rf"\b{name}\s*\([^;]*\)\s*[:{{]", t):
                pass

print(f"{len(HEADERS)} cabeçalhos, {len(SOURCES)} fontes, {len(classes)} classes")
for p in sorted(set(problems)):
    print("ERRO  ", p)
for p in sorted(set(manual)):
    print("VERIF.", p)
sys.exit(1 if problems else 0)
