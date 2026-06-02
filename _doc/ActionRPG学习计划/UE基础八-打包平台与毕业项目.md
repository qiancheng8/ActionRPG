# 阶段八：打一个能跑的包，再造一把武器，给这个系列收尾

到这一步，你已经啃完了 GAS、背包、数据资产、UI。最后这篇不再引入新系统，而是把前面学的东西塞进一条完整的产出链：先把游戏打成一个能双击运行的包，再亲手造一把武器，让它从数据资产一路走到屏幕上砍出伤害。这两件事做完，你就算真正"会用"这个项目了，而不只是"读懂"它。

下面分四块讲：打包、平台、毕业项目、系列收尾。

## 一、先把它打成一个包

平时你按 Play 跑的是编辑器里的 PIE（Play In Editor），那不是真正的游戏，它跑在编辑器进程里，资产是从磁盘上的 `.uasset` 实时读的，连 GC 的时机都和正式版不一样。要拿到一个能发给别人、双击就能玩的东西，得走打包流程。

ActionRPG 有两个 Target，定义在 `Source/` 下：

```csharp
// Source/ActionRPG.Target.cs —— 打包用的游戏目标
public class ActionRPGTarget : TargetRules
{
    public ActionRPGTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;                          // 注意：Game，不是 Editor
        ExtraModuleNames.AddRange(new string[] { "ActionRPG" });
        DefaultBuildSettings = BuildSettingsVersion.V2;
    }
}
```

`ActionRPGEditorTarget` 那个除了 `Type = TargetType.Editor` 几乎一样。区别就在这个 `Type`：Editor 目标会链进整套编辑器代码（编辑器 UI、资产编辑器、各种工具模块），体积大、跑得慢，但能改东西；Game 目标只链运行时需要的代码，没有任何编辑器界面，开机直接进游戏。打包要的是后者。

### Cook 是什么

打包绕不开一个词：Cook（烘焙）。

编辑器里的资产是"通用格式"，里面塞了一堆只有编辑器才用得到的东西（编辑器缩略图、撤销历史、给所有平台的数据）。这种格式直接塞进发行包又大又慢，运行时根本加载不动。Cook 就是把这些通用资产转换成"某个具体平台、运行时能直接吃"的格式：纹理压成该平台的 GPU 格式，shader 编译成目标平台的字节码，编辑器专用数据全部剥掉。

一句话，Cook = 把"开发态资产"翻译成"某平台的运行态资产"。你打 WindowsNoEditor 包，Cook 出来的就是 Windows 专用数据；打 Android，就得重新 Cook 一遍成 Android 专用数据。所以换平台要重新 Cook，这也是为什么第一次打 Android 包那么慢。

### AlwaysCook 和那些不会被引用的资产

这里有个坑，正好和阶段四连上。Cook 默认只烘焙"被引用到的"资产。它从地图、默认类这些根节点出发，顺着引用链走，走得到的才 Cook，走不到的直接丢掉。

问题来了：ActionRPG 的物品是 Primary Data Asset，靠 `FPrimaryAssetId` 通过 Asset Manager **运行时按需加载**，没有人在编译期硬引用它们。引用链走不到它们，默认就会被 Cook 漏掉，结果就是打出来的包里背包空空如也，运行时加载物品直接失败。

解决办法就是你在阶段四见过的 `CookRule=AlwaysCook`。看 `Config/DefaultGame.ini` 里武器类型那行：

```ini
; Weapon 类型注册：扫描 /Game/Items/Weapons 目录，规则 AlwaysCook
+PrimaryAssetTypesToScan=(PrimaryAssetType="Weapon",AssetBaseClass=/Script/ActionRPG.RPGWeaponItem,bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/Items/Weapons")),SpecificAssets=,Rules=(Priority=-1,bApplyRecursively=True,ChunkId=-1,CookRule=AlwaysCook))
```

`AlwaysCook` 的意思是：不管有没有人引用，这个目录下的资产一律烘焙进包。Potion、Skill、Token、Weapon 四类全是这个规则。现在你回头看阶段四那句"加新物品类型要改三处"，第三处（`DefaultGame.ini` 的注册）真正的分量在打包这一步才显出来。你在编辑器里测的时候漏了这条也照样能跑，因为编辑器是实时读盘的；可一旦打包，漏注册的物品就被 Cook 无声地丢掉了。

### 实际打一个 WindowsNoEditor 包

命令行打包用引擎自带的 `RunUAT.bat`（UAT = Unreal Automation Tool），把引擎路径换成你自己装的：

```powershell
# 用 UAT 打一个 WindowsNoEditor 的 Development 包
& "<UE_4.27>\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
  -project="J:\...\ActionRPG\ActionRPG.uproject" `
  -noP4 `
  -platform=Win64 `              # 目标平台
  -clientconfig=Development `    # 配置：Development 带日志，Shipping 才是最终发行
  -cook -build -stage -pak -archive `   # 烘焙、编译、暂存、打 pak、归档
  -archivedirectory="J:\...\ActionRPG\Build\WindowsNoEditor"
```

不想敲命令行，编辑器里 `Platforms → Windows → Package Project` 一样能打，背后跑的是同一套 UAT。

`-pak` 这个开关值得单独说一句：加上它，Cook 出来的成千上万个小文件会被打进一个 `.pak` 大包，运行时引擎从这个大包里按偏移读资产。不加 `-pak` 就是松散文件（loose files），调试时方便单独替换，但发行版基本都用 pak，读取快、文件少、还能配合后面提到的加密和 chunk 分包。

仓库里的 `Build/WindowsNoEditor/` 已经有打包过程产生的中间产物（`FileOpenOrder` 之类），你可以拿它对照。最终能跑的包默认会出现在 `Saved/StagedBuilds/WindowsNoEditor/` 下，里面是一个 `ActionRPG.exe` 加一坨 `Content/Paks/*.pak`。

### 打包版和编辑器跑的，到底差在哪

记住几条，调试时能省很多命：

- 包里**没有编辑器**，你不能在运行时改资产、看 Outliner。出了问题只能看日志（Development 配置会在 `Saved/Logs/` 留日志，Shipping 默认连日志都精简掉）。
- 包里只有被 Cook 进去的资产。编辑器能跑、打包跑不了，第一反应就该怀疑"是不是漏 Cook 了"。
- 路径和大小写。Windows 文件系统不区分大小写，但 Android/iOS 区分，编辑器里能加载、上手机找不到资源，多半是引用路径大小写对不上。
- 性能表现不一样。PIE 跑在编辑器进程里，开销和真包差很远，想评估帧率必须看打包版（最好是 Shipping）。

## 二、平台意识，以及它如何反向塑造了整个项目

打开 `ActionRPG.uproject` 看目标平台，会发现这个"示例项目"瞄准的是四个平台：Android、iOS、WindowsNoEditor、MacNoEditor。它本质上是一个**移动游戏**，PC 只是顺带。这件事是理解前七篇所有设计的钥匙。

平台差异在 `ActionRPG.Build.cs` 里有最直接的体现：

```csharp
if (Target.Platform == UnrealTargetPlatform.IOS)
{
    // 只有 iOS 才链 OnlineSubsystem，以及 Facebook 登录、iOS 广告
    PrivateDependencyModuleNames.AddRange(new string[] { "OnlineSubsystem", "OnlineSubsystemUtils" });
    DynamicallyLoadedModuleNames.Add("OnlineSubsystemFacebook");
    DynamicallyLoadedModuleNames.Add("OnlineSubsystemIOS");
    DynamicallyLoadedModuleNames.Add("IOSAdvertising");
}
else if (Target.Platform == UnrealTargetPlatform.Android)
{
    PrivateDependencyModuleNames.AddRange(new string[] { "OnlineSubsystem", "OnlineSubsystemUtils" });
    DynamicallyLoadedModuleNames.Add("AndroidAdvertising");
    DynamicallyLoadedModuleNames.Add("OnlineSubsystemGooglePlay");  // 安卓接 Google Play
    // ... 还往 APK 里塞了圆形图标的 UPL 配置
}
```

`Build.cs` 是编译期跑的 C# 脚本，相当于条件编译的依赖声明。Windows 包压根不会链进这些 OnlineSubsystem 模块，因为 PC 上不需要 Google Play 登录和移动广告。同一份源码，按目标平台链不同的依赖，这就是 UE 跨平台的底层做法之一。

更值得琢磨的是，移动端的硬约束怎么一路倒逼了前面那些"看起来很讲究"的架构：

**内存紧、加载慢 → 异步加载和数据资产。** 手机内存就那么点，不可能开机把所有物品、技能、模型全塞进内存。所以物品做成 Primary Data Asset，用 `FPrimaryAssetId` 标识，需要谁才异步加载谁（阶段四、五）。阶段七里我们吐槽过 `ForceLoadItem` 是同步加载会卡顿，正是因为目标是手机，一次主线程卡顿在 60Hz 的手机上就是肉眼可见的掉帧，所以正经路径必须走 Asset Manager 的异步加载。数据驱动也是同一逻辑的延伸：物品的数值、引用全放在数据资产里，能单独 Cook、单独按需加载，而不是焊死在代码里。

**要做加载屏遮住卡顿 → 独立的 LoadingScreen 模块。** 阶段七那个 `ActionRPGLoadingScreen` 模块为什么要独立出来、还得在主模块之前的 `PreLoadingScreen` 阶段加载？因为手机上关卡和资产加载慢，必须先把加载屏顶上去遮丑。它不能依赖主模块的任何类型，否则就得等主模块加载完才能显示，那就失去意义了。

**GAS 的网络化设计也是这个味儿。** GAS 整套属性、效果、能力都是按网络复制设计的（GameplayEffect 在服务器算、复制到客户端）。即便 ActionRPG 本身是单机，这套数据驱动加复制友好的结构，恰恰是为"可能要联网、要在弱网手机上同步状态"留的余量。

所以这个项目里那些让你觉得"绕"的设计，很多不是为了炫技，是"因为要上手机"逼出来的。把这条主线记住，前七篇就串成一根线了。

## 三、毕业项目：从零造一把武器

光说不练等于没学。这个项目逼你把阶段四到七全用上：造数据资产（四）→ 进背包装进槽位（五）→ 触发能力施加效果（六）→ UI 上看到、运行验证（七）。下面给完整步骤。

先认清楚要碰的那几个类。武器物品是 `URPGWeaponItem`，它继承 `URPGItem`：

```cpp
// RPGWeaponItem.h —— 构造时就把 ItemType 钉成 Weapon 类型
URPGWeaponItem() { ItemType = URPGAssetManager::WeaponItemType; }
```

而 `URPGItem` 上有两个关键字段，是把物品和 GAS 接起来的桥：

```cpp
// RPGItem.h
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Abilities)
TSubclassOf<URPGGameplayAbility> GrantedAbility;   // 这件物品授予的能力
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Abilities)
int32 AbilityLevel;                                 // 能力等级，参与伤害计算
```

`GrantedAbility` 就是装备这件武器后玩家能放的招。下面开造。

### 第 1 步：做一个新的 GameplayEffect（效果）

到内容浏览器 `Content/Abilities/` 下，参考已有的伤害 GE 复制一份，命名 `GE_MyWeaponDamage`。打开它：

- Duration Policy 设 **Instant**（瞬间生效，一次性扣血，不是持续 buff）。
- Executions 里挂上 `URPGDamageExecution`。这个执行体就是阶段六讲的那个，它读攻击者 `AttackPower`、目标 `DefensePower` 和 base damage，算出 `Damage` 临时属性，最后由 `URPGAttributeSet::PostGameplayEffectExecute` 转成扣 `Health`。
- 给它配一个偏高的 base damage，比如 50，方便等会儿一眼看出"这把新武器确实更疼"。

### 第 2 步：做一个新的 GameplayAbility（能力）

`Content/Abilities/` 下，照着现成的近战能力（比如挥砍那个）复制一份，命名 `GA_MyWeaponAttack`。这是一个 `URPGGameplayAbility` 的蓝图子类。要点：

- 在它的 `EffectContainerMap` 里，把某个触发标签（比如 `Ability.Damage` 这类，看 `Config/DefaultGameplayTags.ini` 里已有的）映射到第 1 步的 `GE_MyWeaponDamage`。这就是阶段六说的"按触发标签成套施加效果"。
- 能力激活后用 `RPGAbilityTask_PlayMontageAndWaitForEvent` 播放攻击蒙太奇，等动画里命中帧那个 gameplay 事件触发时，再调 `ApplyEffectContainerSpec` 把效果打到命中目标身上。直接复用现成能力的图最省事，你只要把它引用的 GE 换成自己的。

### 第 3 步：做武器数据资产

内容浏览器进 `Content/Items/Weapons/`（已有 `Weapon_Sword_2`、`Weapon_Axe` 这些可参考）。右键 → Miscellaneous → Data Asset，类选 `RPGWeaponItem`，命名 `Weapon_MyBlade`。打开它填：

- **GrantedAbility**：选第 2 步的 `GA_MyWeaponAttack`。装备这把武器，玩家就能放这个招。
- **AbilityLevel**：填个 2 或 3，它会进伤害计算。
- 把 ItemName、图标、武器模型这些填一下，否则 UI 上是空白，运行时也看不到武器模型。直接抄旁边 `Weapon_Axe` 的填法即可。

注意它必须放在 `/Game/Items/Weapons` 目录下。Asset Manager 就是按 `DefaultGame.ini` 里注册的这个目录扫的，放别处它根本扫不到，背包里永远不会出现。这一步直接呼应阶段四。

### 第 4 步：在 UI 槽位里装备它

进游戏（先用 PIE 测，方便）。打开背包/装备界面，新武器应该已经出现在物品列表里（如果没出现，回去查第 3 步的目录和命名）。把它拖进武器槽位。

这一拖背后发生的，正是阶段五加六的联动：装备改了 `SlottedItems` 这张 map → 触发 `OnItemSlotChanged` 委托 → character 的 `RefreshSlottedGameplayAbilities` / `FillSlottedAbilitySpecs` 跟着重算，把旧武器的能力撤掉、把 `Weapon_MyBlade.GrantedAbility`（也就是 `GA_MyWeaponAttack`）授予给玩家的 ASC。UI 的刷新则是阶段七那套 delegate 自动驱动的。

### 第 5 步：运行验证

按攻击键。该看到的是：玩家播你配的攻击蒙太奇（动画对了，说明能力激活、AbilityTask 跑通了），命中敌人时敌人掉血，而且掉的血量明显比默认武器多（伤害对了，说明 GE → DamageExecution → Health 这条管线通了，AbilityLevel 和 base damage 都吃上了）。

动画和掉血都对，恭喜，你把四个阶段一次性验证完了。

### 别忘了打包再验一次

最后一脚：按第一节的命令重新打个 WindowsNoEditor 包，进包里再装备一次这把新武器。能正常掉血，才说明你的新武器资产被 `AlwaysCook` 规则正确烘焙进了包。这一步是阶段八和阶段四的合龙，也是整个毕业项目真正闭环的地方。万一编辑器里好好的、打包后武器不见了，回去查 `DefaultGame.ini` 的 Weapon 注册和资产目录。

## 四、给坚持到这的你，几句真心话

能一路读到这，说明你已经跨过了 C++ 程序员学 UE 最难受的那道坎：接受"代码不是全部"。

最开始最容易犯的错，是想把所有逻辑搬回 C++，把蓝图当成"给美术玩的低级玩意"。真上手才发现，这个项目一大半 gameplay 逻辑就在蓝图里，你不读蓝图就等于只看了半本书。把蓝图当成"可视化的、给设计师用的 C++"，心态就顺了。

读这个项目最高效的姿势，前面每一篇都在用：从 `Source/` 里那个 `RPG*Base` 基类进去，那是你的主场，看它暴露了什么；再去 `Content/` 里找对应的 `BP_*` 子类，看设计师在上面装配了什么数据。代码定义能力、蓝图装配数据，这个分工吃透了，再大的 UE 项目你也有抓手。

还有个习惯务必养成：改了就跑。UE 太多行为是运行时才暴露的，GC 什么时候回收、资产什么时候加载、委托什么时候触发，光盯着代码看会漏掉一半。毕业项目那五步，每一步都让你跑一次，不是啰嗦，是因为静态读真的看不出来。

GAS 是这个项目的硬骨头，也是你以后最值钱的技能之一。它陡峭到光看 ActionRPG 源码会很吃力，因为这里用了 ExecutionCalculation、EffectContainer、自定义 AbilityTask 这些进阶玩法。配合官方 GAS 文档和社区的《GASDocumentation》一起啃，会顺很多。

最后提醒一句老生常谈：`.uasset` 和 `.umap` 是二进制，没法 diff、没法 merge。改之前先想清楚团队协作的影响，多人改同一个资产基本只能靠约定独占。这个不是技术问题，是会真的把人坑哭的工程问题。

## 验收清单

走完这一篇，对照检查：

- [ ] 能独立用 `ActionRPGTarget` 打出一个能双击运行的 WindowsNoEditor 包。
- [ ] 能讲清 Cook 是什么、`AlwaysCook` 为什么对 Primary Data Asset 是必须的、打包版和 PIE 的差异。
- [ ] 能说出移动端约束如何反向塑造了异步加载、数据驱动、独立加载屏模块、GAS 网络化这些设计。
- [ ] 独立完成毕业项目：新建 `URPGWeaponItem` 数据资产 → 绑定新 `URPGGameplayAbility` → 施加自定义 `GameplayEffect` → UI 槽位装备 → 运行并打包都验证了伤害和动画。
- [ ] 能用自己的话讲清这把武器是怎么贯穿 Asset Manager → 背包/装备槽 → GAS → UI 这条链的。

这五条都打上勾，这个系列对你就算结业了。剩下的，去做你自己的项目吧。
