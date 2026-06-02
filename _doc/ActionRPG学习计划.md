# ActionRPG 学习计划（面向 C++ 程序员的 UE 正向开发入门）

> 目标读者：有扎实 C++ 基础、**未做过 UE 正向开发**的工程师。
> 学习载体：Epic 官方 **ActionRPG** 示例（UE 4.27.2），本仓库即该项目。
> 官方文档：<https://dev.epicgames.com/documentation/unreal-engine/action-rpg-game?application_version=4.27>
> 配套阅读：仓库根目录 `CLAUDE.md`（已总结了完整架构，建议每个阶段对照看）。

---

## 0. 学习策略（先读这一段）

作为 C++ 程序员，你的优势和陷阱很明确：

- **优势**：能直接看 `Source/` 里的 C++ 源码，理解类继承、内存、模板。ActionRPG 的核心系统恰好都在 C++ 里。
- **陷阱**：UE 不是"普通 C++"。它有自己的一套对象系统（`UObject`/反射/GC）、宏（`UCLASS`/`UPROPERTY`/`UFUNCTION`）、编辑器工作流，以及**大量逻辑在蓝图里而不在 C++ 里**。本项目"几乎所有 gameplay 逻辑都在 `Content/` 的蓝图中，C++ 只提供可被蓝图继承的基类"。

因此本计划的主线是：**用你能读懂的 C++ 当锚点，逐步上浮到蓝图与编辑器**，而不是反过来从零学引擎。

**学习方法贯穿全程**：
1. 每学一个系统，先在 C++ 里找到基类（`RPG*Base`），读懂它暴露了什么。
2. 再去编辑器里找到对应的蓝图子类（`BP_*`），看设计师在上面接了什么。
3. 最后改一个小东西、运行、观察 —— 形成"代码↔蓝图↔运行"的闭环。

**关键认知**：UE 里 C++ 基类几乎从不直接使用，而是被蓝图继承后再在配置里引用。例如 `Config/DefaultEngine.ini` 指向的是 `BP_GameMode_C` 而不是 C++ 的 `ARPGGameModeBase`。理解这一"C++ 定义能力、蓝图装配数据"的分工，是 UE 正向开发的核心思维转变。

---

## 阶段一：环境与引擎心智模型（约 2–3 天）

**目标**：能编译、能打开、能运行，并建立 UE 的基本世界观。

### 任务
1. **跑通工具链**（参考 `CLAUDE.md` 的 Build & run 段）：
   - 生成 VS 工程文件（右键 `.uproject` → Generate Visual Studio project files）。
   - 用 `Build.bat ActionRPGEditor Win64 Development` 编译编辑器目标。
   - 用 `UE4Editor.exe ActionRPG.uproject` 打开项目，按 Play 进游戏，先把它当玩家玩一遍。
2. **认识两个 Target**：`ActionRPGTarget`（打包游戏）vs `ActionRPGEditorTarget`（编辑器）。看 `Source/*.Target.cs`。
3. **认识模块**：`ActionRPG`（Runtime 主模块）和 `ActionRPGLoadingScreen`（独立模块，只为让加载屏在主模块之前初始化）。读 `ActionRPG.uproject` 的 `Modules` 段和 `Source/ActionRPG/ActionRPG.Build.cs`。

### 必须建立的概念（UE 与普通 C++ 的差异）
- **UObject 体系**：反射、垃圾回收（GC）、`UPROPERTY` 让指针被 GC 追踪。普通裸指针指向 UObject 是危险的。
- **核心宏**：`UCLASS / USTRUCT / UENUM / UPROPERTY / UFUNCTION` 各自的作用与常用修饰符（`EditAnywhere`、`BlueprintReadWrite`、`BlueprintCallable`、`Category`）。
- **命名前缀规则**：`U`=UObject、`A`=Actor、`F`=struct、`I`=interface、`E`=enum；本项目额外加 `RPG` 前缀。
- **`ACTIONRPG_API` 导出宏**：所有 public 类型都带它（DLL 导出）。
- **模块的 Build.cs**：相当于 UE 版的依赖声明，本模块依赖 `GameplayAbilities / GameplayTags / GameplayTasks / AIModule`。

### 验收
- 能独立完成"改一行 C++ → 编译 → 运行看到效果"。
- 能说清 Actor、Component、GameMode、GameInstance、PlayerController 各自是什么、生命周期如何。

---

## 阶段二：项目骨架与"C++ 基类 ↔ 蓝图子类"分工（约 3–4 天）

**目标**：彻底理解本项目"原生基类被蓝图化"的架构惯例 —— 这是读懂全项目的钥匙。

### 任务
1. **追一条配置链**：打开 `Config/DefaultEngine.ini`，找到：
   - `GameInstanceClass=/Game/Blueprints/BP_GameInstance.BP_GameInstance_C`
   - `GlobalDefaultGameMode=/Game/Blueprints/BP_GameMode.BP_GameMode_C`
   - `AssetManagerClassName=/Script/ActionRPG.RPGAssetManager`（注意：这个直接指向 C++ 类）
   对每一条，分别找到 C++ 基类（`Source/`）和蓝图子类（编辑器 `Content/Blueprints/`），对比"基类定义了什么、蓝图加了什么"。
2. **读懂一对基类/子类**：以 `ARPGGameModeBase`（C++）↔ `BP_GameMode`（蓝图）为例，理解 `BlueprintImplementableEvent`（C++ 声明、蓝图实现）和 `BlueprintCallable`（C++ 实现、蓝图调用）这两种方向相反的桥接方式。
3. **理解 `RPGTypes.h` 的角色**：项目约定把"共享 enum / struct / **所有 delegate 声明**"集中放这里以避免递归 include。先通读一遍，混个眼熟。

### 验收
- 给定任意一个 `RPG*Base` 类，你能预测"它一定有个 `BP_*` 子类在 Content 里"，并能在编辑器里找到它。
- 能解释 `BlueprintImplementableEvent` 和 `BlueprintCallable` 的区别与各自使用场景。

---

## 阶段三：蓝图与编辑器工作流（约 4–5 天，本阶段最反直觉）

**目标**：作为 C++ 程序员最容易跳过、但绝不能跳过的部分 —— 让你能"读"蓝图、能做基础编辑器操作。

### 任务
1. **蓝图可视化脚本基础**：在编辑器里打开几个 `BP_*` 资产，看 Event Graph：执行引脚（白线）vs 数据引脚（彩色线）、节点、函数、事件、变量。重点能"读懂"而非"精通画图"。
2. **编辑器五件套**：内容浏览器（Content Browser）、关卡编辑器（Viewport/Outliner）、细节面板（Details）、蓝图编辑器、数据资产编辑器。各自干什么。
3. **关卡（Map）**：打开 `Content/Maps/` 里的关卡（如当前 git status 中改动的 `ActionRPG_Dungeon02_Lights.umap`），理解关卡 = Actor 的容器。注意 `.umap`/`.uasset` 是二进制，**只能在编辑器里改，不能用文本工具 diff**。
4. **C++ ↔ 蓝图协作实操**：在某个 `RPG*Base` 里新增一个 `UFUNCTION(BlueprintCallable)`，编译后到对应蓝图里把它连进 Event Graph，运行验证。

### 给 C++ 程序员的提醒
- 不要试图把所有逻辑搬回 C++。UE 正向开发的常态就是 C++/蓝图混合，设计师面向的数据与调参留在蓝图/数据资产。
- 二进制资产的版本管理：本项目用 Git，但 `.uasset`/`.umap` 无法合并，团队里靠"资产独占/沟通"避免冲突。

### 验收
- 能独立打开任意蓝图、看懂主要的事件流。
- 能完成"C++ 加函数 → 蓝图调用 → 运行"的往返。

---

## 阶段四：Asset Manager 与 Primary Data Assets（约 3–4 天）

**目标**：理解 UE 推荐的"数据驱动 + 按需异步加载"资产管理范式。这是 ActionRPG 区别于教学 demo 的工程化亮点。

### 任务（对照 `CLAUDE.md` 的 "Items are Primary Data Assets" 段）
1. **读 `URPGItem`**（`Items/RPGItem.h`）：抽象 `UPrimaryDataAsset`，是所有物品的基类。
2. **看四个具体子类**：`URPGPotionItem / URPGSkillItem / URPGTokenItem / URPGWeaponItem`，各自有不同的 `FPrimaryAssetType`。
3. **看类型常量**：这些 type 字符串常量是 `URPGAssetManager` 上的静态成员（`PotionItemType` 等）。
4. **看注册配置**：`Config/DefaultGame.ini` 的 `[/Script/Engine.AssetManagerSettings] PrimaryAssetTypesToScan`，每个 type 映射到 `Content/Items/<Type>` 目录，`CookRule=AlwaysCook`。
5. **理解加载**：`URPGAssetManager::ForceLoadItem` 是同步加载（会卡顿）且不持有引用；物品到处用 `FPrimaryAssetId` 标识。

### 动手练习（强烈推荐）
- **新增一个物品类型**，体会"三处同步修改"的约束：① 新 C++ 子类；② 在 `RPGAssetManager` 加 `FPrimaryAssetType` 常量；③ 在 `DefaultGame.ini` 加 `PrimaryAssetTypesToScan` 条目。漏一处就不会被扫描到。

### 验收
- 能解释 PrimaryAssetId、PrimaryAssetType、同步 vs 异步加载的取舍。
- 独立完成新增物品类型并在编辑器里创建出一个该类型的数据资产。

---

## 阶段五：背包 / 存档系统（约 3–4 天）

**目标**：理解"数据存哪、谁是 source of truth、如何异步持久化、如何用 delegate 广播变化"。

### 任务（对照 `CLAUDE.md` 的 "Inventory" 段）
1. **背包归属**：`ARPGPlayerControllerBase` 实现 `IRPGInventoryInterface`，拥有两张权威 map：
   - `InventoryData : TMap<URPGItem*, FRPGItemData>`
   - `SlottedItems : TMap<FRPGItemSlot, URPGItem*>`
2. **接口的意义**：`IRPGInventoryInterface` 是 native-only（`CannotImplementInterfaceInBlueprint`），让 `ARPGCharacterBase` 无需向下转型就能读背包。理解 UE interface 与普通 C++ 多继承的差别。
3. **持久化链路**：经 `URPGGameInstanceBase` —— 它持有 `CurrentSaveGame`(`URPGSaveGame`)、`DefaultInventory`、`ItemSlotsPerType`。存读档是异步的：`WriteSaveGame / HandleAsyncSave / LoadOrCreateSaveGame`。
4. **变化广播**：背包改动通过 `RPGTypes.h` 里的 delegate 传播，**每个变化都有 dynamic（蓝图可绑定，如 `FOnInventoryItemChanged`）和 native（`*Native`）两个版本**。理解为什么要两套。

### 验收
- 能画出"修改背包 → 触发哪些 delegate → UI/能力如何响应"的数据流。
- 能解释 dynamic delegate（支持蓝图、`UFUNCTION` 绑定）vs native delegate（纯 C++、更快）的取舍。

---

## 阶段六：GameplayAbilities（GAS）—— 本项目的核心与难点（约 7–10 天）

**目标**：这是 ActionRPG 存在的主要原因，也是 UE 里出了名陡峭的系统。**建议拆成多个小阶段，配合官方 GAS 文档慢啃。**

### 6.1 GAS 总览与组件
- `ARPGCharacterBase` 实现 `IAbilitySystemInterface`，拥有 `URPGAbilitySystemComponent`(ASC) 和 `URPGAttributeSet`，是连接背包与能力的枢纽。
- 先建立四大件的心智模型：**ASC（能力系统组件）、GameplayAbility（能力）、GameplayEffect（效果）、AttributeSet（属性集）、GameplayTag（标签）**。

### 6.2 属性与伤害管线（C++ 程序员最容易读懂的切入点）
- `URPGAttributeSet` 定义全部属性：Health、MaxHealth、Mana、MaxMana、AttackPower、DefensePower、MoveSpeed，以及一个临时属性 `Damage`。
- **伤害管线**（对照 `CLAUDE.md` 的 "Damage pipeline summary"）：
  攻击 → 能力施加一个 `GameplayEffect`，其执行体是 `URPGDamageExecution`（一个 `GameplayEffectExecutionCalculation`）→ 由 base damage / 攻击者 `AttackPower` / 目标 `DefensePower` 算出 `Damage` → `URPGAttributeSet::PostGameplayEffectExecute` 把 `Damage` 转成 `-Health` → 路由到 `ARPGCharacterBase::HandleDamage` → 蓝图事件 `OnDamaged`/`OnKilled`。
- 重点理解 `AttributeSet` 被 `friend` 给 character，并回调 `HandleDamage`/`HandleHealthChanged`，这些再触发 `BlueprintImplementableEvent`。

### 6.3 能力与效果容器
- `URPGGameplayAbility` 加了 `EffectContainerMap`(`FGameplayTag → FRPGGameplayEffectContainer`)，让设计师按"触发标签"成套施加效果。
- `MakeEffectContainerSpec` / `ApplyEffectContainerSpec`（类型在 `Abilities/RPGAbilityTypes.h`）把"构建效果 spec"与"施加"分离。

### 6.4 物品槽授予能力（把 GAS 和背包打通）
- `URPGItem::GrantedAbility` 把物品绑定到 `URPGGameplayAbility`。
- character 维护 `FRPGItemSlot → FGameplayAbilitySpecHandle` 的 `SlottedAbilities`；当背包槽位变化（`OnItemSlotChanged`）时，`RefreshSlottedGameplayAbilities` / `FillSlottedAbilitySpecs` 负责同步授予/移除能力；`DefaultSlottedAbilities` 覆盖背包加载前的情况。

### 6.5 自定义能力任务与标签
- `RPGAbilityTask_PlayMontageAndWaitForEvent`：把蒙太奇（动画）播放与 gameplay 事件等待结合的自定义 AbilityTask。
- 标签定义在 `Config/DefaultGameplayTags.ini`。

### 动手练习
- 给玩家新增一个属性（如 CritChance），让它参与 `URPGDamageExecution` 的计算，并在 UI 上显示。这能串起属性集、执行计算、伤害管线、UI 绑定四块。

### 验收
- 能口述完整伤害管线，从输入攻击到 Health 下降到蓝图事件。
- 能解释 GameplayEffect 的三种 duration（Instant/Duration/Infinite）和 GameplayTag 在能力激活/拦截中的作用。

---

## 阶段七：UI（UMG）、加载屏与异步（约 3–4 天）

**目标**：理解 UE 的 UI 框架和"为什么要有独立的加载屏模块"。

### 任务
1. **UMG 基础**：在编辑器里打开 `Content/UI/` 的 Widget 蓝图，看背包 UI 如何绑定到阶段五的 delegate 上自动刷新。
2. **加载屏模块**：`ActionRPGLoadingScreen` 是 `ClientOnly` + `PreLoadingScreen` 阶段的独立模块，只为在主模块加载前就能显示影片/加载屏；通过 `IActionRPGLoadingScreenModule` 接口访问，且**不能依赖 ActionRPG runtime 类型**。理解 UE 模块加载阶段（LoadingPhase）的意义。
3. **异步加载**：把阶段四的 `ForceLoadItem`（同步、会卡顿）和资产管理器的异步加载对比，理解移动端为什么必须异步。

### 验收
- 能解释一个 UI 控件如何随背包数据变化自动更新（delegate → 事件 → 刷新）。
- 能解释为什么加载屏要做成独立模块、且不能反向依赖主模块。

---

## 阶段八：打包、平台与综合实战（约 4–5 天）

**目标**：跑通完整产出链，并独立完成一个贯穿全栈的功能。

### 任务
1. **打包**：用 `ActionRPGTarget` 打一个 WindowsNoEditor 包（仓库已有 `Build/WindowsNoEditor/` 产物可参考），理解 Cook、`AlwaysCook` 规则、打包 vs 编辑器运行的差异。
2. **平台意识**：目标平台是 Android / iOS / WindowsNoEditor / MacNoEditor；`ActionRPG.Build.cs` 为 iOS/Android 条件性加 OnlineSubsystem 依赖。了解移动端约束如何反向影响了前面所有架构决策（异步加载、数据驱动、GAS 网络化）。
3. **毕业项目（任选其一，建议做完整）**：
   - 新增一件**武器**：新建 `URPGWeaponItem` 数据资产 → 绑定一个新的 `URPGGameplayAbility` → 该能力施加自定义 `GameplayEffect` → 在 UI 槽位里装备它 → 运行验证伤害与动画。
   - 这个项目会强制你把阶段四（数据资产）、五（背包/装备槽）、六（GAS/能力/效果）、七（UI）全部串起来。

### 验收
- 独立打出一个可运行的包。
- 独立完成毕业项目，并能讲清它如何贯穿 Asset Manager → 背包 → GAS → UI。

---

## 学习资源清单

| 主题 | 资源 |
|------|------|
| 本项目官方文档 | <https://dev.epicgames.com/documentation/unreal-engine/action-rpg-game?application_version=4.27> |
| 本仓库架构总结 | 根目录 `CLAUDE.md`（每阶段必读） |
| GameplayAbilities 系统 | 官方 GAS 文档 + 社区经典《GASDocumentation》 |
| UObject / 反射 / GC | 官方 "Gameplay Architecture" / "Unreal Object Handling" |
| Asset Manager | 官方 "Asset Management" 文档 |
| UMG | 官方 "UMG UI Designer" 文档 |

---

## 进度建议与里程碑

| 阶段 | 主题 | 预计耗时 | 里程碑 |
|------|------|---------|--------|
| 一 | 环境 + 引擎心智模型 | 2–3 天 | 改 C++ → 编译 → 运行闭环 |
| 二 | C++ 基类↔蓝图子类分工 | 3–4 天 | 能追配置链到基类与子类 |
| 三 | 蓝图与编辑器工作流 | 4–5 天 | 能读蓝图、做 C++↔蓝图往返 |
| 四 | Asset Manager / 数据资产 | 3–4 天 | 新增物品类型成功 |
| 五 | 背包 / 存档 | 3–4 天 | 画出背包数据流 |
| 六 | **GAS（核心难点）** | 7–10 天 | 口述伤害管线 + 加新属性 |
| 七 | UI / 加载屏 / 异步 | 3–4 天 | 解释 UI 自动刷新机制 |
| 八 | 打包 + 毕业项目 | 4–5 天 | 打包 + 新增武器全栈打通 |

> 总计约 **5–6 周**（按每天 3–4 小时估算）。GAS 是最大的时间黑洞，不要赶进度，宁可在阶段六多停留。

---

## 给 C++ 程序员的最终建议

1. **别抗拒蓝图**。把它当成"可视化的、面向设计师的 C++"，而不是低级玩具。读得懂蓝图，你才能读懂这个项目一半以上的逻辑。
2. **以 C++ 基类为锚，向上追蓝图**。每个系统都从 `Source/` 进入，这是你的主场；再到 `Content/` 看装配。
3. **改了就跑**。UE 的很多行为靠运行时才看得清（GC、加载时机、网络复制），静态读代码会漏掉一半。
4. **GAS 要配合官方文档和社区资料**，光看本项目源码会很吃力 —— 它用了 GAS 的高级特性（ExecutionCalculation、EffectContainer、自定义 AbilityTask）。
5. **二进制资产小心改**。`.uasset`/`.umap` 只在编辑器里改、无法 diff/merge，改前先想清楚团队协作影响。
