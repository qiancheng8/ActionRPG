# ActionRPG 阶段五：背包与存档系统，从「数据存哪」开始把整条链路看穿

背包系统听起来很简单，无非是个列表加几个装备槽。但在 UE 里，第一个真正卡住你的问题不是怎么实现增删，而是「这些数据到底该挂在哪个对象上」。是放在角色身上？放在某个全局单例里？还是塞进存档对象？

ActionRPG 给出的答案有点反直觉：背包的权威数据放在 **PlayerController** 上。这篇就顺着这个选择，把背包归属、native-only 接口、异步存档、以及最关键的双 delegate 广播机制串起来讲清楚。读完你应该能自己画出「改一次背包，到底有哪些东西被惊动」的完整数据流。

阅读前提：你已经看过阶段四，知道物品是 `URPGItem` 这种 Primary Data Asset，靠 `FPrimaryAssetId` 标识。本阶段会大量用到这个 Id。

## 一、背包归属：两张权威 map 挂在 PlayerController 上

打开 `Source/ActionRPG/Public/RPGPlayerControllerBase.h`，类声明第一眼就值得注意：

```cpp
UCLASS()
class ACTIONRPG_API ARPGPlayerControllerBase : public APlayerController, public IRPGInventoryInterface
{
    GENERATED_BODY()
```

它继承了 `APlayerController`，同时实现了 `IRPGInventoryInterface`。背包的两张权威表就定义在这里：

```cpp
/** 玩家拥有的所有物品：从物品定义指向它的数量/等级数据 */
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Inventory)
TMap<URPGItem*, FRPGItemData> InventoryData;

/** 装备槽：从「槽位(类型+编号)」指向「装在里面的物品」，槽位数量由 GameInstance 的 ItemSlotsPerType 初始化 */
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Inventory)
TMap<FRPGItemSlot, URPGItem*> SlottedItems;
```

`InventoryData` 是「我有什么、有几个、几级」，`SlottedItems` 是「哪个槽里装了什么」。两者分开，因为「拥有」和「装备」是两回事。你可能背包里有三把剑，但武器槽只装一把。

`FRPGItemData` 和 `FRPGItemSlot` 都在 `RPGTypes.h`。`FRPGItemData` 就是个数量加等级的结构体，注意它的构造函数默认 count/level 都是 1，作者特意注释了原因：在蓝图里声明时给你符合直觉的默认值，不会一不小心搞出个 0 个的物品。`FRPGItemSlot` 则是「类型 + 槽号」，并且实现了 `operator==` 和 `GetTypeHash`，这是它能当 `TMap` 的 key 的前提。在 UE 里，任何你想拿来当 map key 的 struct 都得自己提供这两样,引擎不会替你生成。

为什么是 PlayerController 而不是别的?因为 PlayerController 代表「操控这个角色的玩家」,它的生命周期跨越角色死亡重生,而且天然是「每个玩家一份」。把背包挂这里,联机时每个玩家的背包归属就自然分清了。角色(`ARPGCharacterBase`)只是个躯壳,可能换,但背包跟着玩家走。

这两张 map 是整个系统的 source of truth。存档里的数据是它的快照,UI 上显示的是它的投影,但「真相」永远在这两张 map。记住这点,后面的存读档逻辑就好理解了。

## 二、接口的意义:native-only 的 IRPGInventoryInterface

角色需要读背包。比如要判断某个槽位装了什么武器,好决定授予哪个能力。问题是角色怎么拿到背包?最粗暴的写法是 `Cast<ARPGPlayerControllerBase>(GetController())` 然后直接访问成员。能用,但耦合死了:角色被焊死在了某个具体的 Controller 子类上。

ActionRPG 的做法是抽一个接口。看 `RPGInventoryInterface.h`:

```cpp
/**
 * 给「能提供一组绑定到槽位的 RPGItem」的 actor 用的接口
 * 它的存在是为了让 RPGCharacterBase 不用做难看的 player controller 向下转型就能查询背包
 * 这个接口只为 native 类设计
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class URPGInventoryInterface : public UInterface
{
    GENERATED_BODY()
};

class ACTIONRPG_API IRPGInventoryInterface
{
    GENERATED_BODY()

public:
    /** 返回「物品 → 数据」的 map */
    virtual const TMap<URPGItem*, FRPGItemData>& GetInventoryDataMap() const = 0;

    /** 返回「槽位 → 物品」的 map */
    virtual const TMap<FRPGItemSlot, URPGItem*>& GetSlottedItemMap() const = 0;

    /** 拿到「物品变化」的 native delegate */
    virtual FOnInventoryItemChangedNative& GetInventoryItemChangedDelegate() = 0;

    /** 拿到「槽位变化」的 native delegate */
    virtual FOnSlottedItemChangedNative& GetSlottedItemChangedDelegate() = 0;

    /** 拿到「背包加载完成」的 native delegate */
    virtual FOnInventoryLoadedNative& GetInventoryLoadedDelegate() = 0;
};
```

这里有两个 C++ 程序员必须搞清楚的 UE 特性。

第一,**UE 接口是「两个类」**。你看到一对:`URPGInventoryInterface`(U 开头,继承 `UInterface`)和 `IRPGInventoryInterface`(I 开头,是个普通抽象类)。前者是给反射系统用的占位 UObject,你几乎从不直接碰它;真正写虚函数、被别人继承的是后者那个 `I` 类。这和普通 C++ 只有一个抽象基类的写法不一样,是 UE 反射机制的要求。实现方在类声明里继承的是 `I` 那个,比如前面 `ARPGPlayerControllerBase : public APlayerController, public IRPGInventoryInterface`。

第二,`meta = (CannotImplementInterfaceInBlueprint)` 加上注释里那句「only for use by native classes」,说明这是个**纯 C++ 接口**,蓝图实现不了它。这个限制是故意的。你看接口里的函数返回的全是 `TMap<>&` 和 `FOnInventoryItemChangedNative&` 这种 C++ 引用和 native delegate,蓝图根本表达不出来。把它锁死在 native 层,反而干净。

那 `ARPGPlayerControllerBase` 怎么实现?直接在头文件里内联给出:

```cpp
// 实现 IRPGInventoryInterface
virtual const TMap<URPGItem*, FRPGItemData>& GetInventoryDataMap() const override
{
    return InventoryData;
}
virtual const TMap<FRPGItemSlot, URPGItem*>& GetSlottedItemMap() const override
{
    return SlottedItems;
}
virtual FOnInventoryItemChangedNative& GetInventoryItemChangedDelegate() override
{
    return OnInventoryItemChangedNative;
}
// ... 其余几个同理,把对应成员的引用吐出去
```

于是角色拿到任何一个实现了 `IRPGInventoryInterface` 的对象,就能读背包、能订阅变化,完全不关心背后是哪个 Controller。这就是接口换来的解耦:角色依赖「能提供背包的东西」这个抽象,而不是某个具体类。

## 三、持久化链路:数据怎么落盘,又怎么读回来

背包在 PlayerController 上,但 PlayerController 是关卡级的,切关卡就没了。要跨关卡、跨会话保留数据,得有个活得更久的对象来管存档。这个角色由 `URPGGameInstanceBase` 担任。GameInstance 是整个游戏进程级的单例,从启动活到退出,天然适合管全局存档。

看 `RPGGameInstanceBase.h` 里它持有的东西:

```cpp
/** 给新玩家的默认背包物品 */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Inventory)
TMap<FPrimaryAssetId, FRPGItemData> DefaultInventory;

/** 每种物品类型有几个槽 */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Inventory)
TMap<FPrimaryAssetType, int32> ItemSlotsPerType;

// ... protected:
/** 当前存档对象 */
UPROPERTY()
URPGSaveGame* CurrentSaveGame;
```

`DefaultInventory` 是新档的初始物品,`ItemSlotsPerType` 决定每种类型开几个槽位(还记得前面 `SlottedItems` 说「槽位数量由 GameInstance 初始化」吗,就是从这来的)。`CurrentSaveGame` 是内存里的存档对象。

`URPGSaveGame`(`RPGSaveGame.h`)继承自引擎的 `USaveGame`,它存的是 map,但 key 全换成了 `FPrimaryAssetId`:

```cpp
/** 物品 → 物品数据 */
UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = SaveGame)
TMap<FPrimaryAssetId, FRPGItemData> InventoryData;

/** 槽位 → 物品 */
UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = SaveGame)
TMap<FRPGItemSlot, FPrimaryAssetId> SlottedItems;
```

对比一下:Controller 里的 `InventoryData` 是 `TMap<URPGItem*, FRPGItemData>`,key 是**指针**;存档里是 `TMap<FPrimaryAssetId, FRPGItemData>`,key 是 **Id**。这个区别是核心。你不能把裸 `URPGItem*` 写进存档,指针落盘没意义,下次启动地址全变了。所以存档时把指针转成 `FPrimaryAssetId`(物品的稳定标识),读档时再用 Asset Manager 按 Id 把物品加载回来,还原成指针。

落盘的逻辑在 `RPGPlayerControllerBase.cpp` 的 `SaveInventory()`。它做的事很直白:把 Controller 的两张 map 遍历一遍,逐个 `Key->GetPrimaryAssetId()` 转成 Id 塞进 `CurrentSaveGame`,清掉旧缓存再重写,最后调 `GameInstance->WriteSaveGame()`。

```cpp
for (const TPair<URPGItem*, FRPGItemData>& ItemPair : InventoryData)
{
    FPrimaryAssetId AssetId;
    if (ItemPair.Key)
    {
        AssetId = ItemPair.Key->GetPrimaryAssetId();   // 指针 → 稳定 Id
        CurrentSaveGame->InventoryData.Add(AssetId, ItemPair.Value);
    }
}
// ... 槽位同理,最后:
GameInstance->WriteSaveGame();
```

**真正落盘是异步的**。看 `WriteSaveGame()`:

```cpp
bool URPGGameInstanceBase::WriteSaveGame()
{
    if (bSavingEnabled)
    {
        if (bCurrentlySaving)
        {
            // 正在存,排队一个待办,但只排一个
            bPendingSaveRequested = true;
            return true;
        }
        bCurrentlySaving = true;
        // 这一步在后台线程跑,不阻塞游戏
        UGameplayStatics::AsyncSaveGameToSlot(GetCurrentSaveGame(), SaveSlot, SaveUserIndex,
            FAsyncSaveGameToSlotDelegate::CreateUObject(this, &URPGGameInstanceBase::HandleAsyncSave));
        return true;
    }
    return false;
}
```

为什么要异步?写磁盘慢,主线程一卡就掉帧,移动端尤其明显。`AsyncSaveGameToSlot` 把写盘丢到后台,完成后回调 `HandleAsyncSave`。这里还有个简单但实用的去抖设计:如果上一次存档还没写完又来了新请求,不会并发去写,而是用 `bPendingSaveRequested` 标记排一个待办,等当前这次写完后,在 `HandleAsyncSave` 里再补一次。注意它只排一个,中间来多少次请求都合并成最后那一次,因为反正存的都是最新快照。

```cpp
void URPGGameInstanceBase::HandleAsyncSave(const FString& SlotName, const int32 UserIndex, bool bSuccess)
{
    ensure(bCurrentlySaving);
    bCurrentlySaving = false;
    if (bPendingSaveRequested)
    {
        // 存档期间又来了请求,现在补存一次
        bPendingSaveRequested = false;
        WriteSaveGame();
    }
}
```

读档入口是 `LoadOrCreateSaveGame()`。它先用 `UGameplayStatics::DoesSaveGameExist` 看磁盘上有没有存档,有就 `LoadGameFromSlot` 同步读出来,然后统一走 `HandleSaveGameLoaded`。`HandleSaveGameLoaded` 的关键在于:读到了就补上可能新增的默认物品(`AddDefaultInventory`,只加不覆盖),读不到就 `CreateSaveGameObject` 当场造一个新档并灌入全套默认背包。无论哪条路,最后都会广播 `OnSaveGameLoaded` / `OnSaveGameLoadedNative`:

```cpp
OnSaveGameLoaded.Broadcast(CurrentSaveGame);        // 蓝图版
OnSaveGameLoadedNative.Broadcast(CurrentSaveGame);  // native 版
```

谁在听这个广播?PlayerController。在 `LoadInventory()` 里它把自己绑了上去:

```cpp
if (!GameInstance->OnSaveGameLoadedNative.IsBoundToObject(this))
{
    GameInstance->OnSaveGameLoadedNative.AddUObject(this, &ARPGPlayerControllerBase::HandleSaveGameLoaded);
}
```

而 `HandleSaveGameLoaded` 干的事就一行:重新 `LoadInventory()`。于是只要 GameInstance 那边换了存档(读档、重置存档),Controller 就会自动把背包刷新一遍。读档时它会把存档里的 `FPrimaryAssetId` 用 `AssetManager.ForceLoadItem` 同步加载回 `URPGItem*`,重新填进自己的两张 map,最后调 `NotifyInventoryLoaded()` 通知所有人「背包重载完毕」。整条链路就此闭合。

顺带提一句,`URPGSaveGame` 重写了 `Serialize` 并带了个 `SavedDataVersion` 版本号。这是给存档做向后兼容的:老版本存档读进来,native 代码能按版本号做字段补正。这是发布过的游戏才会认真对待的细节,初学时知道有这么个东西就行。

## 四、变化广播:为什么每个事件都要两套 delegate

这是本阶段最该花时间的地方。背包一改,UI 得刷新、能力系统得重新算授予的技能,但 PlayerController 不该认识 UI、也不该认识能力系统。解耦靠的就是 delegate(委托/多播事件),改背包的人只管「喊一嗓子」,谁关心谁自己来订阅。

打开 `RPGTypes.h` 的末尾,你会看到一个很有规律的现象,每件事都声明了**两个** delegate:

```cpp
/** 背包物品变化时调用 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInventoryItemChanged, bool, bAdded, URPGItem*, Item);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInventoryItemChangedNative, bool, URPGItem*);

/** 某个装备槽内容变化时调用 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSlottedItemChanged, FRPGItemSlot, ItemSlot, URPGItem*, Item);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSlottedItemChangedNative, FRPGItemSlot, URPGItem*);

/** 整个背包被加载/重载时调用(所有物品可能都被换掉了) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryLoaded);
DECLARE_MULTICAST_DELEGATE(FOnInventoryLoadedNative);

/** 存档被加载/重置时调用 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSaveGameLoaded, URPGSaveGame*, SaveGame);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSaveGameLoadedNative, URPGSaveGame*);
```

带 `DYNAMIC` 的是一套,带 `Native` 后缀的是另一套。区别在哪、为什么要两套,得搞清楚。

**Dynamic 版**(`DECLARE_DYNAMIC_MULTICAST_DELEGATE`)走 UE 反射系统。它能被蓝图绑定,也能用 `UFUNCTION` 标记的函数绑定,在 PlayerController 里它配 `UPROPERTY(BlueprintAssignable)` 暴露给蓝图:

```cpp
/** 背包物品被加/删时调用 */
UPROPERTY(BlueprintAssignable, Category = Inventory)
FOnInventoryItemChanged OnInventoryItemChanged;
```

`BlueprintAssignable` 让你能在蓝图里直接「绑定到这个事件」,UI 蓝图就是这么订阅背包变化的。代价是它走反射,绑定靠函数名字符串查找,触发时有一层反射开销,而且能绑的函数必须是 `UFUNCTION`。参数也只能是反射系统认识的类型。

**Native 版**(`DECLARE_MULTICAST_DELEGATE`)是纯 C++ 多播,不进反射。它快,绑定用的是函数指针(`AddUObject`、`AddLambda` 这些),没有字符串查找的开销,参数类型也更自由。代价是蓝图碰不到它,只有 C++ 能用。注意 native 版的参数列表往往比 dynamic 版还少一个,比如 `FOnInventoryItemChangedNative` 只有 `bool, URPGItem*`,把 dynamic 版里的参数名省了,因为 native delegate 声明不需要参数名。

为什么不二选一?因为两边的订阅者诉求不同。UI 大多在蓝图里画,必须用 dynamic;而像 PlayerController 自己、或者 `ARPGCharacterBase` 这种要快速响应、又全在 C++ 里的订阅者,用 native 更合适。`IRPGInventoryInterface` 暴露出去的全是 native delegate,就是因为接口的目标用户(角色)是 native 类,要的是性能和直接的函数指针绑定。

触发顺序也很讲究。看 `NotifyInventoryItemChanged`:

```cpp
void ARPGPlayerControllerBase::NotifyInventoryItemChanged(bool bAdded, URPGItem* Item)
{
    // 先 native 后蓝图
    OnInventoryItemChangedNative.Broadcast(bAdded, Item);
    OnInventoryItemChanged.Broadcast(bAdded, Item);

    // 再调 BP 的 implementable event
    InventoryItemChanged(bAdded, Item);
}
```

**永远先广播 native,再广播 dynamic**。注释写得很明白「Notify native before blueprint」。这样 C++ 侧的核心逻辑(比如能力系统更新)先于蓝图 UI 跑完,UI 刷新时看到的已经是处理过的状态。最后那个 `InventoryItemChanged(...)` 是个 `BlueprintImplementableEvent`,给蓝图子类一个直接重写的钩子,跟广播是互补的两条路。槽位变化的 `NotifySlottedItemChanged` 是一模一样的套路。

## 五、把数据流画出来

现在把前面所有碎片拼成一张图。以「玩家捡到一把武器」为例,从调用 `AddInventoryItem` 开始:

```
玩家捡起武器
  └─ ARPGPlayerControllerBase::AddInventoryItem(武器, 1, 1, bAutoSlot=true)
       ├─ 1) 改权威数据:InventoryData.Add(武器, 新的 FRPGItemData)
       ├─ 2) NotifyInventoryItemChanged(bAdded=true, 武器)
       │       ├─ OnInventoryItemChangedNative.Broadcast(...)   ← C++ 订阅者先收到
       │       ├─ OnInventoryItemChanged.Broadcast(...)         ← 蓝图(背包 UI)收到 → 刷新格子
       │       └─ InventoryItemChanged(...)                     ← 蓝图 implementable event 钩子
       ├─ 3) bAutoSlot 为真 → FillEmptySlotWithItem(武器)
       │       └─ 找到空的武器槽 → SlottedItems[槽] = 武器
       │            └─ NotifySlottedItemChanged(槽, 武器)
       │                 ├─ OnSlottedItemChangedNative.Broadcast(...) ← 角色(GAS)收到 → 重新授予技能
       │                 ├─ OnSlottedItemChanged.Broadcast(...)       ← 蓝图(装备栏 UI)收到 → 刷新
       │                 └─ SlottedItemChanged(...)                   ← 蓝图钩子
       └─ 4) 有变化 → SaveInventory()
               ├─ 把两张 map 的 key 从 URPGItem* 转成 FPrimaryAssetId 写进 CurrentSaveGame
               └─ GameInstance->WriteSaveGame()  → AsyncSaveGameToSlot 后台落盘
```

读档方向是反的:

```
LoadOrCreateSaveGame() / 切关卡 / BeginPlay
  └─ GameInstance::HandleSaveGameLoaded → 广播 OnSaveGameLoadedNative
       └─ PlayerController::HandleSaveGameLoaded → LoadInventory()
            ├─ 按 ItemSlotsPerType 重建空槽位
            ├─ 遍历存档:ForceLoadItem(FPrimaryAssetId) 还原成 URPGItem*,填回两张 map
            └─ NotifyInventoryLoaded() → 所有 UI 整体重刷
```

看懂这两张图,你就抓住了本阶段的精髓:**权威数据只有一份(Controller 的两张 map),所有变化先落到它身上,再通过 delegate 向外扩散,落盘只是顺手把它转成 Id 形式存一份快照**。UI、GAS 这些下游谁都不直接改背包,全靠订阅。

## 六、验收清单

对照学习计划阶段五,确认下面这些你都能脱口而出:

- [ ] 背包权威数据在哪?`ARPGPlayerControllerBase` 的 `InventoryData`(`TMap<URPGItem*, FRPGItemData>`)和 `SlottedItems`(`TMap<FRPGItemSlot, URPGItem*>`)。为什么挂在 Controller 而不是角色上,能说出理由。
- [ ] `IRPGInventoryInterface` 为什么是 native-only?它解决了什么(角色不用向下转型 Cast Controller)?顺便能讲清 UE 接口为什么是 `U` + `I` 一对类,跟普通 C++ 单一抽象基类的差别。
- [ ] 存档为什么用 `FPrimaryAssetId` 而不是 `URPGItem*` 做 key?(指针不能跨会话持久化。)
- [ ] 存档为什么异步?`WriteSaveGame` → `AsyncSaveGameToSlot` → `HandleAsyncSave` 这条链,以及 `bCurrentlySaving`/`bPendingSaveRequested` 的去抖排队逻辑。
- [ ] dynamic delegate 和 native delegate 的取舍:dynamic 支持蓝图/`UFUNCTION` 绑定但走反射偏慢,native 纯 C++、用函数指针、更快但蓝图用不了。为什么 `IRPGInventoryInterface` 只暴露 native 版。
- [ ] 为什么广播顺序是「先 native 后蓝图」?
- [ ] 能独立画出第五节那张「改背包 → 触发哪些 delegate → UI/GAS 如何响应 → 如何落盘」的数据流。

能全过,这一阶段就稳了。下一站是 GAS,前面提到的「装备槽变化 → 角色重新授予技能」那条线,正是阶段六的入口,到时候你会回头看 `NotifySlottedItemChanged`,发现它就是背包和能力系统之间那根线。
