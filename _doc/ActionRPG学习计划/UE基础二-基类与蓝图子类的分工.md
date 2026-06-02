# C++ 写好了类，引擎却跑的是蓝图：看懂 UE 这套"基类被蓝图化"的把戏

刚从纯 C++ 转过来做 UE 的人，几乎都会被同一件事绊倒：你在 `Source/` 里翻到一个写得有模有样的 `ARPGGameModeBase`，满心以为引擎跑的就是它，结果运行起来根本不走你的断点。配置文件里引用的压根不是这个 C++ 类，而是一个叫 `BP_GameMode_C` 的东西。

这不是哪里配错了。这就是 UE 正向开发里最核心、也最反直觉的一条惯例：C++ 基类几乎从不直接拿来用，它存在的意义是被某个蓝图继承，然后引擎在配置里引用那个蓝图子类。本篇用 ActionRPG 这个官方示例，带你把这条链子从配置文件一路追到 C++ 头文件，搞懂之后你再看整个项目会顺很多。

## 先追一条真实的配置链

打开 `Config/DefaultEngine.ini`，有三行是这套架构的活样本：

```ini
[/Script/EngineSettings.GameMapsSettings]
GameInstanceClass=/Game/Blueprints/BP_GameInstance.BP_GameInstance_C
GlobalDefaultGameMode=/Game/Blueprints/BP_GameMode.BP_GameMode_C

[/Script/Engine.Engine]
AssetManagerClassName=/Script/ActionRPG.RPGAssetManager
```

先解释这两种路径写法，它们差别很大。

`/Game/...` 开头的，指向的是 `Content/` 目录下的资产。`/Game` 在 UE 里就是 `Content` 文件夹的虚拟根。所以 `/Game/Blueprints/BP_GameMode.BP_GameMode_C` 说的是 `Content/Blueprints/BP_GameMode.uasset` 这个蓝图资产里、那个名为 `BP_GameMode_C` 的生成类。结尾那个 `_C` 是蓝图编译后生成类的后缀，你写蓝图名字时通常不带 `_C`，但配置里引用它的运行时类就得带上。

`/Script/...` 开头的，指向的是 C++ 模块里编译出来的原生类。`/Script/ActionRPG.RPGAssetManager` 就是 `ActionRPG` 模块里的 `URPGAssetManager` 这个 C++ 类，没有中间蓝图。

把三行摆一起对比，意思就清楚了：

| 配置项 | 引擎实际用的类 | 它是什么 | 对应 C++ 基类 |
|--------|--------------|---------|--------------|
| `GameInstanceClass` | `BP_GameInstance_C` | 蓝图子类 | `URPGGameInstanceBase` |
| `GlobalDefaultGameMode` | `BP_GameMode_C` | 蓝图子类 | `ARPGGameModeBase` |
| `AssetManagerClassName` | `RPGAssetManager` | C++ 原生类 | 自己（无蓝图） |

GameMode 和 GameInstance 走的是"C++ 基类 + 蓝图子类"的两层结构，引擎引用蓝图那一层。AssetManager 例外，它直接用 C++ 类。

为什么 AssetManager 不套一层蓝图？因为它的职责是资产扫描、按需异步加载这类纯逻辑活,没有任何需要设计师在编辑器里调的参数,也没有需要可视化连线的事件。套一层蓝图纯属浪费。而 GameMode、GameInstance 这类东西,设计师经常要往上面挂数据、接事件流(比如默认背包配置、游戏结束时弹什么 UI),所以留了蓝图子类这一层给非程序员去装配。

记住这个判断标准:**有没有给设计师留装配空间,决定了它要不要被蓝图化。**

## 插一段基础:那串路径、`BP_` 和 `_C` 到底是什么

上面这几行配置对老手是常识,但第一次见的人多半一脸懵。这一节专门把它们拆开讲清楚,后面看 `DefaultEngine.ini`、`DefaultGame.ini` 里满屏的这种路径就不慌了。

### 路径的通用格式:`包路径.对象名`

UE 里引用一个资产或类,用的是一种叫"对象路径"的字符串,长得像 `/Game/Blueprints/BP_GameMode.BP_GameMode_C`。关键是中间那个**英文句点**,它把整条路径切成两半:

- 句点**左边**是"包路径",指向一个文件(在 UE 里叫 package,磁盘上通常就是一个 `.uasset`)。
- 句点**右边**是"对象名",指向这个文件**内部**的某个具体对象。

所以 `/Game/Blueprints/BP_GameMode.BP_GameMode_C` 读作:在 `/Game/Blueprints/BP_GameMode` 这个文件里,找名叫 `BP_GameMode_C` 的那个对象。

你可能会问,为什么左右两边长得几乎一样、都带 `BP_GameMode`?因为 UE 的资产文件一般只装一个"主对象",而且这个主对象默认跟文件同名。文件叫 `BP_GameMode`,里面那个蓝图主对象也叫 `BP_GameMode`,只不过我们要的不是蓝图本身,是它编译出来的类 `BP_GameMode_C`(下面讲 `_C` 会细说)。所以才出现"文件名.类名"这种看着重复的写法。

### 两个虚拟根:`/Game` 和 `/Script`

路径最开头那个 `/Game` 或 `/Script`,是 UE 给不同来源的东西安排的虚拟根目录,跟你磁盘上的真实路径不是一回事。

`/Game` 是 `Content/` 文件夹的虚拟根。凡是设计师在编辑器里做出来的资产(蓝图、贴图、材质、关卡、数据资产)都在这下面。换算关系很直接:

```
/Game/Blueprints/BP_GameMode   ←→   Content/Blueprints/BP_GameMode.uasset
```

`/Script` 是 C++ 代码模块的虚拟根。凡是你在 `Source/` 里用 C++ 写的、带 `UCLASS` 的类,引擎都通过 `/Script/模块名.类名` 来引用。所以:

```
/Script/ActionRPG.RPGAssetManager
         ↑          ↑
       模块名      C++ 类名(去掉 U/A 前缀)
```

`ActionRPG` 是模块名(就是 `ActionRPG.Build.cs` 那个模块),`RPGAssetManager` 是类名。注意这里类名**不带** `U` 前缀:头文件里是 `URPGAssetManager`,但路径里写 `RPGAssetManager`。UE 的对象路径不带 `U`/`A` 这种 C++ 前缀。

顺带提一句,还有个 `/Engine` 根,指向引擎自带的内容(官方的示例网格、默认材质那些),用法跟 `/Game` 一样,只是来源是引擎而非你的项目。

一句话总结这三个根:`/Game` 是你项目的资产,`/Engine` 是引擎的资产,`/Script` 是所有 C++ 代码类。看到路径开头是哪个,立刻就知道这东西是策划做的、引擎给的、还是程序写的。

### `BP_` 前缀:只是个命名约定

`BP_GameMode` 前面的 `BP_` 没有任何语法含义,纯粹是命名习惯,`BP` 就是 Blueprint 的缩写。引擎不强制,但所有人都这么写,为的是一眼区分这是个蓝图资产,而不是 C++ 类或别的资产。

UE 社区对各类资产都有这种前缀约定,顺手记几个常见的,以后在内容浏览器里扫一眼名字就知道是什么:

| 前缀 | 含义 | 例子 |
|------|------|------|
| `BP_` | 蓝图 | `BP_GameMode` |
| `WBP_` | UMG 控件蓝图(Widget Blueprint) | `WBP_Inventory` |
| `SM_` | 静态网格 | `SM_Rock` |
| `SK_` | 骨骼网格 | `SK_Player` |
| `M_` / `MI_` | 材质 / 材质实例 | `M_Floor` |
| `T_` | 贴图 | `T_Icon_Potion` |
| `DA_` | 数据资产 | `DA_Potion_Health` |

这跟 C++ 那套 `U`/`A`/`F`/`I` 前缀是两个体系。C++ 前缀是给代码看的,资产前缀是给编辑器里的人看的。

### `_C` 后缀:蓝图资产和它编译出的类,是两个东西

这个是这一节最容易被忽略、却最该搞懂的点。一个蓝图资产,内部其实不止一个对象。拿 `BP_GameMode` 举例,它至少装着两样东西:

1. **蓝图对象本身**,名字就叫 `BP_GameMode`,类型是 `UBlueprint`。这是你在编辑器里双击打开、连节点、加变量时操作的那个"设计稿"。它只在编辑器时期有意义。
2. **蓝图编译出来的类**,名字叫 `BP_GameMode_C`,类型是 `UClass`。这才是游戏运行时真正拿来 `new` 出实例的那个类。`_C` 就是 "Class" 的意思,是引擎给蓝图生成类自动加的后缀。

打个比方:`BP_GameMode` 是图纸,`BP_GameMode_C` 是按图纸造出来的模具,游戏里每个 GameMode 实例都是这个模具压出来的。引擎运行时要的是模具,所以配置里写的是带 `_C` 的 `BP_GameMode_C`。如果你写成不带 `_C` 的 `BP_GameMode`,引擎拿到的是那张图纸而不是模具,实例化会失败。

什么时候要手写 `_C`、什么时候不用,有个规律:

- 在**配置文件**(`.ini`)和 **C++ 代码**里手动引用一个蓝图当"可实例化的类",得带 `_C`。这就是为什么 `DefaultEngine.ini` 里是 `BP_GameMode_C`。
- 在**编辑器界面**里,比如某个属性是 `TSubclassOf<...>`,下拉框让你选一个蓝图,你直接选 `BP_GameMode` 就行,引擎在背后帮你补上 `_C`,不用你操心。

C++ 里这俩对象对应两套加载函数,也能帮你记住区别:`LoadObject` 加载 `BP_GameMode`,拿到的是 `UBlueprint` 那张图纸;`LoadClass`(或引用 `BP_GameMode_C`)拿到的是 `UClass` 那个模具。平时跑游戏要的几乎都是后者。

回头再看那张配置表就通透了:`GlobalDefaultGameMode=/Game/Blueprints/BP_GameMode.BP_GameMode_C` 完整翻译过来是"游戏默认的 GameMode,用 `Content/Blueprints/BP_GameMode.uasset` 这个蓝图编译出的运行时类"。一串看着唬人的路径,拆开就这么点事。

## 打开 BP_GameMode,看蓝图到底加了什么

我们拿 `ARPGGameModeBase`(C++)和 `BP_GameMode`(蓝图)这一对来拆。先看 C++ 基类,`Source/ActionRPG/Public/RPGGameModeBase.h` 整个就这么点:

```cpp
UCLASS()
class ACTIONRPG_API ARPGGameModeBase : public AGameModeBase
{
    GENERATED_BODY()

public:
    ARPGGameModeBase();

    virtual void ResetLevel() override;
    virtual bool HasMatchEnded() const override;

    /** 游戏结束时调用,比如玩家死了、时间到了 */
    UFUNCTION(BlueprintCallable, Category=Game)
    virtual void GameOver();

protected:
    // 注意这两个:声明在 C++,实现留给蓝图
    UFUNCTION(BlueprintImplementableEvent, Category=Game, meta=(DisplayName="DoRestart"))
    void K2_DoRestart();

    UFUNCTION(BlueprintImplementableEvent, Category=Game, meta=(DisplayName="OnGameOver"))
    void K2_OnGameOver();

    UPROPERTY(BlueprintReadOnly, Category=Game)
    uint32 bGameOver : 1;
};
```

C++ 这边只定了骨架:游戏结束的状态位 `bGameOver`、一个能被外部调用的 `GameOver()`、两个空壳事件。具体游戏结束后画面怎么变、重开时怎么处理,C++ 一个字没写。这些留给 `BP_GameMode` 在编辑器里用节点连出来。

这就引出本篇第二个重点:C++ 和蓝图之间有两条方向相反的桥,这个头文件里两条都有,正好对照着讲。

### 桥一:BlueprintImplementableEvent —— C++ 声明,蓝图实现

看 `K2_OnGameOver` 这个函数,它标了 `BlueprintImplementableEvent`,而且在 C++ 里你找不到它的 `.cpp` 实现,因为根本不该有。这个宏的意思是:我 C++ 在这儿声明一个事件,但具体干什么由蓝图子类去画。

调用方向是 **C++ 调,蓝图执行**。GameMode 的 C++ 逻辑在某个时刻判断游戏结束了,就调一下 `K2_OnGameOver()`,执行流瞬间跳进 `BP_GameMode` 的事件图里,跑设计师连的那串节点(比如停止玩家输入、播放结束音效、弹出结算面板)。

把它理解成"C++ 留的回调钩子"就对了。基类决定**什么时候**触发,蓝图决定触发后**做什么**。`K2_` 前缀是 UE 的惯例(K2 是 Kismet 2 即蓝图系统的内部代号),配合 `meta=(DisplayName="OnGameOver")`,蓝图里看到的节点名是干净的 `OnGameOver`。

### 桥二:BlueprintCallable —— C++ 实现,蓝图调用

再看 `GameOver()`,标的是 `BlueprintCallable`。这个方向正好反过来:C++ 把函数实现写好,蓝图可以在自己的事件图里把它当一个节点拖出来调用。

调用方向是 **蓝图调,C++ 执行**。比如某个伤害结算的蓝图发现玩家血量归零,就在节点图里拉一个 `GameOver` 节点连上去,执行流进入 C++ 的 `ARPGGameModeBase::GameOver()` 实现,把 `bGameOver` 置位、做该做的清理。

一句话区分这两座桥:`BlueprintImplementableEvent` 是 C++ 向蓝图要实现,`BlueprintCallable` 是 C++ 给蓝图提供能力。前者实现体在蓝图,后者实现体在 C++。

顺带说个我踩过的坑:`BlueprintImplementableEvent` 的函数你**不能**在 `.cpp` 里写实现,写了会编译报重复定义。如果你既想给个 C++ 默认实现、又允许蓝图覆盖,那要用的是 `BlueprintNativeEvent`,实现写在 `_Implementation` 后缀的函数里。这俩别搞混。

## RPGTypes.h 为什么把一堆东西堆在一起

翻到 `Source/ActionRPG/Public/RPGTypes.h`,你会发现它有点怪:没有一个完整的类,塞的全是结构体、还有一长串 delegate 声明。文件头的注释把意图写得很直白:

```cpp
// This header is for enums and structs used by classes and blueprints accross the game
// Collecting these in a single header helps avoid problems with recursive header includes
```

翻译过来:这里集中放全游戏跨类共享的 enum 和 struct,集中放是为了躲开头文件递归 include 的麻烦。

这事 C++ 程序员一点就透。设想没有这个文件会怎样:背包变化的 delegate `FOnInventoryItemChanged` 带个 `URPGItem*` 参数,你可能想把它声明在 `RPGItem.h` 里。可 PlayerController 要用这个 delegate,于是 `RPGPlayerControllerBase.h` 得 include `RPGItem.h`。但 `RPGItem` 又反过来需要知道 slot 相关的类型……这种你引我我引你的关系一多,很容易绕成环,编译器直接报循环依赖。

UE 这种重度依赖头文件和代码生成(每个 `UCLASS`/`USTRUCT` 都会生成 `.generated.h`)的项目,这个问题被放大得很厉害。解法就是把这些"大家都要、谁也离不开"的基础类型抽出来,单独放进一个谁都能安全 include、自己却几乎不 include 别人的头文件。`RPGTypes.h` 顶上只 include 了 `PrimaryAssetId.h` 和自己的 `.generated.h`,干干净净,这就是它能当公共底座的原因。

里面那些 delegate 还藏着本项目一个贯穿始终的约定,每个变化都成对声明 dynamic 和 native 两个版本:

```cpp
// 动态版:蓝图能绑定,慢一点
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInventoryItemChanged, bool, bAdded, URPGItem*, Item);
// 原生版:纯 C++,快
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInventoryItemChangedNative, bool, URPGItem*);
```

带 `DYNAMIC` 的支持蓝图绑定、能被序列化,代价是走反射有性能开销;带 `Native` 后缀的是纯 C++ 委托,只能 C++ 绑,但快。这套 dynamic/native 配对在第五阶段讲背包数据流时会反复出现,这里先混个眼熟。新增任何跨类共享的 struct 或 delegate,按项目约定都该往这个文件放,别散落到各个类的头文件里。

## 给 C++ 程序员的心智转变

从纯 C++ 过来,最该调的一个观念是:**C++ 定义"能力",蓝图装配"数据与调参"。**

别再想着把所有逻辑都攥在 C++ 手里。在 UE 里,C++ 负责那些适合代码表达的东西:类的骨架、算法、性能敏感的循环、和引擎底层打交道的部分。而具体用哪个网格、默认背包放几瓶药、游戏结束弹哪个 UI、伤害公式里那几个系数填多少,这些经常要调、要让策划自己改的东西,留在蓝图和数据资产里。

回头看 `ARPGGameModeBase` 就是教科书级的示范:C++ 给了 `GameOver` 这个能力、`bGameOver` 这个状态、两个事件钩子,但一个具体数值、一个具体表现都没写死。`BP_GameMode` 在上面装配出真正跑起来的那套行为。基类是引擎,蓝图是仪表盘。

承认蓝图的地位,你才读得动这个项目。ActionRPG 一多半的 gameplay 逻辑都在 `Content/` 的蓝图里,你抗拒它,就等于自废一半视力。

## 验收清单

学完这阶段,拿下面几条自测,能答上来就算过了。

第一,给你任意一个名字带 `Base` 的 `RPG*Base` 类(`ARPGGameModeBase`、`URPGGameInstanceBase`、`ARPGPlayerControllerBase`、`ARPGCharacterBase` 都行),你应该能立刻反应过来:它名字里的 `Base` 就是在喊"我是基类,别直接用我"。它八成有个 `BP_*` 蓝图子类躺在 `Content/Blueprints/` 或相近目录,引擎实际用的是那个子类。验证方式:在内容浏览器里搜 `BP_`,右键蓝图选 Class Settings 看它的 Parent Class,就能确认它继承自哪个 C++ 基类。

第二,看到一个 `UFUNCTION`,你能凭 specifier 判断调用方向。`BlueprintImplementableEvent` 是 C++ 声明、蓝图实现、C++ 来调;`BlueprintCallable` 是 C++ 实现、蓝图来调。前者实现体在蓝图图表里找,后者在 `.cpp` 里找。

第三,有人问你为什么 ActionRPG 把共享类型都堆在 `RPGTypes.h`,你能答出"为了打断头文件循环依赖,顺便统一存放跨类的 enum、struct 和全部 delegate 声明"。

这三条通了,这套"基类被蓝图继承、引擎引用子类"的架构你就真懂了。它是后面每个阶段的入口套路:先在 `Source/` 里找到 `RPG*Base` 当锚点读懂能力,再去 `Content/` 找 `BP_*` 看装配。锚点找对了,后面的 Asset Manager、背包、GAS 才不会读得一头雾水。
