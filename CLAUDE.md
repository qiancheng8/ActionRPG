# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Epic Games' **ActionRPG** sample project for **Unreal Engine 4.27.2** (`EngineAssociation` `"4.27"`). It is a reference implementation demonstrating the **GameplayAbilities (GAS)** plugin, the **Asset Manager / Primary Assets** system, and a save-game–backed inventory. Almost all gameplay logic lives in Blueprints under `Content/`; the C++ in `Source/` provides blueprintable base classes and the systems that are awkward to express in Blueprint.

## Build & run

There are no in-repo build scripts; build through the engine toolchain (adjust the engine path to your install):

```powershell
# Generate Visual Studio project files (right-click .uproject → "Generate ... files", or:)
& "<UE_4.27>\Engine\Build\BatchFiles\GenerateProjectFiles.bat" "<repo>\ActionRPG.uproject"

# Build the editor target (Development Editor, Win64)
& "<UE_4.27>\Engine\Build\BatchFiles\Build.bat" ActionRPGEditor Win64 Development "<repo>\ActionRPG.uproject" -waitmutex

# Open in editor
& "<UE_4.27>\Engine\Binaries\Win64\UE4Editor.exe" "<repo>\ActionRPG.uproject"
```

Two targets exist: `ActionRPGTarget` (packaged game) and `ActionRPGEditorTarget` (editor). Both use `BuildSettingsVersion.V2`. There is no test suite, lint step, or CI in this repo.

## Module layout

Two C++ modules (declared in `ActionRPG.uproject` → `Modules`):

- **ActionRPG** (`Runtime`, default phase) — all gameplay code. Build deps include `GameplayAbilities`, `GameplayTags`, `GameplayTasks`, `AIModule` (`Source/ActionRPG/ActionRPG.Build.cs`). Headers split `Public/` vs `Private/`; PCH is `Public/ActionRPG.h`.
- **ActionRPGLoadingScreen** (`ClientOnly`, `PreLoadingScreen` phase) — a separate module purely so the movie/loading screen can initialize *before* the main module loads. Accessed via the `IActionRPGLoadingScreenModule` interface; it cannot depend on ActionRPG runtime types.

The `GameplayAbilities` plugin is the only non-default plugin enabled (plus `SlateRemote`). Target platforms: Android, iOS, WindowsNoEditor, MacNoEditor — `ActionRPG.Build.cs` adds OnlineSubsystem deps conditionally for iOS/Android.

## Architecture

### Native base classes are blueprinted, not used directly
Every `RPG*Base` C++ class is meant to be subclassed in Blueprint, and the Blueprint subclass is what's wired up in config. Config in `Config/DefaultEngine.ini` points the engine at the **Blueprint** classes, not the C++ ones:
- `GameInstanceClass=/Game/Blueprints/BP_GameInstance.BP_GameInstance_C`
- `GlobalDefaultGameMode=/Game/Blueprints/BP_GameMode.BP_GameMode_C`
- `AssetManagerClassName=/Script/ActionRPG.RPGAssetManager` (this one *is* the C++ class)

When changing gameplay behavior, decide whether it belongs in the C++ base or the Blueprint subclass; designer-facing data and tuning generally live in Blueprint/data assets.

### Items are Primary Data Assets, loaded via the Asset Manager
`URPGItem` (`Items/RPGItem.h`) is an abstract `UPrimaryDataAsset`. Concrete native subclasses — `URPGPotionItem`, `URPGSkillItem`, `URPGTokenItem`, `URPGWeaponItem` — each define a distinct `FPrimaryAssetType`. The matching string constants live as statics on `URPGAssetManager` (`PotionItemType`, `SkillItemType`, `TokenItemType`, `WeaponItemType`).

`Config/DefaultGame.ini` registers these types under `[/Script/Engine.AssetManagerSettings]` `PrimaryAssetTypesToScan`, mapping each type to a `Content/Items/<Type>` directory with `CookRule=AlwaysCook`. **If you add a new item type, you must add it in three places**: the native subclass, the `FPrimaryAssetType` constant in `RPGAssetManager`, and a `PrimaryAssetTypesToScan` entry in `DefaultGame.ini`.

`URPGAssetManager::ForceLoadItem` does a synchronous load (can hitch) and does *not* retain a reference. Items are identified everywhere by `FPrimaryAssetId`.

### Inventory: stored on the PlayerController, persisted via SaveGame
`ARPGPlayerControllerBase` implements `IRPGInventoryInterface` and owns the two source-of-truth maps: `InventoryData` (`TMap<URPGItem*, FRPGItemData>`) and `SlottedItems` (`TMap<FRPGItemSlot, URPGItem*>`). The interface (`RPGInventoryInterface.h`) exists so `ARPGCharacterBase` can read inventory without casting to a specific controller, and is **native-only** (`CannotImplementInterfaceInBlueprint`).

Persistence flows through `URPGGameInstanceBase`: it holds `CurrentSaveGame` (a `URPGSaveGame`), the `DefaultInventory`, and `ItemSlotsPerType` (how many slots exist per item type). Save/load is async (`WriteSaveGame`, `HandleAsyncSave`, `LoadOrCreateSaveGame`). Inventory changes propagate through the delegates declared in `RPGTypes.h` — every change has both a dynamic (Blueprint-assignable, e.g. `FOnInventoryItemChanged`) and a native (`*Native`) variant.

### GameplayAbilities (GAS) wiring
- `ARPGCharacterBase` implements `IAbilitySystemInterface`, owns a `URPGAbilitySystemComponent` and a `URPGAttributeSet`, and is the hub connecting inventory to abilities.
- **Item slots grant abilities.** `URPGItem::GrantedAbility` ties an item to a `URPGGameplayAbility`. The character maps `FRPGItemSlot → FGameplayAbilitySpecHandle` in `SlottedAbilities`; `RefreshSlottedGameplayAbilities` / `FillSlottedAbilitySpecs` reconcile granted abilities whenever the inventory's slotted items change (via `OnItemSlotChanged`). `DefaultSlottedAbilities` covers slots before inventory loads.
- `URPGAttributeSet` defines all attributes (Health, MaxHealth, Mana, MaxMana, AttackPower, DefensePower, MoveSpeed, and a transient `Damage`). `Damage` is a temporary attribute consumed by `URPGDamageExecution` (a `GameplayEffectExecutionCalculation`) and converted into `-Health` in `PostGameplayEffectExecute`. The attribute set is `friend`ed to the character and calls back into `HandleDamage`/`HandleHealthChanged`/etc., which fire the `BlueprintImplementableEvent`s (`OnDamaged`, `OnHealthChanged`, ...).
- `URPGGameplayAbility` adds an `EffectContainerMap` (`FGameplayTag → FRPGGameplayEffectContainer`) so designers apply sets of gameplay effects keyed by a triggering tag. The `MakeEffectContainerSpec` / `ApplyEffectContainerSpec` pair (types in `Abilities/RPGAbilityTypes.h`) separates *building* an effect spec from *applying* it.
- `RPGAbilityTask_PlayMontageAndWaitForEvent` is a custom ability task combining montage playback with gameplay-event waiting.
- Gameplay tags are defined in `Config/DefaultGameplayTags.ini`.

### Damage pipeline summary
Attack → ability applies a `GameplayEffect` whose execution is `URPGDamageExecution` → computes a `Damage` value from base damage, attacker `AttackPower`, target `DefensePower` → `URPGAttributeSet::PostGameplayEffectExecute` turns `Damage` into a `Health` reduction → routes to `ARPGCharacterBase::HandleDamage` → Blueprint `OnDamaged`/`OnKilled` events.

## Conventions

- Prefixes follow UE rules: `U` = UObject, `A` = Actor, `F` = struct, `I` = interface, `E` = enum. Game classes are additionally prefixed `RPG`.
- All public types use the `ACTIONRPG_API` export macro.
- Shared enums, structs, and **all delegate declarations** go in `RPGTypes.h` to avoid recursive includes — put new cross-cutting structs/delegates there, not in individual class headers.
- C++ exposes hooks to Blueprint via `BlueprintImplementableEvent` (BP implements) and `BlueprintCallable`/`BlueprintPure` (C++ implements, BP calls). Most concrete content (data assets, BP_ subclasses, UI) lives in `Content/`, which is binary `.uasset`/`.umap` and only editable in the editor.
