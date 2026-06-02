# 阶段七：UMG、加载屏与异步，UE 里 UI 怎么跟数据"自己对上"

到了这一阶段，你已经啃完了 GAS 那座大山。这篇相对轻松，但有两个点特别能体现"UE 工程化思路"和"普通 C++ demo"的区别：一个是 UI 怎么不靠轮询就跟着背包数据自动刷新，一个是加载屏为什么要单独抽成一个模块。我们一个个来。

先说清楚一件事，作为 C++ 程序员你最容易踩的坑是：以为 UI 也能在源码里写完。ActionRPG 的 UI 一行 C++ 都没有，全在 UMG 的 Widget 蓝图里。所以这一阶段你得多开编辑器，少看 `Source/`。

## UMG 是什么，先建立印象

UMG（Unreal Motion Graphics）就是 UE 的 UI 框架。你做的每一个界面，背包、暂停菜单、血条、伤害飘字，都是一个 Widget 蓝图（资产前缀 `WB_`）。它底层是 Slate（纯 C++ 的那套，加载屏就是直接用 Slate 写的，后面会看到），但日常开发你基本只跟 UMG 打交道。

一个 Widget 蓝图有两个面板：

- **Designer**：拖控件、摆布局。Button、Text、Image、各种 Box 容器，跟做网页排版差不多。
- **Graph**：写逻辑的地方，跟普通蓝图一样有 Event Graph。

打开内容浏览器，定位到 `Content/Blueprints/WidgetBP/`，你会看到这个项目所有的界面控件。背包相关的都在 `Inventory/` 子目录下：

```
Content/Blueprints/WidgetBP/Inventory/WB_Equipment.uasset      装备面板
Content/Blueprints/WidgetBP/Inventory/WB_EquipmentSlot.uasset  单个装备槽
Content/Blueprints/WidgetBP/Inventory/WB_InventoryItem.uasset  背包里的单个物品格
Content/Blueprints/WidgetBP/Inventory/WB_InventoryList.uasset  背包列表
```

我们重点看 `WB_InventoryList` 和 `WB_EquipmentSlot` 这两个，它们能把"数据变了，UI 自己刷新"这条链讲透。

## 核心问题：UI 怎么知道背包变了

回想阶段五，背包的权威数据在 `ARPGPlayerControllerBase` 上，就是那两张 map：`InventoryData` 和 `SlottedItems`。现在 UI 要把这些画出来。问题是，玩家捡了个药水、换了把武器，UI 怎么知道该重画？

最蠢的做法是每帧去读一遍 map，对比有没有变化。UE 不这么干，它用的是 delegate 广播。这就是阶段五埋下的那套东西，现在终于用上了。

打开 `RPGTypes.h`，你能看到背包相关的 delegate 是成对声明的：

```cpp
/** Delegate called when the contents of an inventory slot change */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSlottedItemChanged, FRPGItemSlot, ItemSlot, URPGItem*, Item);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSlottedItemChangedNative, FRPGItemSlot, URPGItem*);
```

上面那个 `Dynamic` 版本是给蓝图用的，下面带 `Native` 后缀的是纯 C++ 版本。为什么要两套，阶段五讲过：dynamic 的支持蓝图绑定、能走反射，慢一点；native 的纯 C++ 调用、快，但蓝图碰不到。UI 是蓝图，所以它绑的是 dynamic 那个。

数据这边怎么发广播？看 `RPGPlayerControllerBase.cpp` 里换装备的逻辑，玩家把一件物品放进某个槽位时，最后会调一个 `NotifySlottedItemChanged`：

```cpp
void ARPGPlayerControllerBase::NotifySlottedItemChanged(FRPGItemSlot ItemSlot, URPGItem* Item)
{
    // Notify native before blueprint
    OnSlottedItemChangedNative.Broadcast(ItemSlot, Item);
    OnSlottedItemChanged.Broadcast(ItemSlot, Item);

    // Call BP update event
    SlottedItemChanged(ItemSlot, Item);
}
```

注意这里的顺序，先广播 native，再广播给蓝图。原因是有些 C++ 逻辑（比如 GAS 那边要根据槽位重新授予能力）必须先于 UI 跑完，免得 UI 拿到的是半截状态。

## 把链子串起来：delegate → 事件 → 刷新

现在把整条链按真实流程走一遍。

第一步，UI 控件出生时去订阅。打开 `WB_EquipmentSlot` 的 Graph，找 Event Construct（相当于这个控件的"构造函数"事件）。它会拿到玩家控制器，然后把自己的一个自定义事件绑到控制器的 `OnSlottedItemChanged` 上。在蓝图里这一步就是从控制器变量拖出 `OnSlottedItemChanged`，连一个 `Bind Event` 节点，绑到自己的刷新函数上。

第二步，玩家操作触发数据变化。比如把"火焰斧"拖进武器槽，控制器更新 `SlottedItems` 这张 map，然后调 `NotifySlottedItemChanged`。

第三步，广播。`OnSlottedItemChanged.Broadcast(...)` 一发，所有订阅过的控件都收到回调，参数告诉它们是哪个槽（`FRPGItemSlot`）、放进去的是哪件物品（`URPGItem*`）。

第四步，每个槽位控件在回调里判断"这个变化的槽是不是我负责的那个"，是的话就把图标、数量、等级重新设一遍。`WB_EquipmentSlot` 拿到 `URPGItem` 后，读它的图标、名字，刷到自己的 Image 和 Text 控件上。

整条链就是：**数据改 → 控制器 Broadcast delegate → 订阅的 Widget 收到事件 → Widget 重画自己**。UI 全程被动，从不主动去问"数据变了没"。这就是事件驱动比轮询好的地方，没人空转，改一次画一次。

`WB_InventoryList` 也是一样的套路，它订阅的是 `OnInventoryItemChanged`（捡到/丢弃物品时触发的那个），收到广播后重新生成列表里的 `WB_InventoryItem` 子控件。

还有个细节值得记一下，进游戏一开始读档完成时，会触发 `OnInventoryLoaded`：

```cpp
// Load failed but we reset inventory, so need to notify UI
NotifyInventoryLoaded();
```

哪怕读档失败、背包被重置成空的，也得发这个广播，否则 UI 会停在旧状态。这种"无论成功失败都要通知 UI"的习惯，写界面时记牢。

## 加载屏：为什么非得拆成独立模块

切到第二个主题。打开 `ActionRPG.uproject`，看 `Modules` 段：

```json
"Modules": [
    {
        "Name": "ActionRPG",
        "Type": "Runtime",
        "LoadingPhase": "Default"
    },
    {
        "Name": "ActionRPGLoadingScreen",
        "Type": "ClientOnly",
        "LoadingPhase": "PreLoadingScreen"
    }
]
```

两个模块。主模块 `ActionRPG` 是 `Runtime` 类型、`Default` 阶段加载。加载屏 `ActionRPGLoadingScreen` 是 `ClientOnly` 类型、`PreLoadingScreen` 阶段加载。关键就在这个 `LoadingPhase`。

UE 启动时模块不是一股脑全加载，而是分阶段来。`PreLoadingScreen` 这个阶段，顾名思义，比加载屏本身还早，比绝大多数游戏模块都早。`Default` 则是常规游戏代码加载的阶段，晚得多。

现在你应该能想明白这个鸡生蛋的问题了：加载屏的作用，是在主模块、各种资产都还没准备好的那段空白时间里，给玩家一个"程序没卡死，正在加载"的反馈。可如果加载屏的代码本身放在主模块里，那它得等主模块加载完才能显示。等主模块都加载完了，加载屏也就没意义了。

所以加载屏必须比主模块先加载，那它就**不能**待在主模块里，只能单独成一个模块，并且声明成 `PreLoadingScreen` 阶段，抢在主模块前头初始化。源码注释把这点说得很直白：

```cpp
// This module must be loaded "PreLoadingScreen" in the .uproject file, otherwise it will not hook in time!
```

"otherwise it will not hook in time"，否则就来不及挂上去了。

## 不能反向依赖主模块

既然加载屏比主模块先加载，那它就**绝对不能**引用主模块里的任何类型。道理很硬：主模块这会儿压根还没加载，你去 `#include` 它的头文件、用它的 `URPGItem`，链接和加载顺序都会出问题。

这也是为什么这个加载屏整个用 Slate 手写、连资源路径都是硬编码字符串的原因。看 `ActionRPGLoadingScreen.cpp`：

```cpp
// Load version of the logo with text baked in, path is hardcoded because this loads very early in startup
static const FName LoadingScreenName(TEXT("/Game/UI/T_ActionRPG_TransparentLogo.T_ActionRPG_TransparentLogo"));
```

注释明说了，路径硬编码，因为这玩意儿在启动极早期就跑了，那时候资产系统还指望不上，只能直接按路径捞这张 logo 贴图。整个加载界面就是 Slate 拼出来的：一个深灰背景、中间一张 logo、右下角一个转圈的 `SThrobber`，没有用到任何 UMG Widget 蓝图，也没碰主模块一行代码。

那主模块怎么用这个加载屏？通过接口，不直接 `#include` 实现。`ActionRPGLoadingScreen.h` 里只暴露一个纯虚接口：

```cpp
class IActionRPGLoadingScreenModule : public IModuleInterface
{
public:
    static inline IActionRPGLoadingScreenModule& Get()
    {
        return FModuleManager::LoadModuleChecked<IActionRPGLoadingScreenModule>("ActionRPGLoadingScreen");
    }

    virtual void StartInGameLoadingScreen(bool bPlayUntilStopped, float PlayTime) = 0;
    virtual void StopInGameLoadingScreen() = 0;
};
```

主模块想在关卡切换时弹个加载屏，就走 `RPGBlueprintLibrary.cpp` 里这段：

```cpp
IActionRPGLoadingScreenModule& LoadingScreenModule = IActionRPGLoadingScreenModule::Get();
LoadingScreenModule.StartInGameLoadingScreen(bPlayUntilStopped, PlayTime);
```

主模块只认这个接口，不知道也不关心背后的 Slate 怎么画的。这就是标准的依赖单向流动：主模块依赖加载屏的接口，加载屏不依赖主模块的任何东西。这个隔离让加载屏能安心待在它那个超早的加载阶段。

顺带提一句，`ClientOnly` 这个类型是说这模块只在客户端编译进去。专用服务器（dedicated server）跑起来没人看屏幕，要加载屏干嘛，所以服务器构建直接把它剔掉，省体积。

## 异步加载：手机上卡一下是要命的

最后聊异步，把阶段四的伏笔收掉。

阶段四你见过 `URPGAssetManager::ForceLoadItem`，函数体很短：

```cpp
URPGItem* URPGAssetManager::ForceLoadItem(const FPrimaryAssetId& PrimaryAssetId, bool bLogWarning)
{
    FSoftObjectPath ItemPath = GetPrimaryAssetPath(PrimaryAssetId);

    // This does a synchronous load and may hitch
    URPGItem* LoadedItem = Cast<URPGItem>(ItemPath.TryLoad());
    // ...
    return LoadedItem;
}
```

`TryLoad()` 是同步加载，注释直说了 "may hitch"，会卡。它会阻塞当前线程，把整个资产从磁盘读出来、反序列化好，才返回。在这期间游戏整个停住，下一帧都出不来。

在 PC 上你可能感觉不明显，盘快、内存大，卡个几毫秒过去了。但这个项目的目标平台里有 Android 和 iOS。手机的存储读取慢得多，一次同步加载可能卡掉好几帧，画面直接顿一下。游戏里最忌讳的就是这种掉帧，玩家立刻就能感觉到"卡"，体验崩坏。所以移动端基本不能用同步加载,这不是优化建议,是硬约束。

那为什么这项目还在用 `ForceLoadItem`？看它的调用点，主要是读档恢复背包的时候（`RPGPlayerControllerBase.cpp` 里 `ForceLoadItem(ItemPair.Key)`）。读档本来就是切场景、卡在加载屏后面的时机，玩家本来就在等，这时候卡一下藏在加载屏背后没人看得见。换句话说,同步加载只用在"反正要等、又有加载屏遮着"的场合。

正经的游戏内加载走的是 Asset Manager 的异步接口（`LoadPrimaryAssets` / `LoadPrimaryAssetsWithType` 这一类，底层是 `FStreamableManager`）。异步加载是这样的：你说"我要这几个资产"，给一个加载完成的回调，函数立刻返回不阻塞，引擎在后台一点点把资产读进来，读好了再调你的回调。游戏主线程该跑跑、画面该动动，玩家完全无感。代价是你的代码得改成回调风格，资产到手前那一刻它还没准备好，逻辑上要处理"还在加载中"的中间态。

记住这个取舍就行：**要么用异步、不卡但要写回调；要么用同步、简单但会卡，且只敢用在加载屏遮着的地方。** 平台决定了你大部分时候只能选前者。

## 验收清单

这一阶段过没过，自己对一下：

1. 能不能讲清一个 UI 控件怎么随背包数据自动更新？标准答案：控件在 Construct 时把自己绑到控制器的 dynamic delegate（`OnSlottedItemChanged` / `OnInventoryItemChanged`）上，数据一变控制器调 `Notify*` 广播，控件收到事件回调，重画自己。全程事件驱动,不轮询。

2. 能不能解释加载屏为什么独立成模块、且不能反向依赖主模块？标准答案：加载屏要在主模块加载完之前就显示，所以它声明成 `PreLoadingScreen` 阶段、抢在主模块前加载；正因为它先加载，主模块那会儿还不存在，它就不能引用主模块的任何类型，只能用 Slate 手写、硬编码资源路径，并通过 `IActionRPGLoadingScreenModule` 接口被主模块单向调用。

3. 能不能说出同步加载在移动端为什么不行？标准答案：同步加载阻塞主线程会掉帧,手机存储慢更明显,所以游戏内加载必须走异步,同步的 `ForceLoadItem` 只用在读档这种有加载屏遮挡的时机。
