# 阶段四：用 ActionRPG 搞懂 UE 的 Asset Manager 与 Primary Data Assets

接着学习计划往下走。前三个阶段你已经能编译、能读蓝图、能在 C++ 基类和蓝图子类之间来回跳了。这一篇换个主题，聊聊 ActionRPG 里所有"物品"是怎么组织和加载的。说实话，这块是这个示例项目最有工程价值的地方之一，也是它跟那些"教学 demo"拉开差距的地方。

我假设你有 C++ 底子，但没正经做过 UE 项目。所以下面会从"为什么要这么干"讲起，再贴真实代码，最后带你动手加一个新物品类型。动手那一节最重要，因为有个坑我自己踩过，新手几乎都会踩。

## 先说为什么需要 Asset Manager

你写过普通 C++ 程序，加载一个资源大概就是直接 `new` 一下、或者从磁盘读个文件。这套思路放到 UE 移动端 RPG 上会出大问题。

一个 RPG 有几百上千件物品：药水、技能、武器、各种代币。每件物品身上挂着图标、描述文本、绑定的能力、模型引用。如果游戏一启动就把这些全加载进内存，手机直接卡死给你看。可你又不能完全不管它，因为玩家随时可能开背包、捡装备、用技能。

所以引擎推荐的做法是两条腿走路。第一，数据驱动，物品的所有参数都做成"数据资产"，由设计师在编辑器里填，不写死在代码里。第二，按需异步加载，只在真正要用某件物品的时候才把它的资源拉进内存，用完还能让 GC 回收掉。

Asset Manager 就是干这个的。它在引擎启动时扫描你声明的那些资产目录，建立一张"我知道哪里有哪些物品"的索引表，但**不立刻加载内容**。等你真要用了，再通过它去加载。这张索引表里每件物品都有个唯一身份证，叫 `FPrimaryAssetId`，全项目都靠它来指代一件物品，而不是直接拿裸指针。

ActionRPG 把引擎自带的 `UAssetManager` 继承了一层，做成 `URPGAssetManager`，加了点游戏自己的东西。配置上是这么挂的（在 `Config/DefaultEngine.ini` 里）：

```ini
AssetManagerClassName=/Script/ActionRPG.RPGAssetManager
```

注意这一条直接指向 C++ 类，不是蓝图子类。前面阶段二讲过本项目大多数基类都被蓝图化了再引用，资产管理器是个例外，它不需要设计师改什么，所以就直接用 C++ 类本身。

## URPGItem：抽象的数据资产基类

物品的根基类是 `URPGItem`，定义在 `Source/ActionRPG/Public/Items/RPGItem.h`。它继承自 `UPrimaryDataAsset`，而且是抽象的，注释里写得很直白，别直接蓝图化它：

```cpp
/** Base class for all items, do not blueprint directly */
UCLASS(Abstract, BlueprintType)
class ACTIONRPG_API URPGItem : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** Type of this item, set in native parent class */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Item)
    FPrimaryAssetType ItemType;          // 物品类型，由具体 C++ 子类在构造函数里设定

    /** User-visible short name */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Item)
    FText ItemName;                       // 显示名

    /** Icon to display */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Item)
    FSlateBrush ItemIcon;                 // 图标

    /** Ability to grant if this item is slotted */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Abilities)
    TSubclassOf<URPGGameplayAbility> GrantedAbility;   // 物品装到槽位时授予的能力
    // ... Price / MaxCount / MaxLevel / AbilityLevel 等省略
};
```

这里有两个东西要盯住。

`FPrimaryAssetType ItemType` 是这件物品属于哪一类。它不在数据资产里手填，而是由具体子类在 C++ 构造函数里写死。`UPROPERTY` 上是 `VisibleAnywhere`，意思是编辑器里能看见但改不了。

再看文件最底下这个 override，理解它你就懂了"身份证"是怎么拼出来的：

```cpp
FPrimaryAssetId URPGItem::GetPrimaryAssetId() const
{
    // 这是 DataAsset 不是蓝图，所以直接用原始 FName 就行
    // 如果是蓝图，还要处理掉 _C 后缀
    return FPrimaryAssetId(ItemType, GetFName());
}
```

一个 `FPrimaryAssetId` 由两部分拼成：类型（比如 `Potion`）加上这个资产自己的名字。所以一瓶叫 `Potion_Health` 的药水，它的 id 就是 `Potion:Potion_Health`。全项目要引用这瓶药水，传这个 id 就够了，不需要持有它的指针，也不需要它已经被加载。这点对移动端很关键。

## 四个子类，四种类型

`URPGItem` 自己是抽象的，真正能用的是四个具体子类。它们都在 `Source/ActionRPG/Public/Items/` 下，每个文件都短得可怜，核心就是在构造函数里给 `ItemType` 赋个值。

药水（`RPGPotionItem.h`）：

```cpp
UCLASS()
class ACTIONRPG_API URPGPotionItem : public URPGItem
{
    GENERATED_BODY()
public:
    URPGPotionItem()
    {
        ItemType = URPGAssetManager::PotionItemType;   // 类型 = "Potion"
    }
};
```

技能（`RPGSkillItem.h`）几乎一样，只是类型换成 `SkillItemType`。代币（`RPGTokenItem.h`）多了一行，因为代币应该能无限叠加：

```cpp
URPGTokenItem()
{
    ItemType = URPGAssetManager::TokenItemType;
    MaxCount = 0; // Infinite，0 表示数量无上限
}
```

武器（`RPGWeaponItem.h`）比别的多一个属性，因为武器需要在世界里生成一个 actor：

```cpp
UCLASS()
class ACTIONRPG_API URPGWeaponItem : public URPGItem
{
    GENERATED_BODY()
public:
    URPGWeaponItem()
    {
        ItemType = URPGAssetManager::WeaponItemType;   // 类型 = "Weapon"
    }

    /** Weapon actor to spawn */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Weapon)
    TSubclassOf<AActor> WeaponActor;                   // 装备时要生成的武器 actor
};
```

看出门道了吧。子类自己几乎不写逻辑，它存在的全部意义就是声明"我是一个独立的 `FPrimaryAssetType`"，顺手调一两个默认值。这就是数据驱动的味道，类型靠 C++ 划清楚，具体每一件物品的数值靠数据资产去填。

那 `PotionItemType` 这些常量到底是啥？它们是 `URPGAssetManager` 上的静态成员，声明在 `RPGAssetManager.h`：

```cpp
/** Static types for items */
static const FPrimaryAssetType	PotionItemType;
static const FPrimaryAssetType	SkillItemType;
static const FPrimaryAssetType	TokenItemType;
static const FPrimaryAssetType	WeaponItemType;
```

真正的字符串值在 `RPGAssetManager.cpp` 里定义：

```cpp
const FPrimaryAssetType	URPGAssetManager::PotionItemType = TEXT("Potion");
const FPrimaryAssetType	URPGAssetManager::SkillItemType  = TEXT("Skill");
const FPrimaryAssetType	URPGAssetManager::TokenItemType  = TEXT("Token");
const FPrimaryAssetType	URPGAssetManager::WeaponItemType = TEXT("Weapon");
```

为啥不把字符串直接写在每个子类里？因为这样四个子类、资产管理器内部的加载逻辑、还有别处的代码，全都引用同一份常量，改一个地方就够了，不会出现一边写 "Potion" 一边写 "potion" 的对不上。

## 注册：让引擎知道去哪儿扫

光有 C++ 类型还不够。引擎启动时怎么知道"Potion 这种东西要去哪个目录找"？答案在 `Config/DefaultGame.ini` 的 `[/Script/Engine.AssetManagerSettings]` 段。每一种物品类型对应一条 `PrimaryAssetTypesToScan`：

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(PrimaryAssetType="Potion",AssetBaseClass=/Script/ActionRPG.RPGPotionItem,bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/Items/Potions")),SpecificAssets=,Rules=(Priority=-1,bApplyRecursively=True,ChunkId=-1,CookRule=AlwaysCook))
+PrimaryAssetTypesToScan=(PrimaryAssetType="Skill",AssetBaseClass=/Script/ActionRPG.RPGSkillItem,...,Directories=((Path="/Game/Items/Skills")),...,Rules=(...,CookRule=AlwaysCook))
+PrimaryAssetTypesToScan=(PrimaryAssetType="Token",AssetBaseClass=/Script/ActionRPG.RPGTokenItem,...,Directories=((Path="/Game/Items/Tokens")),...,Rules=(...,CookRule=AlwaysCook))
+PrimaryAssetTypesToScan=(PrimaryAssetType="Weapon",AssetBaseClass=/Script/ActionRPG.RPGWeaponItem,...,Directories=((Path="/Game/Items/Weapons")),...,Rules=(...,CookRule=AlwaysCook))
```

逐字段拆开看：

- `PrimaryAssetType="Potion"`，这串字符串必须跟 C++ 里 `PotionItemType` 的值一模一样，引擎靠它把配置和类型对上号。
- `AssetBaseClass=/Script/ActionRPG.RPGPotionItem`，指向对应的 C++ 子类。
- `Directories=((Path="/Game/Items/Potions"))`，扫描目录。注意 `/Game` 在引擎里等于 `Content/` 文件夹，所以这条对应磁盘上的 `Content/Items/Potions`。
- `CookRule=AlwaysCook`，打包（cook）时总是把这些资产打进去。物品是按 id 动态加载的，cook 阶段静态分析往往发现不了谁引用了它们，不强制 AlwaysCook 的话打出来的包会缺资源。

行首那个 `+` 是 ini 的数组追加语法，表示往 `PrimaryAssetTypesToScan` 这个列表里再加一项，而不是覆盖。

到这里整条链就闭合了。引擎读 ini 知道要扫 `Content/Items/Potions`，扫到的资产按 `RPGPotionItem` 类型解释，每个资产的 id 是 `Potion:资产名`。

## 加载：同步省事，但会卡

资产被扫描到只是建了索引，内容还没进内存。要真正拿到一件物品对象，看 `URPGAssetManager::ForceLoadItem`：

```cpp
URPGItem* URPGAssetManager::ForceLoadItem(const FPrimaryAssetId& PrimaryAssetId, bool bLogWarning)
{
    FSoftObjectPath ItemPath = GetPrimaryAssetPath(PrimaryAssetId);

    // 这是同步加载，可能会卡顿（hitch）
    URPGItem* LoadedItem = Cast<URPGItem>(ItemPath.TryLoad());

    if (bLogWarning && LoadedItem == nullptr)
    {
        UE_LOG(LogActionRPG, Warning, TEXT("Failed to load item for identifier %s!"), *PrimaryAssetId.ToString());
    }
    return LoadedItem;
}
```

这函数很直接：拿 id 换路径，`TryLoad()` 同步加载，加载失败打个警告。它的头文件注释把权衡说得很清楚，照搬过来：

> Synchronously loads an RPGItem subclass, this can hitch but is useful when you cannot wait for an async load. This does not maintain a reference to the item so it will garbage collect if not loaded some other way.

两个关键点。一，它是**同步**的，加载这件物品时整个游戏线程就停在这儿等磁盘，物品资源大或者一次加载一堆，画面就会卡一下（hitch）。二，它**不持有引用**，函数返回后如果没有别的地方（比如背包的 `UPROPERTY` 指针）抓住这个对象，下次 GC 就把它回收了。

那什么时候该用同步、什么时候用异步？经验上是这样的。需要立刻拿到结果、又确定卡顿可接受的场合用同步，比如某些编辑器工具、或者已经在加载屏后面、玩家本来就在等的初始化阶段。正常游戏过程里能提前预测的加载，比如进图前预载这张图会用到的物品，应该走异步，让加载在后台慢慢做，做完回调通知你。Asset Manager 提供了异步加载接口（`LoadPrimaryAssets` 之类），ActionRPG 在背包和存档流程里用的就是异步那套。`ForceLoadItem` 是个"应急通道"，名字里那个 `Force` 就是提醒你这是有代价的。

移动端尤其要把这条记牢。手机磁盘慢、内存小，能异步就别同步，能晚加载就别早加载，用完让它被 GC 回收掉。这也是前面说的"区别于教学 demo"的地方，demo 才会图省事一股脑全同步加载。

## 动手练习：加一个新物品类型

理论说完了，下面是这阶段的重头戏。我们加一种新物品类型，假设叫 **护甲（Armor）**。这个练习的目的不是真做出能用的护甲，而是让你亲身体会一条铁律：

> 加一个新物品类型，必须同步改三个地方，漏任何一处都扫不到。

我第一次做的时候就漏了第三处（ini 那条），C++ 编译得好好的，结果编辑器里死活看不到资产、加载永远失败，对着代码看了半天才反应过来扫描配置根本没注册。所以下面三步一步都别少。

### 第一处：新建 C++ 子类

照着 `RPGWeaponItem.h` 抄一份，新建 `Source/ActionRPG/Public/Items/RPGArmorItem.h`：

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Items/RPGItem.h"
#include "RPGArmorItem.generated.h"

/** Native base class for armor, should be blueprinted */
UCLASS()
class ACTIONRPG_API URPGArmorItem : public URPGItem
{
    GENERATED_BODY()

public:
    /** Constructor */
    URPGArmorItem()
    {
        ItemType = URPGAssetManager::ArmorItemType;   // 用下一步要加的新常量
    }
};
```

这里引用了 `URPGAssetManager::ArmorItemType`，现在还不存在，等第二步补上。

### 第二处：在 RPGAssetManager 加类型常量

先在头文件 `Source/ActionRPG/Public/RPGAssetManager.h` 的静态常量那一组里加一行声明：

```cpp
/** Static types for items */
static const FPrimaryAssetType	PotionItemType;
static const FPrimaryAssetType	SkillItemType;
static const FPrimaryAssetType	TokenItemType;
static const FPrimaryAssetType	WeaponItemType;
static const FPrimaryAssetType	ArmorItemType;   // 新增
```

再到 `Source/ActionRPG/Private/RPGAssetManager.cpp` 里给它定义值：

```cpp
const FPrimaryAssetType	URPGAssetManager::PotionItemType = TEXT("Potion");
const FPrimaryAssetType	URPGAssetManager::SkillItemType  = TEXT("Skill");
const FPrimaryAssetType	URPGAssetManager::TokenItemType  = TEXT("Token");
const FPrimaryAssetType	URPGAssetManager::WeaponItemType = TEXT("Weapon");
const FPrimaryAssetType	URPGAssetManager::ArmorItemType  = TEXT("Armor");   // 新增
```

记住这个 `"Armor"` 字符串，下一步 ini 里要用一模一样的值。

### 第三处：在 DefaultGame.ini 加扫描条目

这就是我当年漏掉的那处。打开 `Config/DefaultGame.ini`，在 `[/Script/Engine.AssetManagerSettings]` 段里照着 Weapon 那条加一行：

```ini
+PrimaryAssetTypesToScan=(PrimaryAssetType="Armor",AssetBaseClass=/Script/ActionRPG.RPGArmorItem,bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/Items/Armors")),SpecificAssets=,Rules=(Priority=-1,bApplyRecursively=True,ChunkId=-1,CookRule=AlwaysCook))
```

对照检查三件事：`PrimaryAssetType="Armor"` 要跟第二步的字符串完全一致；`AssetBaseClass` 指向第一步的 `RPGArmorItem`；`Directories` 指向 `/Game/Items/Armors`，也就是磁盘上的 `Content/Items/Armors`，这个目录待会儿在编辑器里建资产时会用到。

三处都改完，重新编译编辑器目标：

```powershell
& "<UE_4.27>\Engine\Build\BatchFiles\Build.bat" ActionRPGEditor Win64 Development "<repo>\ActionRPG.uproject" -waitmutex
```

### 到编辑器里建一个 Armor 数据资产

编译通过、打开编辑器后：

1. 在内容浏览器里进到 `Content/Items/`，新建一个文件夹 `Armors`（要和 ini 里的 `Directories` 路径对上）。
2. 在这个文件夹里右键，选 Miscellaneous → Data Asset（不同版本菜单位置略有差别，找"数据资产"就对了）。
3. 弹出选类的对话框，搜 `RPGArmorItem`，选它。能在列表里看到这个类，说明你的 C++ 子类已经被引擎认到了。
4. 给资产起个名，比如 `Armor_Iron`。双击打开，会看到从 `URPGItem` 继承下来的那些字段：ItemName、ItemDescription、ItemIcon、Price 之类，填几个值。注意 ItemType 那栏是灰的、显示 `Armor`、改不了，正好印证了它由 C++ 构造函数写死、`VisibleAnywhere` 只读。

建好之后，这个 `Armor_Iron` 的 `FPrimaryAssetId` 就是 `Armor:Armor_Iron`，Asset Manager 启动扫描时会把它收进索引。这时候若用 `ForceLoadItem` 传这个 id，就能把它加载出来了。

如果你故意把第三步那条 ini 删掉再重启编辑器，会发现这个资产虽然文件还在，但资产管理器扫不到它，按 id 加载直接失败。这就是"三处缺一不可"最直观的验证。

## 验收清单

对着学习计划阶段四的验收标准，过一遍下面这些，能讲清楚就算过关：

- [ ] 能说清 `FPrimaryAssetId` 和 `FPrimaryAssetType` 分别是什么，以及 id 是怎么由 `类型 + 资产名` 拼出来的（看 `URPGItem::GetPrimaryAssetId`）。
- [ ] 能解释为什么要用 id 而不是裸指针来到处指代物品，这跟移动端的按需加载有什么关系。
- [ ] 能说清同步加载（`ForceLoadItem`，会卡、不持引用）和异步加载各自的适用场合与代价。
- [ ] 知道 `URPGItem` 是抽象基类，四个子类靠在构造函数里设 `ItemType` 来区分类型，类型字符串常量统一放在 `URPGAssetManager` 上。
- [ ] 理解 `DefaultGame.ini` 里 `PrimaryAssetTypesToScan` 每个字段的含义，尤其 `Directories` 和 `CookRule=AlwaysCook` 为什么这么设。
- [ ] 亲手加完一个新物品类型，三处（C++ 子类、`RPGAssetManager` 常量、`DefaultGame.ini` 条目）都改对，并在编辑器里成功建出一个该类型的数据资产。

做完这一阶段，你对"UE 怎么把成百上千件物品做成数据、又怎么省着内存按需加载"就有谱了。下一阶段会用到这里的 `URPGItem` 和 `FPrimaryAssetId`，去看背包系统怎么把它们存起来、又怎么异步持久化到存档。
