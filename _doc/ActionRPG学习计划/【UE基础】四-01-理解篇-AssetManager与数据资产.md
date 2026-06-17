# 阶段四·01 理解篇：Asset Manager 与数据资产的设计

> 阶段四拆成三篇，建议按顺序读：
> **01 理解篇（本篇）** —— 不碰深源码，先把"为什么 / 是什么"讲透。
> [02 源码篇](UE基础四-02-源码篇-从启动到加载.md) —— 从启动到加载，把链路连源码走一遍。
> [03 实战篇](UE基础四-03-实战篇-加类型与验证接口.md) —— 动手加物品类型、动手验证查询接口。

这一篇换个主题，聊聊 ActionRPG 里所有"物品"是怎么组织和加载的。说实话，这块是这个示例项目最有工程价值的地方之一，也是它跟那些"教学 demo"拉开差距的地方。本篇只讲设计动机和概念，读完你能看懂整套设计长什么样、为什么这么设计；具体源码和动手放到后两篇。

---

## 一、先说为什么需要 Asset Manager

你写过普通 C++ 程序，加载一个资源大概就是直接 `new` 一下、或者从磁盘读个文件。这套思路放到 UE 移动端 RPG 上会出大问题。

一个 RPG 有几百上千件物品：药水、技能、武器、各种代币。每件物品身上挂着图标、描述文本、绑定的能力、模型引用。如果游戏一启动就把这些全加载进内存，手机直接卡死给你看。可你又不能完全不管它，因为玩家随时可能开背包、捡装备、用技能。

所以引擎推荐的做法是两条腿走路。第一，**数据驱动**，物品的所有参数都做成"数据资产"，由设计师在编辑器里填，不写死在代码里。第二，**按需异步加载**，只在真正要用某件物品的时候才把它的资源拉进内存，用完还能让 GC 回收掉。

Asset Manager 就是干这个的。它在引擎启动时扫描你声明的那些资产目录，建立一张"我知道哪里有哪些物品"的索引表，但**不立刻加载内容**。等你真要用了，再通过它去加载。



> 先看一个图，整体了解接下来要做的事情：
> ![image-20260616184523805](J:\_ALL\CODE\codeup.aliyun.com\_jy\_ue4.27.2\ActionRPG\_doc\ActionRPG学习计划\UE基础四-01-理解篇-AssetManager与数据资产.assets\image-20260616184523805.png)



---

## 二、心智模型：仓库管理员 UAssetManager

后面所有概念都从这个比喻推得出来，先记牢它。

想象一个大仓库，堆着成千上万件东西。仓库雇了个**管理员**，这就是 **Asset Manager**。

但管理员没那么勤快——他**不会给每一件东西都建档**。他只给"重点货物"建一张**登记表**：每件重点货物给一个唯一**货号**，记下它放在哪个货架。至于重点货物的包装盒、说明书、配件这些**附属品**，他不单独登记，反正它们是跟着重点货物一起进出的。

翻译成 UE 术语：

| 比喻 | UE 概念 |
|------|---------|
| 仓库管理员 | `UAssetManager`（全局单例） |
| 重点货物 | **主要资产 Primary Asset** |
| 货号 | `FPrimaryAssetId`（如 `Potion:Potion_Health`） |
| 货物类别（药水/武器…） | `FPrimaryAssetType`（如 `Potion`） |
| 登记表 | 启动扫描后建立的索引 |
| 附属品（包装、配件） | **次要资产 Secondary Asset**（贴图、材质、模型、音效…） |

ActionRPG 把引擎自带的 `UAssetManager` 继承了一层做成 `URPGAssetManager`，配置上这么挂（`Config/DefaultEngine.ini`）：

```ini
AssetManagerClassName=/Script/ActionRPG.RPGAssetManager
```

注意这条直接指向 C++ 类，不是蓝图子类。阶段二讲过本项目大多数基类都被蓝图化了再引用，资产管理器是个例外——它不需要设计师改什么，所以直接用 C++ 类本身。

---

## 三、主要资产 vs 次要资产：有没有 Secondary？

**结论先行：概念上有"次要资产（Secondary Asset）"，但代码里没有 `FSecondaryAssetType` 这个类型，也不需要。**

UE 把所有资产分两类：

- **主要资产（Primary Asset）**：你想**主动、按名字去加载/卸载**的资产。它有货号（`FPrimaryAssetId`），管理员给它建了档。ActionRPG 里的药水、技能、武器、代币都是。
- **次要资产（Secondary Asset）**：其余一切——贴图、材质、网格、音效……它们**没有货号**，你不会写代码点名加载它们；它们是**因为被某个主要资产引用了，才被顺带加载进来**的。

举例：你加载 `Potion:Potion_Health`（一瓶血药的数据资产），它身上的 `ItemIcon`（图标贴图）就是次要资产。你从不单独加载那张图标，它跟着血药一起来、一起走。

为什么不需要 `FSecondaryAssetType`？因为次要资产根本**不靠"类型 + 名字"来管理**，引擎管它们用的是另一套机制——**资产引用关系图（Asset Registry 依赖图）**：

```
你：加载 Potion:Potion_Health（主要资产，按货号点名）
        │
        ▼
管理员：查登记表 → 找到磁盘路径 → 开始加载
        │
        ▼
引擎顺着"引用关系图"发现：这瓶药引用了 图标贴图、字体…（次要资产）
        │
        ▼
把这些次要资产一并加载进来（你没点名，引擎自动办了）
```

这是一种**故意的不对称**：主要资产是你**手动管理的入口**，少而精，需要稳定身份证；次要资产是**被动跟随的依赖**，多而杂，没必要也发身份证，靠依赖图自动收。

> 一句话：**你只需要给"入口"编号，剩下的让依赖关系自动兜底。** 这就是没有 `Secondary` 类型的根本原因。`FPrimaryAssetId` 只有"类型 + 名字"两个字段，整个引擎里也找不到 `FSecondaryAssetId`。（想看结构体源码佐证，见 [02 源码篇](UE基础四-02-源码篇-从启动到加载.md)。）

---

## 四、什么是 Primary Data Asset

理解了主次，这个就好讲了。先分清三个名字，别搞混：

- `UDataAsset`：最基础的"数据容器"基类。继承它就能在内容浏览器右键造一个纯数据资产（设计师填字段用）。但它**默认是次要资产**——没货号。
- `UPrimaryDataAsset`：`UDataAsset` 的子类。它做了关键一步——**让自己变成主要资产**（拿到货号）。
- `URPGItem`：ActionRPG 的物品基类，继承自 `UPrimaryDataAsset`。

`UPrimaryDataAsset` 比 `UDataAsset` 多的核心能力，就是 **override 了 `GetPrimaryAssetId()`**——这个函数返回一个有效货号，于是它从"次要"晋升成"主要"，能被管理员按货号点名加载/卸载。

所以：**"Primary Data Asset" = 一个会自报货号、因而能被 Asset Manager 管理的数据资产**。就这么简单。（这三层 `GetPrimaryAssetId` 是怎么一层层改写的，是源码篇的重点。）

`URPGItem` 定义在 `Source/ActionRPG/Public/Items/RPGItem.h`，注释里写得很直白，别直接蓝图化它：

```cpp
/** Base class for all items, do not blueprint directly */
UCLASS(Abstract, BlueprintType)
class ACTIONRPG_API URPGItem : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    /** Type of this item, set in native parent class */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Item)
    FPrimaryAssetType ItemType;          // 物品类型，由具体 C++ 子类构造函数设定

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Item)
    FText ItemName;                       // 显示名

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Item)
    FSlateBrush ItemIcon;                 // 图标

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Abilities)
    TSubclassOf<URPGGameplayAbility> GrantedAbility;   // 装到槽位时授予的能力
    // ... Price / MaxCount / MaxLevel / AbilityLevel 等省略
};
```

盯住 `FPrimaryAssetType ItemType`：它是这件物品属于哪一类，**不在数据资产里手填，而是由具体子类在 C++ 构造函数里写死**。`UPROPERTY` 上是 `VisibleAnywhere`——编辑器里能看见但改不了。

---

## 五、货号 `FPrimaryAssetId` = 类型 + 名字

理解"货号怎么拼出来"，整套设计就通了。`URPGItem` 在文件底部 override 了这个函数：

```cpp
FPrimaryAssetId URPGItem::GetPrimaryAssetId() const
{
    // 这是 DataAsset 不是蓝图，直接用原始 FName 就行
    // 如果是蓝图，还要处理掉 _C 后缀
    return FPrimaryAssetId(ItemType, GetFName());
}
```

一个 `FPrimaryAssetId` 由两部分拼成：**类型**（比如 `Potion`）加上**这个资产自己的名字**。所以一瓶叫 `Potion_Health` 的药水，货号就是 `Potion:Potion_Health`。

全项目要引用这瓶药水，传这个货号就够了——**不需要持有它的指针，也不需要它已经被加载**。这点对移动端很关键：背包、存档里存的都是货号这种轻量字符串身份证，而不是几 MB 的资产对象。

---

## 六、四个子类，四种类型

`URPGItem` 自己是抽象的，真正能用的是四个具体子类。它们都在 `Source/ActionRPG/Public/Items/` 下，每个文件都短得可怜，核心就是在构造函数里给 `ItemType` 赋值。

![image-20260616184559911](J:\_ALL\CODE\codeup.aliyun.com\_jy\_ue4.27.2\ActionRPG\_doc\ActionRPG学习计划\UE基础四-01-理解篇-AssetManager与数据资产.assets\image-20260616184559911.png)

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

技能（`RPGSkillItem.h`）几乎一样，类型换成 `SkillItemType`。代币（`RPGTokenItem.h`）多一行，因为代币应能无限叠加：

```cpp
URPGTokenItem()
{
    ItemType = URPGAssetManager::TokenItemType;
    MaxCount = 0; // Infinite，0 表示数量无上限
}
```

武器（`RPGWeaponItem.h`）多一个属性，因为武器要在世界里生成 actor：

```cpp
URPGWeaponItem()
{
    ItemType = URPGAssetManager::WeaponItemType;   // 类型 = "Weapon"
}

UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Weapon)
TSubclassOf<AActor> WeaponActor;                   // 装备时生成的武器 actor
```

看出门道了吧：**子类自己几乎不写逻辑，它存在的全部意义就是声明"我是一个独立的 `FPrimaryAssetType`"**，顺手调一两个默认值。这就是数据驱动的味道——类型靠 C++ 划清楚，每一件物品的数值靠数据资产去填。

那 `PotionItemType` 这些常量是啥？它们是 `URPGAssetManager` 上的静态成员（`RPGAssetManager.h` 声明、`.cpp` 定义）：

```cpp
const FPrimaryAssetType URPGAssetManager::PotionItemType = TEXT("Potion");
const FPrimaryAssetType URPGAssetManager::SkillItemType  = TEXT("Skill");
const FPrimaryAssetType URPGAssetManager::TokenItemType  = TEXT("Token");
const FPrimaryAssetType URPGAssetManager::WeaponItemType = TEXT("Weapon");
```

为啥不把字符串直接写进每个子类？因为这样四个子类、管理器内部的加载逻辑、还有别处的代码，全都引用同一份常量，改一处就够了，不会出现一边 "Potion" 一边 "potion" 对不上。

---

## 七、同步 vs 异步加载：代价与场合

资产被扫描到只是建了索引，内容还没进内存。要真正拿到一件物品对象，ActionRPG 提供了一个**同步**应急通道 `URPGAssetManager::ForceLoadItem`：

```cpp
URPGItem* URPGAssetManager::ForceLoadItem(const FPrimaryAssetId& PrimaryAssetId, bool bLogWarning)
{
    FSoftObjectPath ItemPath = GetPrimaryAssetPath(PrimaryAssetId);
    URPGItem* LoadedItem = Cast<URPGItem>(ItemPath.TryLoad());  // 同步加载，可能卡顿
    // 失败打警告 ...
    return LoadedItem;
}
```

它的头文件注释把权衡说得很清楚：

> Synchronously loads an RPGItem subclass, this can hitch but is useful when you cannot wait for an async load. This does not maintain a reference to the item so it will garbage collect if not loaded some other way.

两个关键点：

1. **同步**：加载这件物品时整个游戏线程就停在这儿等磁盘，资源大或一次加载一堆，画面就会卡一下（hitch）。
2. **不持有引用**：函数返回后如果没有别的地方（比如背包的 `UPROPERTY` 指针）抓住这个对象，下次 GC 就把它回收了。

什么时候用哪种？经验：

- **同步**：需要立刻拿到结果、又确定卡顿可接受的场合。比如某些编辑器工具，或已经在加载屏后面、玩家本来就在等的初始化阶段。
- **异步**：正常游戏过程里能提前预测的加载，比如进图前预载这张图会用到的物品。让加载在后台慢慢做，做完回调通知你。Asset Manager 提供了 `LoadPrimaryAssets` 之类的异步接口，ActionRPG 在背包和存档流程里用的就是异步那套。

`ForceLoadItem` 名字里那个 `Force` 就是提醒你这是有代价的。移动端尤其要记牢：**能异步就别同步，能晚加载就别早加载，用完让它被 GC 回收**。这也是"区别于教学 demo"的地方——demo 才会图省事一股脑全同步加载。

> `ForceLoadItem → GetPrimaryAssetPath → TryLoad` 这条链路具体怎么走、为什么"不持引用就会被 GC"，见 [02 源码篇](UE基础四-02-源码篇-从启动到加载.md)。

---

## 八、小结

- Asset Manager = 仓库管理员：启动时建"货号 → 路径"索引，按需加载，省内存——全为"海量资产 + 上手机"服务。
- **主要资产**靠货号手动管，**次要资产**靠引用关系图自动跟随；没有 `FSecondaryAssetType`，是故意的不对称。
- **Primary Data Asset** = override 了 `GetPrimaryAssetId()`、因而能被管理的数据资产。
- **货号 `FPrimaryAssetId` = 类型 + 名字**，如 `Potion:Potion_Health`；全项目用货号指代物品，不持裸指针。
- `URPGItem` 抽象基类 + 四个子类，子类只负责声明类型（`ItemType` 常量统一放在 `URPGAssetManager` 上）。
- 加载分**同步**（`ForceLoadItem`，会卡、不持引用）和**异步**（能预测的加载走这条），移动端优先异步。

概念清楚了，下一篇我们把这套设计**连着源码**从头到尾走一遍：启动时管理员到底干了什么、`Potion_Health` 是怎么进到管理员里的、`DefaultGame.ini` 又起什么作用。→ [02 源码篇](UE基础四-02-源码篇-从启动到加载.md)
