# 阶段四·补充：把 Asset Manager 这几个概念彻底讲通

这篇是阶段四正文的配套答疑。正文带你跑通了"加一个新物品类型"的实操，但有几个概念光看实操容易夹生：

1. Asset Manager 到底有哪些用途？（不只是加载物品）
2. Primary Data Asset 到底是个什么东西？
3. 从源码看，`UAssetManager` 和 `UPrimaryDataAsset` 是什么关系？
4. 有 `FPrimaryAssetType`，那有没有 `Secondary`？

我把顺序调了一下，先讲第 4 个。因为"主要资产 / 次要资产"这条线是钥匙，钥匙拿到手，其余三个问题会一起开。

---

## 一、先建一个心智模型：仓库管理员

想象一个大仓库，里面堆着成千上万件东西。仓库雇了个**管理员**，这个管理员就是 **Asset Manager**。

但管理员没那么勤快——他**不会给每一件东西都建档**。他只给"重点货物"建一张**登记表**：每件重点货物给一个唯一**货号**，记下它放在哪个货架。至于重点货物的包装盒、说明书、配件这些**附属品**，他不单独登记，反正它们是跟着重点货物一起进出的。

把这个比喻翻译成 UE 的术语：

| 比喻 | UE 概念 |
|------|---------|
| 仓库管理员 | `UAssetManager`（全局单例） |
| 重点货物 | **主要资产 Primary Asset** |
| 货号 | `FPrimaryAssetId`（如 `Potion:Potion_Health`） |
| 货物的类别（药水/武器…） | `FPrimaryAssetType`（如 `Potion`） |
| 登记表 | Asset Manager 扫描后建立的索引 |
| 附属品（包装、配件） | **次要资产 Secondary Asset**（贴图、材质、模型、音效…） |

记住这张表，下面四个问题都是从它推出来的。

---

## 二、有 `FPrimaryAssetType`，那有没有 Secondary？

**结论先行：概念上有"次要资产（Secondary Asset）"，但代码里没有 `FSecondaryAssetType` 这个类型，也不需要。**

### 为什么概念上要分主次

UE 官方把所有资产分成两类：

- **主要资产（Primary Asset）**：你想**主动、按名字去加载/卸载**的资产。它有货号（`FPrimaryAssetId`），Asset Manager 给它建了档。ActionRPG 里的药水、技能、武器、代币都是。
- **次要资产（Secondary Asset）**：其余一切——贴图、材质、静态网格、音效、动画……它们**没有货号**，你不会写代码说"给我加载这张贴图"。它们是**因为被某个主要资产引用了，才被顺带加载进来**的。

举个 ActionRPG 的例子：你加载主要资产 `Potion:Potion_Health`（一瓶血药的数据资产），它身上的 `ItemIcon`（图标贴图）就是次要资产。你从来不会单独点名加载那张图标，它跟着血药一起来、一起走。

### 为什么不需要 `FSecondaryAssetType`

因为次要资产**根本不靠"类型 + 名字"来管理**。引擎管它们用的是另一套机制：**资产引用关系图（Asset Registry 依赖图）**。

加载流程是这样的：

```
你：加载 Potion:Potion_Health（主要资产，按货号点名）
        │
        ▼
Asset Manager：查登记表 → 找到磁盘路径 → 开始加载
        │
        ▼
引擎顺着"引用关系图"发现：这瓶药引用了 图标贴图、字体…（次要资产）
        │
        ▼
把这些次要资产一并加载进来（你没点名，引擎自动办了）
```

所以这是一种**故意的不对称设计**：

- 主要资产是你**手动管理的入口**，少而精，需要一个稳定的身份证（`FPrimaryAssetId`）。
- 次要资产是**被动跟随的依赖**，多而杂，没必要也给每个发身份证——靠引用图自动收。

一句话：**你只需要给"入口"编号，剩下的让依赖关系自动兜底。** 这就是没有 `Secondary` 类型的根本原因。

> 源码佐证：`FPrimaryAssetId`（在 `CoreUObject/Public/UObject/PrimaryAssetId.h`）里只有两个字段：
> ```cpp
> struct FPrimaryAssetId
> {
>     FPrimaryAssetType PrimaryAssetType;  // 类型，如 "Potion"
>     FName            PrimaryAssetName;   // 名字，如 "Potion_Health"
> };
> ```
> 整个引擎里**没有** `FSecondaryAssetId` 或 `FSecondaryAssetType`。而 `FPrimaryAssetType` 本身极简，就是个 `FName` 的包装：
> ```cpp
> struct FPrimaryAssetType
> {
>     FName Name;   // 全部家当就这一个 FName
> };
> ```

---

## 三、Primary Data Asset 到底是什么

理解了"主要/次要"，这个就好讲了。

先分清三个名字，别搞混：

- `UDataAsset`：最基础的"数据容器"基类。继承它，就能在内容浏览器里右键造一个纯数据资产（设计师填字段用）。但它**默认是次要资产**——没货号。
- `UPrimaryDataAsset`：`UDataAsset` 的子类。它做了一件关键的事——**让自己变成主要资产**（拿到了货号）。
- `URPGItem`：ActionRPG 自己的物品基类，继承自 `UPrimaryDataAsset`。

那 `UPrimaryDataAsset` 比 `UDataAsset` 多了什么，使它"够格当主要资产"？看引擎里它的类注释（`Engine/Classes/Engine/DataAsset.h`），写得非常直白：

```cpp
/**
 * A DataAsset that implements GetPrimaryAssetId and has asset bundle support,
 * which makes it something that can be manually loaded/unloaded from the AssetManager
 * ...
 */
UCLASS(abstract, MinimalAPI, Blueprintable)
class UPrimaryDataAsset : public UDataAsset
{
    ENGINE_API virtual FPrimaryAssetId GetPrimaryAssetId() const override;  // ← 关键
    // ... AssetBundleData 相关
};
```

划重点：`UPrimaryDataAsset` 干的核心事就是 **override 了 `GetPrimaryAssetId()`**。这个函数返回一个有效的货号，于是它就从"次要资产"晋升成了"主要资产"，能被 Asset Manager 按货号点名加载/卸载。

所以 **"Primary Data Asset"= 一个会自报货号、因而能被 Asset Manager 管理的数据资产**。就这么简单。

> 补充：它还顺带支持 **Asset Bundle**（资产捆绑），可以把一个主要资产身上的引用分组——比如"UI 用的部分"和"3D 场景用的部分"分开，按需只加载其中一组。这是进阶用法，初学先知道有这回事即可。

---

## 四、源码视角：`UAssetManager` 和 `UPrimaryDataAsset` 怎么"接上头"

这俩类一个管加载、一个被加载，它们之间唯一的"接头暗号"就是那个虚函数 **`GetPrimaryAssetId()`**。整条链路是这样握手的：

### 第 1 步：默认情况下，谁都不是主要资产

`UObject` 是万物之基。看它的默认实现（`CoreUObject/Private/UObject/Obj.cpp`）：

```cpp
FPrimaryAssetId UObject::GetPrimaryAssetId() const
{
    // ...（一个可选的全局回调，先忽略）
    return FPrimaryAssetId();   // 返回一个【无效】货号
}
```

`FPrimaryAssetId()` 是空的、无效的。意思是：**默认情况下，任何 UObject 都不是主要资产**（即都是次要资产）。这正好印证了第二节那条"次要是默认、主要是特例"。

### 第 2 步：`UPrimaryDataAsset` 改写它，发给自己一张货号

```cpp
// Engine/Private/DataAsset.cpp
FPrimaryAssetId UPrimaryDataAsset::GetPrimaryAssetId() const
{
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        // 这一大段是处理"蓝图子类"的：往上找父类，
        // 用第一个原生类(或紧贴 PrimaryDataAsset 的蓝图类)的名字当 Type
        // ...
    }

    // 普通数据资产：用【类名】当 Type，【资产名】当 Name
    return FPrimaryAssetId(GetClass()->GetFName(), GetFName());
}
```

看最后一行：默认规则是 **类名 当类型，资产名 当名字**。也就是说，如果直接用引擎默认逻辑，一个 `RPGPotionItem` 类型的药水，货号会是 `RPGPotionItem:Potion_Health`。

### 第 3 步：ActionRPG 嫌默认规则不好，又改了一次

ActionRPG 不想让"类型"等于 C++ 类名，它想让四个子类按**业务类型**（Potion/Skill/Token/Weapon）归类。所以 `URPGItem` 再次 override（`Source/ActionRPG/Private/Items/RPGItem.cpp`）：

```cpp
FPrimaryAssetId URPGItem::GetPrimaryAssetId() const
{
    // 这是 DataAsset 不是蓝图，直接用原始 FName 就行
    // 蓝图的话还要处理掉 _C 后缀
    return FPrimaryAssetId(ItemType, GetFName());   // ← 用 ItemType，不用类名
}
```

于是货号变成 `Potion:Potion_Health`——`ItemType` 是子类构造函数里写死的 `"Potion"`。这就是阶段四正文里反复出现的那个货号的来历。

> 这里藏着一个**新手最容易忽略、但极其重要**的一致性要求：
>
> Asset Manager 扫描磁盘建登记表时，是根据 `DefaultGame.ini` 里 `PrimaryAssetTypesToScan` 的配置（`PrimaryAssetType="Potion"` + 目录 + 基类）来给资产编号的，编出来的类型是 `"Potion"`。
>
> 而对象自己的 `GetPrimaryAssetId()` 也得返回 `"Potion"`。**两边必须对得上**——"管理员登记的货号"和"货物自己报的货号"要一致，否则按货号去查就查不到。
>
> 这正是 `URPGItem` 必须把 `ItemType` 用进 `GetPrimaryAssetId()` 的原因：如果用引擎默认的类名，对象会报 `RPGPotionItem:...`，而登记表里是 `Potion:...`，两边对不上，加载就会神秘失败。这也解释了阶段四正文那条"三处必须同步改"为什么成立——本质是在保证这个一致性。

### 一张图收尾

```
DefaultEngine.ini: AssetManagerClassName=.../RPGAssetManager
        │ 引擎启动时据此 new 出唯一的管理员
        ▼
   URPGAssetManager（管理员，全局单例）
        │ 读 DefaultGame.ini 的 PrimaryAssetTypesToScan
        │ 扫描 Content/Items/* 目录，建登记表
        ▼
   登记表：FPrimaryAssetId → 磁盘路径
        ▲                       │ 按货号 ForceLoadItem / 异步加载
        │ 两边货号必须一致        ▼
   URPGItem::GetPrimaryAssetId() ← 物品自报货号(ItemType + 名字)
   （继承自 UPrimaryDataAsset，正是它把自己变成"主要资产"）
```

`UAssetManager` 和 `UPrimaryDataAsset` 的关系一句话：**`UPrimaryDataAsset` 通过 override `GetPrimaryAssetId()` 把自己注册成"可被管理的主要资产"，`UAssetManager` 则按这个货号去扫描、索引、加载、卸载它。** 接头暗号就是 `GetPrimaryAssetId()`。

---

## 五、Asset Manager 到底有哪些用途

正文主要讲了"按需加载物品"，但 Asset Manager 的职责远不止此。看引擎里它的类注释（`Engine/Classes/Engine/AssetManager.h`）：

```cpp
/**
 * A singleton UObject that is responsible for loading and unloading PrimaryAssets,
 * and maintaining game-specific asset references
 */
```

把它的本事摊开，大致这几类（每条都标了对应的 API，方便你去翻源码）：

1. **扫描与索引**——启动时扫你声明的目录，建立"货号 → 路径"登记表。
   `ScanPathsForPrimaryAssets`。这是一切的地基，且**只建索引、不加载内容**。

2. **按货号异步加载 / 卸载**——这是它最常用的本事，也是移动端的命根子。
   `LoadPrimaryAssets` / `UnloadPrimaryAssets`。能异步在后台慢慢加载，加载完回调通知你，不卡主线程。（正文里的 `ForceLoadItem` 是同步应急通道，是这套的"简化粗暴版"。）

3. **Bundle（捆绑）状态管理**——同一个主要资产，按用途分组、只加载需要的那组。
   `ChangeBundleStateForPrimaryAssets`。比如只加载物品的"UI 展示部分"，先不加载"3D 模型部分"，省内存。

4. **维持引用、防止被 GC**——加载后返回一个句柄(handle)，只要你攥着它，资产就不会被回收。
   `GetPrimaryAssetHandle`。这正好解释了正文那句"`ForceLoadItem` 不持有引用，所以会被 GC 回收"——因为它走的不是这套句柄机制。

5. **反查**——给个对象/路径/包名，反过来问"它的货号是多少"。
   `GetPrimaryAssetIdForObject` / `...ForPath` / `...ForPackage`。

6. **打包（Cook）控制**——决定哪些资产要打进最终的包。
   配置里的 `CookRule=AlwaysCook` 就是交给它执行的。因为主要资产是动态按货号加载的，打包时的静态分析经常发现不了谁引用了它们，必须强制打进去，否则真机上缺资源。

7. **分块（Chunk）与 DLC / 补丁**——把资产分配到不同的 pak 文件，支持按需下载、做 DLC、做热更补丁。
   配置里的 `ChunkId`，以及 `ExtractChunkIdFromPrimaryAssetId` 之类接口。手游"先下小包、进游戏再下后续内容"就靠这个。

8. **编辑器内的资产审计**——编辑器有个 **Asset Audit** 窗口，能查每个主要资产的大小、被谁引用、归到哪个 chunk，方便排查"包怎么这么大"。

把这 8 条归一下，Asset Manager 其实就管两件大事：

> **运行时**：按货号、按需、异步地加载和卸载主要资产，省内存（第 1~5 条）。
> **打包时**：决定哪些资产进包、怎么分块，支撑 DLC 和热更（第 6~8 条）。

这两件事，全是冲着"一个有成百上千资产、还要塞进手机"的项目去的。这也是阶段四正文反复强调的那句"区别于教学 demo"的底气所在。

---

## 六、串起来看：启动时它到底干了什么（配置 → 扫描入册 → 按需加载）

前面几节是静态概念，这节把一件具体物品 `Potion_Health` 从"配置在哪"到"怎么进内存"的完整链路追一遍。先记住一条最容易踩的混淆点：

> **"扫描入册"和"真正加载内容"是两个完全不同的阶段。** 把这俩分开，整条链路就清晰了。

### 资产目录在哪配的

两个文件、两件事：

1. **先指定用哪个管理员**——`Config/DefaultEngine.ini`：
   ```ini
   AssetManagerClassName=/Script/ActionRPG.RPGAssetManager
   ```
   引擎启动时据此 new 出唯一的 `URPGAssetManager`。

2. **再告诉管理员去哪个目录扫**——`Config/DefaultGame.ini` 的 `[/Script/Engine.AssetManagerSettings]` 段，Potion 那条：
   ```ini
   +PrimaryAssetTypesToScan=(PrimaryAssetType="Potion",AssetBaseClass=/Script/ActionRPG.RPGPotionItem,...,Directories=((Path="/Game/Items/Potions")),...)
   ```
   `Directories=((Path="/Game/Items/Potions"))` 就是答案。`/Game` 等于磁盘上的 `Content/`，所以对应 `Content/Items/Potions/`，`Potion_Health.uasset` 正躺在那。

注意：**没有任何代码或配置点名引用 `Potion_Health`**，它纯靠"扫目录"被发现。

### 阶段 A：扫描入册（建登记表，**不读内容**）

引擎启动时的调用栈（除标注外都在 `Engine/Private/AssetManager.cpp`）：

```
URPGAssetManager::StartInitialLoading()          // 项目 override，先调 Super
  └─ UAssetManager::StartInitialLoading()        // 3265
       └─ ScanPrimaryAssetTypesFromConfig()      // 3267
            └─ 遍历 Settings.PrimaryAssetTypesToScan，对 Potion 这条 →
            └─ ScanPathsForPrimaryAssets("Potion", ["/Game/Items/Potions"], RPGPotionItem, ...)  // 3003
                 ├─ SearchAssetRegistryPaths(...)               // 823：Asset Registry 扫目录
                 │     → 发现 Potion_Health.uasset，拿到它的 FAssetData（轻量元数据，不是完整对象）
                 ├─ ExtractPrimaryAssetIdFromData(Data, "Potion")  // 829 → 算出货号 Potion:Potion_Health
                 └─ UpdateCachedAssetData(货号, Data)            // 857：存进登记表
```

最后那步 `UpdateCachedAssetData`（924）就是真正的"入册"：

```cpp
FPrimaryAssetData& NameData = TypeData.AssetMap.FindOrAdd(PrimaryAssetId.PrimaryAssetName);
NameData.AssetDataPath = NewAssetData.ObjectPath;        // 972：记下路径（不带 _C）
NameData.AssetPtr      = FSoftObjectPtr(NewAssetPath);   // 973：一个【软引用】，仍未加载
```

> **划重点：整个阶段 A 只读了 `FAssetData`（元数据）、记了一个软引用路径，`Potion_Health` 的内容此刻完全没进内存。** 登记表存的是 `AssetTypeMap["Potion"].AssetMap["Potion_Health"] → 路径`。这正呼应第五节那句"扫描只建索引、不加载内容"。

那货号 `Potion:Potion_Health` 怎么算出来的？看 `ExtractPrimaryAssetIdFromData`（1388）：

```cpp
FPrimaryAssetId FoundId = AssetData.GetPrimaryAssetId();   // 先读 .uasset 里缓存的货号标签
if (!FoundId.IsValid() && bShouldGuessTypeAndName && SuggestedType != NAME_None) {
    // 编辑器里若标签缺失，则兜底：用配置里的类型 + 资产文件名
    return FPrimaryAssetId(SuggestedType /*=Potion*/, AssetData.AssetName /*=Potion_Health*/);
}
```

而那个"缓存的货号标签"，值正来自 `URPGItem::GetPrimaryAssetId() = FPrimaryAssetId(ItemType, GetFName())`。这就跟第四节"两边货号必须一致"接上了——管理员算的 `Potion:Potion_Health` 必须和物品自报的一致。

### 阶段 B：真正加载（用的时候才读盘）

直到某段代码拿货号点名加载，内容才进内存（`RPGAssetManager.cpp:35`）：

```cpp
URPGItem* URPGAssetManager::ForceLoadItem(const FPrimaryAssetId& PrimaryAssetId, ...) {
    FSoftObjectPath ItemPath = GetPrimaryAssetPath(PrimaryAssetId);  // 查登记表拿软引用路径
    URPGItem* LoadedItem = Cast<URPGItem>(ItemPath.TryLoad());       // 这一刻才从磁盘读进内存
    ...
}
```

`GetPrimaryAssetPath`（1288）去登记表取阶段 A 存的那个软引用；`TryLoad()` 才真正读盘，并顺带把它引用的次要资产（图标贴图等）一起加载。

一句话总结这条链路：

> 目录在 `DefaultGame.ini` 的 `Directories` 配；`Potion_Health` 在 `StartInitialLoading` 里被**扫目录发现并入册**，入册时只记"货号 → 软引用路径"、**内容没加载**；真正内容要等 `ForceLoadItem`（或异步加载）按货号点名时，`TryLoad()` 才读进内存。

---

## 七、DefaultGame.ini 会被打包进游戏吗

会。**它必须随包发布，因为成品游戏运行时还要读它。**

### 为什么必须打包进去

上面那段 `PrimaryAssetTypesToScan` 在打好的游戏里**仍然是运行时读取的**——`ScanPrimaryAssetTypesFromConfig()` 里：

```cpp
const UAssetManagerSettings& Settings = GetSettings();   // 运行时从 config 读
for (FPrimaryAssetTypeInfo TypeInfo : Settings.PrimaryAssetTypesToScan) { ... }
```

打包后的游戏启动一样跑 `StartInitialLoading` → 读这份配置建类型表。配置丢了，AssetManager 就不知道有 Potion/Skill/Weapon 这些类型。所以它不能被丢掉。

### 但不是"原样照搬整个文件夹"

UE 的配置是**分层合并**的：

```
Engine/Config/Base*.ini                              (引擎基础)
  → <Project>/Config/Default*.ini                    (你写的，如 DefaultGame.ini)
    → <Project>/Config/<Platform>/<Platform>Game.ini (平台覆盖)
      → 运行时/用户存档层
```

打包（cook + stage）时，UAT 会把项目 `Config/` 下的 `Default*.ini` 连同引擎层、平台层一起打进最终包，文件名保持不变，仍叫 `DefaultGame.ini`。

### 它落在包里的哪个位置

取决于打包选项 **Use Pak File**（默认开）：

- **开启 Pak（默认）**：配置被塞进 `.pak`，路径形如 `../../../ActionRPG/Config/DefaultGame.ini`。用 `UnrealPak -list` 或 pak 浏览工具能看到这些 `Config/*.ini` 条目。
- **关闭 Pak（loose files）**：以散文件形式留在 `WindowsNoEditor/ActionRPG/Config/DefaultGame.ini`。

### 跟逆向 / Mod 相关的一点

这些 `.ini` **默认是明文**，没有加密（除非额外开了 pak 加密签名）：

- 解包 `.pak` 就能直接读到 `DefaultGame.ini`、看到 `AssetManagerSettings` 等结构，对分析一个 UE 游戏的资产组织很有用。
- loose 模式下玩家甚至能直接改它（很多 UE 游戏的简单 Mod / 作弊就是改散落的 ini）；pak 模式下要改就得重打包或用 Mod pak 覆盖。

一句话：**`DefaultGame.ini` 会随包发布**，默认在 `.pak` 内的 `ActionRPG/Config/` 下，明文、运行时被读取——`AssetManager` 的类型扫描配置正是靠它在成品游戏里依然生效。

---

## 八、动手验证：怎么触发这些查询接口

想在源码里实测 Asset Manager 的"资产列表"（`GetPrimaryAssetIdList` 等）和"反查"（`GetPrimaryAssetIdForObject` / `...ForPath`），关键不是函数怎么调，而是**在什么时机触发**。

### 前提：等扫描完成再查

这些接口都依赖登记表 `AssetTypeMap` 已建好，而它是在 `StartInitialLoading → ScanPrimaryAssetTypesFromConfig` 里填的。所以：

- ❌ 别在构造函数、CDO、`StartupModule` 里查——那时还没扫，结果为空。
- ⚠️ **编辑器里尤其注意**：编辑器启动时 Asset Registry 还在异步加载，`ScanPathsForPrimaryAssets` 会把扫描**推迟**（走 `DeferredAssetScanPaths` 分支）。所以编辑器里 `StartInitialLoading` 那一刻查，列表可能**不全甚至为 0**；cooked 包里 registry 是预烘焙的，反而立刻就全。
- ✅ 结论：**等世界跑起来后用控制台命令触发最稳**，PIE / 独立进程 / 真机一致。

### 第一档：引擎自带命令（零代码）

UE 已把"列资产"做成控制台命令（`AssetManager.cpp:2734` 起注册），按 `~` 直接敲：

| 命令 | 作用 |
|------|------|
| `AssetManager.DumpTypeSummary` | 列出每种类型：类、数量、扫描路径 |
| `AssetManager.DumpLoadedAssets` | 列出当前已加载的主要资产 + bundle 状态 |
| `AssetManager.DumpReferencersForPackage <包名>` | 反查"谁引用了这个包" |
| `AssetManager.LoadPrimaryAssetsWithType <类型>` | 按类型批量加载（测异步） |

> **踩坑实录：在编辑器里敲这些命令"没有任何输出"。** 99% 不是命令没生效，而是**输出位置 / 控制台搞错了**，三点排查：
>
> 1. 这些命令用 `UE_LOG(LogAssetManager, Log, ...)` 打印，**只进 Output Log，不会显示在视口、也不弹屏**。先打开 **Window → Output Log**，再在过滤框搜 `Asset Manager Type Summary` 或 `LogAssetManager`。
> 2. **不在 PIE 时，视口按 `~` 是没有控制台的**。要么先 Play(PIE) 再按 `~` 敲；要么直接用 **Output Log 面板底部那个命令输入框**（`Cmd>`）敲。
> 3. 这些命令是 `ECVF_Cheat`：Development/Editor 能用，Test/Shipping 被 cheat 门禁挡掉。
>
> 一句话：命令没问题，去 **Output Log** 找输出，别盯着视口。

内置命令能看列表，但**没有**"给一个对象/路径反查货号"的——那要自己写。

### 第二档（推荐）：自己加一条 Exec 控制台命令

测**反查**最合理：世界已就绪、对象已加载、可重复传参。本项目挂在 `ARPGPlayerControllerBase` 上（PlayerController 默认接收 exec 路由）。

头文件 `RPGPlayerControllerBase.h`：

```cpp
/** 控制台测试：TestAssetManager Potion（留空遍历所有类型）*/
UFUNCTION(Exec)
void TestAssetManager(FString TypeName);
```

实现 `RPGPlayerControllerBase.cpp`（需 `#include "Engine/AssetManager.h"`、`"RPGAssetManager.h"`、`"Engine/Engine.h"`）：

```cpp
void ARPGPlayerControllerBase::TestAssetManager(FString TypeName)
{
    UAssetManager& Manager = UAssetManager::Get();

    // 同时打到 Output Log 和屏幕，避免"看不到输出"
    auto LogLine = [](const FString& Msg)
    {
        UE_LOG(LogActionRPG, Display, TEXT("%s"), *Msg);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 12.f, FColor::Cyan, Msg);
    };

    TArray<FPrimaryAssetType> TypesToTest;
    if (TypeName.IsEmpty())
    {
        TArray<FPrimaryAssetTypeInfo> TypeInfos;
        Manager.GetPrimaryAssetTypeInfoList(TypeInfos);     // 拿到所有类型
        for (const FPrimaryAssetTypeInfo& Info : TypeInfos) TypesToTest.Add(Info.PrimaryAssetType);
    }
    else
    {
        TypesToTest.Add(FPrimaryAssetType(*TypeName));
    }

    for (const FPrimaryAssetType& Type : TypesToTest)
    {
        // ① 列表
        TArray<FPrimaryAssetId> IdList;
        Manager.GetPrimaryAssetIdList(Type, IdList);
        LogLine(FString::Printf(TEXT("[List] '%s' 共 %d 个："), *Type.ToString(), IdList.Num()));
        for (const FPrimaryAssetId& Id : IdList)
            LogLine(FString::Printf(TEXT("    %s -> %s"), *Id.ToString(), *Manager.GetPrimaryAssetPath(Id).ToString()));

        // ② 反查（先加载，再从对象/路径反查回货号）
        if (IdList.Num() > 0)
        {
            URPGItem* Item = URPGAssetManager::Get().ForceLoadItem(IdList[0]);
            if (Item)
            {
                FPrimaryAssetId ById   = Manager.GetPrimaryAssetIdForObject(Item);
                FPrimaryAssetId ByPath = Manager.GetPrimaryAssetIdForPath(FSoftObjectPath(Item));
                LogLine(FString::Printf(TEXT("[Reverse] %s -> ById=%s ByPath=%s"),
                    *Item->GetName(), *ById.ToString(), *ByPath.ToString()));
            }
        }
    }
}
```

游戏里 PIE → 按 `~` → 敲 `TestAssetManager Potion`（或留空 `TestAssetManager` 测全部）。结果同时进 Output Log 和屏幕。

> **反查的前提**：`GetPrimaryAssetIdForObject` 只对**已加载**且**已注册为主要资产**的对象有效，所以先 `ForceLoadItem` 再反查；对没加载的对象反查会拿到无效货号——这本身也值得验证一把。

### 第三档：在 `StartInitialLoading` 末尾埋点（只测列表）

```cpp
void URPGAssetManager::StartInitialLoading()
{
    Super::StartInitialLoading();   // ← 扫描在 Super 里完成，务必放它之后
    UAbilitySystemGlobals::Get().InitGlobalData();
#if !UE_BUILD_SHIPPING
    TArray<FPrimaryAssetId> IdList;
    GetPrimaryAssetIdList(PotionItemType, IdList);   // 这里能直接用类型常量
    UE_LOG(LogActionRPG, Display, TEXT("[Boot] Potion 数量 = %d"), IdList.Num());
#endif
}
```

缺点见"前提"：**编辑器里此刻扫描可能未完、数量偏少甚至为 0**，cooked 包才准；且这时对象还没加载，**别用它测反查**。

### 选型一句话

- 只看**列表** → 内置 `AssetManager.DumpTypeSummary`（记得去 Output Log 找输出）。
- 测**反查** / 要可重复传参 → Exec 命令（第二档），最合理。
- 看**启动时点** → `StartInitialLoading` 末尾埋点，只测列表、知道编辑器下可能不全。

---

## 九、几个问题，一句话各自收口

- **有没有 Secondary？** 概念上有"次要资产"，代码里没有 `FSecondaryAssetType`。主要资产靠货号手动管，次要资产靠引用关系图自动跟随，故意不对称。
- **Primary Data Asset 是什么？** 一个 override 了 `GetPrimaryAssetId()`、因而能被 Asset Manager 按货号管理的数据资产。
- **二者源码关系？** 接头暗号是 `GetPrimaryAssetId()`：`UPrimaryDataAsset` 用它把自己变成主要资产，`UAssetManager` 用它来索引和加载。两边报的货号必须一致。
- **Asset Manager 有哪些用途？** 运行时按需异步加载/卸载省内存，打包时控制 cook 与分块支撑 DLC/热更——全为"海量资产 + 上手机"服务。
- **物品怎么进 AssetManager？** 启动时 `StartInitialLoading` 扫 `Directories` 配的目录，把"货号 → 软引用路径"入册（不读内容）；用时再 `ForceLoadItem` 按货号读盘。
- **DefaultGame.ini 会打包吗？** 会，默认进 `.pak` 内 `Config/` 下、明文、运行时读取，类型扫描配置靠它在成品里生效。
- **怎么实测这些查询接口？** 等扫描完成后用控制台触发最稳：看列表用内置 `AssetManager.DumpTypeSummary`（输出在 Output Log），测反查写一条 `UFUNCTION(Exec)`。编辑器里"没输出"多半是没看 Output Log / 没在 PIE 里开控制台。

读完这篇再回去看阶段四正文那条"三处必须同步改"，你会发现它不再是死记硬背的规则，而是"保证登记表货号和物品自报货号一致"的必然结果。
