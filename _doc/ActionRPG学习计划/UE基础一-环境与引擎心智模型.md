# 从 C++ 程序员到 UE 开发者：先把 ActionRPG 跑起来，再换脑子

我假设你和我一样，C++ 写了不少年，但从没碰过 UE 的正向开发。打开 ActionRPG 这个项目，第一反应大概是「源码在哪、main 函数在哪、怎么编译」。结果翻了一圈，连个 CMakeLists 都没有，`.cpp` 文件里全是看不懂的宏。别慌，这篇就是带你把这套工具链跑通，顺便把脑子里那套「裸 C++」的世界观换成「UE 的世界观」。

这是 ActionRPG 学习计划的阶段一。目标只有三件事：能编译、能打开编辑器、能进游戏玩一遍。然后在这个过程里，把几个绕不开的概念啃下来。

## 约定：
- 小编的项目目录为：J:\_ALL\CODE\codeup.aliyun.com\_jy\_ue4.27.2\ActionRPG，遇到该路径时替换成你的工作目录。

## 先认清这个项目长什么样

ActionRPG 是 Epic 官方的示例项目，对应引擎版本是 4.27（看 `ActionRPG.uproject` 里的 `"EngineAssociation": "4.27"`）。它演示的是 GameplayAbilities（简称 GAS）、资产管理器、存档背包这几套系统。

有件事现在就要记住：这个项目绝大部分游戏逻辑不在 C++ 里，而在 `Content/` 目录下的蓝图里。`Source/` 里的 C++ 只提供「可以被蓝图继承的基类」。所以你想靠读 `.cpp` 把整个游戏看明白，是行不通的。C++ 在这里负责定义能力，蓝图负责装配数据和接线。这个分工现在先有个印象，后面阶段会反复遇到。

## 第一步：生成 VS 工程文件

UE 项目的源码不是直接拿 VS 打开的，得先由引擎根据 `.uproject` 和各种 `.cs` 配置生成一套 `.sln` / `.vcxproj`。这套生成出来的工程文件本质上是给 IDE 看的壳子，真正的编译规则在 UE 自己的 UnrealBuildTool（UBT）手里。

最省事的办法是在资源管理器里右键 `ActionRPG.uproject`，选「Generate Visual Studio project files」。

![image-20260602172018620](J:\_ALL\CODE\codeup.aliyun.com\_jy\_ue4.27.2\ActionRPG\_doc\ActionRPG学习计划\UE基础一-环境与引擎心智模型.assets\image-20260602172018620.png)

如果右键没这一项（菜单项偶尔会丢，尤其是装了多个引擎版本时），就用命令行，把引擎路径换成你自己的安装位置：

```powershell
# 用引擎自带的批处理生成工程文件
& "<UE_4.27>\Engine\Build\BatchFiles\GenerateProjectFiles.bat" "J:\_ALL\CODE\codeup.aliyun.com\_jy\_ue4.27.2\ActionRPG\ActionRPG.uproject"
```

> 部分电脑安装多个版本的UE，会导致右键执行报错（或者执行了非指定版本的sln文件），这时候可以通过下面命令进行操作：
>
> ![image-20260602172039765](J:\_ALL\CODE\codeup.aliyun.com\_jy\_ue4.27.2\ActionRPG\_doc\ActionRPG学习计划\UE基础一-环境与引擎心智模型.assets\image-20260602172039765.png)
```
$ubt = "J:\Program Files\Epic Games\UE_4.27\Engine\Binaries\DotNET\UnrealBuildTool.exe";
$proj = "J:\_ALL\CODE\codeup.aliyun.com\_jy\_ue4.27.2\ActionRPG\ActionRPG.uproject";
$ubt -projectfiles -project="$proj" -game -rocket -progress
```

> 如果你想通过UE源码编译项目（能调试引擎代码），修改上述代码中的`UnrealBuildTool.exe`路径即可。

跑完你会在项目根目录看到 `ActionRPG.sln`。注意这个 `.sln` 是生成物，不该进版本库，改了 C++ 文件名或加了新文件，重新生成一次就行。

## 第二步：编译编辑器目标

这里有个新手最容易懵的点。UE 不是「编译一个程序」，而是「编译某个 Target」。这个项目有两个 Target，待会儿专门讲区别。现在你要编的是编辑器目标 `ActionRPGEditor`：

```powershell
# 编译编辑器目标，Win64 平台，Development 配置
& "<UE_4.27>\Engine\Build\BatchFiles\Build.bat" ActionRPGEditor Win64 Development "J:\_ALL\CODE\codeup.aliyun.com\_jy\_ue4.27.2\ActionRPG\ActionRPG.uproject" -waitmutex
```

几个参数解释一下：`ActionRPGEditor` 是 Target 名，`Win64` 是平台，`Development` 是编译配置（带调试信息但开了优化，日常开发就用它）。`-waitmutex` 让它等其他 UBT 进程释放锁，避免你同时开着编辑器时报冲突。

第一次编译会比较久，因为要编引擎依赖。编出来的产物是一个 DLL，编辑器启动时把它当模块加载进去。这点和你熟悉的「编译出一个 exe 双击运行」不一样，UE 编辑器本体是引擎提供的 `UE4Editor.exe`，你的项目代码是挂在它上面的动态模块。

你当然也可以直接在 VS 里选 `ActionRPGEditor` 配置按 F5，效果一样，背后调的还是 UBT。

## 第三步：打开编辑器，进游戏玩一遍

```powershell
# 用编辑器打开项目
& "<UE_4.27>\Engine\Binaries\Win64\UE4Editor.exe" "J:\_ALL\CODE\codeup.aliyun.com\_jy\_ue4.27.2\ActionRPG\ActionRPG.uproject"
```

编辑器起来后，工具栏上有个 Play 按钮（快捷键 Alt+P）。点下去就在编辑器窗口里跑起游戏了，这个模式叫 PIE（Play In Editor）。先别急着看代码，老老实实当个玩家把游戏玩一遍：走两步、攻击、捡装备、用药水。你对这游戏有了体感，后面读到「伤害管线」「物品槽授予能力」这些抽象描述时，脑子里才有画面对得上。

阶段一的验收里有一条是「改一行 C++ → 编译 → 运行看到效果」，文末会给一个具体例子，但得等你把概念铺垫读完才好动手。

## 两个 Target 的区别

打开 `Source/` 目录，你会看到两个 `.Target.cs` 文件。它们是 UBT 的配置，用 C# 写的（对，UE 的构建脚本是 C#，不是 C++）。

`ActionRPGEditor.Target.cs` 内容很短，核心就一行：

```csharp
Type = TargetType.Editor;   // 这是编辑器目标
ExtraModuleNames.AddRange(new string[] { "ActionRPG" });
```

`ActionRPG.Target.cs` 几乎一样，只差类型：

```csharp
Type = TargetType.Game;     // 这是打包游戏目标
```

区别就在这个 `Type`。`Editor` 目标编出来的是带编辑器功能的模块，能在 UE4Editor 里跑、能用 PIE、能改资产。`Game` 目标编出来的是脱离编辑器的独立游戏，也就是你打包发给玩家的那份。打包游戏里不会带编辑器那一大坨工具代码，体积小、跑得快。

阶段一你只需要 `Editor` 目标。`Game` 目标和打包是阶段八的事，现在知道有这么个东西、知道它俩靠 `TargetType` 区分就够了。两个 Target 都设了 `DefaultBuildSettings = BuildSettingsVersion.V2`，这是一组默认编译选项的版本号，暂时不用管。

## 两个模块，以及为什么加载屏要单独拎出来

Target 是「编译目标」，模块（Module）是「代码的组织单元」。一个 Target 可以包含多个模块。看 `ActionRPG.uproject` 的 `Modules` 段：

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

`ActionRPG` 是主模块，所有 gameplay 代码都在这。它的类型是 `Runtime`、加载阶段是 `Default`，没什么特别。

有意思的是第二个模块 `ActionRPGLoadingScreen`。它干的事很单一，就是显示加载屏（启动时那段影片或者 loading 画面）。为什么不把这点代码塞进主模块、非要单开一个？答案藏在 `LoadingPhase` 上。它是 `PreLoadingScreen`，意思是「在主模块加载之前就先把我加载起来」。

道理也好理解：加载屏的使命是在主模块和大堆资产还没就绪的时候，先给玩家一个不黑屏的画面。如果加载屏代码跟主模块绑在一起，那就得等主模块加载完才能显示加载屏，逻辑上自相矛盾。所以它必须是个独立模块，且加载时机要排在主模块前面。这个模块的 `.Build.cs` 里有句注释把话挑明了：

> This module must be loaded "PreLoadingScreen" in the .uproject file, otherwise it will not hook in time!
>
> 此模块必须在 .uproject 文件中以“PreLoadingScreen”的名称进行加载，否则将无法及时加载！

还有个约束你后面会用到：这个加载屏模块不能反向依赖主模块的类型。看它的 `ActionRPGLoadingScreen.Build.cs`，依赖里只有 `Core / CoreUObject / Engine / MoviePlayer / Slate / SlateCore / InputCore`，没有 `ActionRPG`。因为它加载得更早，那时主模块的类还不存在，依赖了就会出问题。主模块反过来通过 `IActionRPGLoadingScreenModule` 接口去用它。这种「早加载的模块不能依赖晚加载的模块」的约束，是 UE 模块体系里很典型的一类坑。

## 换脑子：UE 的 C++ 和你熟的 C++ 不是一回事

工具链跑通了，接下来这部分才是阶段一真正的硬骨头。你打开任何一个 `.h` 文件，都会被一堆宏糊一脸。这些不是花架子，是 UE 整套对象系统的入口。

### UObject、反射、垃圾回收

UE 有自己的一套对象系统，核心基类叫 `UObject`。凡是继承自它的类，引擎都能在运行时知道这个类有哪些属性、哪些函数、叫什么名字。这种「程序运行时还能反过来查自己结构」的能力叫`反射`，C++ 标准里没有，UE 靠代码生成硬造了一套。

反射带来的最直接好处是垃圾回收（GC）。`UObject` 是被 GC 管理的，你不用手动 delete。但这里有个新手必踩的坑：GC 怎么知道某个对象还在被用、不能回收？靠的就是 `UPROPERTY` 标记的指针。

举个例子，`RPGCharacterBase.h` 里有这么一句：

```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Abilities)
TArray<TSubclassOf<URPGGameplayAbility>> GameplayAbilities;
```

这个成员被 `UPROPERTY` 标记了，GC 就把它当成一条「引用」记账。只要这个角色还活着，它引用的能力类就不会被回收。反过来，如果你写一个**裸指针**指向 UObject，又**没**加 `UPROPERTY`，GC 完全看不见这条引用，可能在某次回收里把对象干掉，留你一个野指针。这是从裸 C++ 转过来的人最容易栽的地方，记住一句话：指向 UObject 的成员指针，要么加 `UPROPERTY`，要么你非常清楚自己在干什么。

### 那一堆宏分别是干嘛的

- `UCLASS()`：声明这是个反射类（必须继承 `UObject` 或其子类）。配套要在类体里写 `GENERATED_BODY()`，这是代码生成器塞东西的占位。
- `USTRUCT()`：声明一个反射结构体，比如 `RPGTypes.h` 里那些 `FRPGItemSlot`。
- `UENUM()`：声明一个能被编辑器和蓝图识别的枚举。
- `UPROPERTY()`：标记一个成员变量，让它进反射系统、被 GC 追踪、能在编辑器面板里显示。
- `UFUNCTION()`：标记一个成员函数，让它能被蓝图调用、被反射、或者作为网络 RPC。

`UPROPERTY` 和 `UFUNCTION` 括号里的修饰符是你天天要打交道的，常见这几个：

- `EditAnywhere`：这个属性能在编辑器的细节面板里改。
- `BlueprintReadWrite` / `BlueprintReadOnly`：蓝图能不能读写这个属性。
- `BlueprintCallable`：蓝图能调这个函数（C++ 实现，蓝图调用）。
- `Category="Abilities"`：在编辑器面板里把它归到哪个分组，纯粹是为了界面整洁。

看 `RPGCharacterBase.h` 里这个真实例子就懂了：

```cpp
UFUNCTION(BlueprintCallable)
virtual float GetHealth() const;   // C++ 实现，蓝图里能直接拖出这个节点调用
```

还有一个方向相反的修饰符叫 `BlueprintImplementableEvent`，意思是 C++ 只声明、由蓝图去实现。这俩方向的桥接是阶段二的重点，这里先混个眼熟。

### 命名前缀不是装饰，是规则

UE 的类名首字母是有强制含义的，编译器和代码生成器会校验：

- `U` 开头：继承自 `UObject` 的类，比如 `URPGItem`、`URPGGameplayAbility`。
- `A` 开头：继承自 `AActor` 的类（Actor 是能放进关卡的对象），比如 `ARPGCharacterBase`。
- `F` 开头：普通结构体，比如 `FRPGItemSlot`。
- `I` 开头：接口，比如 `IRPGInventoryInterface`。
- `E` 开头：枚举。

这套前缀不是建议，是硬规定。你把一个 `UObject` 子类命名成 `A` 开头，UHT（代码生成器）会直接报错。本项目在这套规则之上，又统一加了 `RPG` 业务前缀，所以你看到的是 `ARPGCharacterBase` 这种 `A` + `RPG` + 业务名的组合。

### ACTIONRPG_API 是什么

你会发现几乎每个 public 类前面都挂着 `ACTIONRPG_API`：

```cpp
UCLASS()
class ACTIONRPG_API ARPGCharacterBase : public ACharacter, ...
```

前面说过，模块编出来是 DLL。这个宏就是控制符号导出/导入的，让别的模块能链接到这个类。它是个根据模块名自动生成的宏，规则是 `模块名大写_API`。你自己加新类的时候，如果想让别的模块用，就照着挂上；纯内部用的可以不挂。机制上等价于 Windows 那套 `__declspec(dllexport/dllimport)`，UE 帮你包好了。

### Build.cs 是 UE 版的依赖声明

回到 `ActionRPG.Build.cs`。这个文件相当于在告诉 UBT「我这个模块依赖哪些别的模块」。它分两类依赖：

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine"
});

PrivateDependencyModuleNames.AddRange(new string[] {
    "ActionRPGLoadingScreen", "Slate", "SlateCore", "InputCore",
    "MoviePlayer", "GameplayAbilities", "GameplayTags", "GameplayTasks", "AIModule"
});
```

`Public` 依赖会传递给依赖本模块的人，`Private` 不会，差不多就是 CMake 里 `PUBLIC` 和 `PRIVATE` 链接的味道。从这份清单能直接读出这个项目用了什么：`GameplayAbilities / GameplayTags / GameplayTasks` 是 GAS 那一套（阶段六的主菜），`AIModule` 是敌人 AI，`MoviePlayer` 给加载屏放影片用。

文件后半段还有个值得一看的写法：

```csharp
if (Target.Platform == UnrealTargetPlatform.IOS)
{
    PrivateDependencyModuleNames.AddRange(new string[] { "OnlineSubsystem", "OnlineSubsystemUtils" });
    // ...
}
else if (Target.Platform == UnrealTargetPlatform.Android)
{
    // ...
}
```

因为 Build.cs 是 C# 脚本而不是死的配置，它能根据目标平台动态加依赖。这里只在 iOS / Android 上才拉进 OnlineSubsystem。这也提醒你，ActionRPG 是个跨移动端的项目（`.uproject` 里 `TargetPlatforms` 列了 Android、iOS、WindowsNoEditor、MacNoEditor），后面很多架构选择都是被移动端约束逼出来的。

## 几个跑不掉的运行时概念

阶段一验收要求你能说清 Actor、Component、GameMode、GameInstance、PlayerController 各是什么。这里给个能对上号的最小版本，细节后面阶段会展开。

- **Actor**：能被放进关卡的东西，有位置、能在世界里存在。角色、敌人、宝箱、触发器都是 Actor。它有完整的生命周期：`BeginPlay`（进游戏时）到 `EndPlay`（销毁时），中间每帧可能跑 `Tick`。本项目的 `ARPGCharacterBase` 就是 Actor（准确说是 Actor 的子类 Character）。
- **Component**：挂在 Actor 身上的功能零件，自己`不能单独存在于世界里`。比如能力系统组件 `URPGAbilitySystemComponent` 就是挂在角色上的。一个 Actor 由若干 Component 拼出能力，这是 UE 推崇的组合优于继承。
- **GameMode**：定义这一局游戏的规则，比如用哪个角色类、谁是默认控制器、胜负怎么判。它只在服务器/单机端存在，客户端没有。本项目是 `ARPGGameModeBase`。
- **GameInstance**：比一局游戏活得更久的全局对象。从游戏启动到退出，它一直在，切关卡也不销毁。所以存档、背包这种「跨关卡要保留」的数据挂在它身上，本项目是 `URPGGameInstanceBase`。
- **PlayerController**：代表「玩家这个操控者」，负责把输入翻译成游戏里的动作。本项目把背包数据放在 `ARPGPlayerControllerBase` 上，这是 ActionRPG 的一个设计选择，阶段五会细说。

生命周期长短排个序大致是：GameInstance（整个进程）> GameMode/关卡里的 Actor（一局/一个关卡）> 每帧的 Tick。记住「数据该挂在哪个对象上」往往取决于它需要活多久，这个判断后面会反复用到。

## 验收清单：改一行 C++ 看到效果

概念铺完了，来闭环。下面这个改动能让你确认整条工具链是通的。

打开 `Source/ActionRPG/Public/RPGCharacterBase.h`，找到 `GetHealth` 那个声明，照葫芦画瓢加一个返回固定值的新函数声明：

```cpp
// 在 RPGCharacterBase.h 里，public 区域
UFUNCTION(BlueprintCallable, Category = "Debug")
float GetMagicNumber() const;
```

再到对应的 `.cpp`（`Source/ActionRPG/Private/RPGCharacterBase.cpp`）里给个实现：

```cpp
float ARPGCharacterBase::GetMagicNumber() const
{
    return 42.0f;
}
```

然后重新编译编辑器目标（前面那条 `Build.bat ActionRPGEditor ...`，或者 VS 里 F5）。编译过了，说明你的反射宏写对了、模块依赖没破、导出符号正常。这就完成了「改一行 C++ → 编译」这半程。

剩下「运行看到效果」那半程，得等到阶段二三学会怎么在蓝图里把这个 `BlueprintCallable` 函数连进 Event Graph、或者直接在角色蓝图里打印它的返回值。所以阶段一的验收，能编译过、能进 PIE 玩一遍，就算达标了。

对照一下你现在应该能答上来的几个问题：两个 Target 差在哪、加载屏为什么单独成模块、`UPROPERTY` 和 GC 什么关系、`U`/`A`/`F`/`I`/`E` 各代表什么、GameInstance 和 PlayerController 谁活得久。这几个都顺了，阶段一的脑子就换过来了，可以进阶段二去追那条「C++ 基类到蓝图子类」的配置链了。

