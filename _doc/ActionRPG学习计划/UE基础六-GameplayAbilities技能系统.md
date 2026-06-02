# 阶段六：啃下 GameplayAbilities（GAS）——ActionRPG 的心脏

先说句大实话。GAS 是 UE 里出了名劝退的系统，没有之一。它的概念多、缩写多、回调链长，官方文档写得又抽象，很多人卡在这里几个月。我自己第一次读 ActionRPG 的 GAS 代码时，盯着 `PostGameplayEffectExecute` 那一坨 `Cast` 看了半天，完全不知道控制流是怎么走到这里的。

所以这篇我换个讲法。不一上来堆术语，而是带你顺着一次"砍一刀掉血"的完整过程，把每一棒交接看清楚。你有 C++ 基础，这其实是优势，因为 ActionRPG 的 GAS 核心逻辑全在 `Source/` 里，是你能直接读的真代码，不像别的部分埋在蓝图二进制里。

读不懂没关系，这很正常。建议这一节配合官方 GAS 文档和社区那本《GASDocumentation》一起啃，别赶进度。

---

## 6.1 GAS 总览：先认全这几大件

ActionRPG 的角色基类长这样（`Source/ActionRPG/Public/RPGCharacterBase.h`）：

```cpp
UCLASS()
class ACTIONRPG_API ARPGCharacterBase : public ACharacter, public IAbilitySystemInterface, public IGenericTeamAgentInterface
{
    GENERATED_BODY()
    // ...
    /** 实现 IAbilitySystemInterface，GAS 全靠这个接口找到 ASC */
    UAbilitySystemComponent* GetAbilitySystemComponent() const override;
protected:
    /** 处理能力系统交互的组件，也就是 ASC */
    UPROPERTY()
    URPGAbilitySystemComponent* AbilitySystemComponent;

    /** 被能力系统修改的属性列表，也就是属性集 */
    UPROPERTY()
    URPGAttributeSet* AttributeSet;
};
```

记住这个结构：角色实现了 `IAbilitySystemInterface`，身上挂了一个 ASC 和一个 AttributeSet。GAS 里任何东西想找到一个角色的能力系统，都是先拿到这个 Actor，问它要 `GetAbilitySystemComponent()`。这就是接口存在的意义，谁都不用关心你的 ASC 具体藏在哪个成员变量里。

下面五个大件，先建立粗糙的心智模型，细节后面慢慢补。

**ASC（AbilitySystemComponent，能力系统组件）**。它是整套系统的总管家。每个参与 GAS 的角色身上挂一个，负责持有能力、施加效果、管理属性的复制。本项目的 `URPGAbilitySystemComponent` 只是在官方 `UAbilitySystemComponent` 上加了几个查询工具函数（`Source/ActionRPG/Public/Abilities/RPGAbilitySystemComponent.h`），它的注释自己都说了："大多数游戏都需要做一个游戏专属子类来提供工具函数"。

**GameplayAbility（能力）**。一个"技能"，比如普攻、火球术、喝药。它定义了"激活时做什么"。本项目的基类是 `URPGGameplayAbility`。

**GameplayEffect（效果，简称 GE）**。对属性的一次改动，比如"扣 50 血""加 20% 攻击力，持续 10 秒"。能力激活后，真正去改数值的活是 GE 干的。能力负责"决定打谁、放什么动画"，改数值这种脏活交给 GE。

**AttributeSet（属性集）**。一堆数值的容器，血量、蓝量、攻击力都在这。`URPGAttributeSet` 就是它。

**GameplayTag（标签）**。一个层级化的字符串标识，比如 `Ability.Melee.Close`。GAS 几乎所有的"匹配""分类""拦截"都靠标签，而不是靠 `enum` 或者 `bool`。

打个比方。把角色想象成一个 RPG 人物卡。AttributeSet 是卡片上印着的数值栏，GameplayEffect 是别人往你卡上贴的便利贴（扣血 -50、加攻 +5），GameplayAbility 是你能主动打出的招式卡，ASC 是管这整张卡桌的裁判，而 GameplayTag 是贴在每张卡角上的彩色标签，裁判靠它快速判断谁能跟谁交互。

---

## 6.2 属性与伤害管线：从 C++ 程序员最舒服的地方切入

属性集是 GAS 里最像"普通 C++ 数据类"的部分，从这切入最舒服。

### 属性长什么样

看 `URPGAttributeSet`（`Source/ActionRPG/Public/Abilities/RPGAttributeSet.h`）：

```cpp
/** 当前血量，为 0 时角色死亡。受 MaxHealth 限制 */
UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing=OnRep_Health)
FGameplayAttributeData Health;
ATTRIBUTE_ACCESSORS(URPGAttributeSet, Health)

/** 攻击者的 AttackPower 会乘到基础 Damage 上，1.0 表示无加成 */
UPROPERTY(BlueprintReadOnly, Category = "Damage", ReplicatedUsing = OnRep_AttackPower)
FGameplayAttributeData AttackPower;
ATTRIBUTE_ACCESSORS(URPGAttributeSet, AttackPower)

/** Damage 是一个"临时"属性，DamageExecution 用它算出最终伤害，然后转成 -Health */
UPROPERTY(BlueprintReadOnly, Category = "Damage")
FGameplayAttributeData Damage;
ATTRIBUTE_ACCESSORS(URPGAttributeSet, Damage)
```

完整的属性清单：`Health`、`MaxHealth`、`Mana`、`MaxMana`、`AttackPower`、`DefensePower`、`MoveSpeed`，外加一个特殊的 `Damage`。

有几个点容易踩坑，提前说：

属性的类型不是 `float`，而是 `FGameplayAttributeData`。这个结构内部其实有两个值，`BaseValue`（基础值，被 Instant 类型的 GE 永久改动）和 `CurrentValue`（当前值，被 Duration/Infinite 类型的 GE 临时叠加修正）。你平时读到的是 `CurrentValue`。这个设计是为了支持"buff 期间临时加攻击力，buff 结束后自动还原"，不用你手动记录原值。

`MaxHealth` 是个独立属性，不是写死的常量。注释解释了原因：GE 可能会去改最大血量（比如某件装备加最大生命）。配套地，`PreAttributeChange` 里有逻辑，当最大值变化时按比例缩放当前值，让血条百分比保持不变（`AdjustAttributeForMaxChange`）。

那一行 `ATTRIBUTE_ACCESSORS` 宏会自动展开出 `GetHealth()` / `SetHealth()` / `InitHealth()` 和一个 `GetHealthAttribute()` 静态函数。后面伤害管线里大量用 `Data.EvaluatedData.Attribute == GetHealthAttribute()` 来判断"这次改的是哪个属性"，就是它生成的。

最特别的是 `Damage`。它没有 `ReplicatedUsing`，也就是不复制，因为它根本不是一个要长期保存的状态。它是一个"中转站"：执行计算往里写一个最终伤害值，属性集随即把它读出来、清零，转成对 `Health` 的扣减。下面就讲这个过程。

### 一刀砍下去，到底发生了什么

这是本阶段最该烂熟于心的一条链。我顺着代码走一遍。

第一步，攻击的能力施加一个 GameplayEffect，这个 GE 的执行体（Execution）是 `URPGDamageExecution`，它继承自 `UGameplayEffectExecutionCalculation`（`Source/ActionRPG/Public/Abilities/RPGDamageExecution.h`）。普通 GE 只能做简单的加减乘，而 Execution 允许你写任意 C++ 计算逻辑，伤害公式这种需要同时读攻方和守方属性的，就得用它。

它先声明要"捕获"哪些属性（`Source/ActionRPG/Private/Abilities/RPGDamageExecution.cpp`）：

```cpp
struct RPGDamageStatics
{
    DECLARE_ATTRIBUTE_CAPTUREDEF(DefensePower);
    DECLARE_ATTRIBUTE_CAPTUREDEF(AttackPower);
    DECLARE_ATTRIBUTE_CAPTUREDEF(Damage);

    RPGDamageStatics()
    {
        // 捕获目标的 DefensePower，不做快照，要用施加这一刻的值
        DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, DefensePower, Target, false);
        // 捕获来源的 AttackPower，做快照（比如抛射物，要用发射那一刻的攻击力）
        DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, AttackPower, Source, true);
        // 捕获来源传进来的原始 Damage
        DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, Damage, Source, true);
    }
};
```

注意 `Source` 和 `Target` 的区分，以及那个 `bSnapshot` 布尔。守方的防御力不快照（要用挨打这一刻的防御），攻方的攻击力快照（用出手那一刻的攻击）。这种细节正是 GAS 强大也烦人的地方。

第二步，真正算伤害：

```cpp
// Damage Done = Damage * AttackPower / DefensePower
// 若 DefensePower 为 0，按 1.0 处理
float DamageDone = Damage * AttackPower / DefensePower;
if (DamageDone > 0.f)
{
    // 把算出来的数写回 Damage 这个临时属性
    OutExecutionOutput.AddOutputModifier(
        FGameplayModifierEvaluatedData(DamageStatics().DamageProperty, EGameplayModOp::Additive, DamageDone));
}
```

公式很朴素：原始伤害乘攻击力除防御力。算完它不直接扣血，而是把结果"加"到目标的 `Damage` 临时属性上。

第三步，重头戏。GE 一旦改动了任何属性，GAS 会回调属性集的 `PostGameplayEffectExecute`。本项目在这里检查"刚才改的是不是 `Damage`"，如果是，就把它转成扣血（`Source/ActionRPG/Private/Abilities/RPGAttributeSet.cpp`）：

```cpp
if (Data.EvaluatedData.Attribute == GetDamageAttribute())
{
    // ...省略：从 Context 里解析出 SourceCharacter、HitResult...

    // 取出本次伤害数额，并立刻把临时属性 Damage 清零
    const float LocalDamageDone = GetDamage();
    SetDamage(0.f);

    if (LocalDamageDone > 0)
    {
        // 真正扣血，并 clamp 到 [0, MaxHealth]
        const float OldHealth = GetHealth();
        SetHealth(FMath::Clamp(OldHealth - LocalDamageDone, 0.0f, GetMaxHealth()));

        if (TargetCharacter)
        {
            // 回调角色，这才是真正的伤害事件
            TargetCharacter->HandleDamage(LocalDamageDone, HitResult, SourceTags, SourceCharacter, SourceActor);
            // 所有血量变化都通知一次
            TargetCharacter->HandleHealthChanged(-LocalDamageDone, SourceTags);
        }
    }
}
```

读到这你应该能体会到 `Damage` 临时属性的妙处了。它把"算出一个数"和"这个数怎么影响血量"解耦开。执行计算只管算，属性集统一负责"把伤害落到 Health 上、做 clamp、防止死亡后还掉血"这些规则。换个游戏想做"伤害先扣护盾再扣血"，只改属性集这一处就行，执行计算不用动。

第四步，回到角色。`HandleDamage` 是 `ARPGCharacterBase` 的成员，它再去触发蓝图事件：

```cpp
// RPGCharacterBase.h 里的声明，C++ 声明、蓝图实现
UFUNCTION(BlueprintImplementableEvent)
void OnDamaged(float DamageAmount, const FHitResult& HitInfo, const FGameplayTagContainer& DamageTags,
               ARPGCharacterBase* InstigatorCharacter, AActor* DamageCauser);
```

`HandleDamage`（C++ 实现）内部会调用 `OnDamaged`（蓝图实现），蓝图里就能接上"播放受击特效、弹伤害数字、血量归零时播死亡动画"这些表现层逻辑。这就是 ActionRPG 一贯的分工：C++ 把数据算准、把时机卡好，表现交给蓝图。

这里还有个很 UE 的细节值得停一下。`URPGAttributeSet` 怎么能调用角色的 `HandleDamage`？因为角色头文件最后一行把它认作了朋友：

```cpp
// RPGCharacterBase.h 末尾
// 友元，让属性集能访问上面那些 Handle 函数
friend URPGAttributeSet;
```

属性集和角色是强耦合的一对，用 `friend` 让属性集直接回调角色，省掉了一层接口。这在 UE 代码里其实不算常见手法，但在 GAS 这种紧密协作的场景里很实用。

**把整条链背下来**（验收会考）：攻击 → 能力施加 GE（执行体是 `URPGDamageExecution`）→ 用原始 Damage、攻方 AttackPower、守方 DefensePower 算出最终伤害写回 `Damage` 临时属性 → `PostGameplayEffectExecute` 把 `Damage` 清零并转成 `-Health` → 回调 `HandleDamage` → 蓝图 `OnDamaged` / `OnKilled`。

---

## 6.3 能力与效果容器：让设计师"成套"施加效果

光有伤害管线还不够。一次攻击命中，往往不只扣血，可能还要附带"中毒""击退""减速"等一串效果。要是每个能力都手写一遍"创建 spec、设置目标、逐个施加"，蓝图会乱成一团。

ActionRPG 的解法是给能力加一个"效果容器"。看 `URPGGameplayAbility`（`Source/ActionRPG/Public/Abilities/RPGGameplayAbility.h`）：

```cpp
UCLASS()
class ACTIONRPG_API URPGGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    /** 标签 → 效果容器 的映射 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = GameplayEffects)
    TMap<FGameplayTag, FRPGGameplayEffectContainer> EffectContainerMap;

    /** 从 EffectContainerMap 里按标签查出容器，做成一个可施加的 spec */
    UFUNCTION(BlueprintCallable, Category = Ability, meta = (AutoCreateRefTerm = "EventData"))
    virtual FRPGGameplayEffectContainerSpec MakeEffectContainerSpec(
        FGameplayTag ContainerTag, const FGameplayEventData& EventData, int32 OverrideGameplayLevel = -1);

    /** 施加一个之前做好的 spec */
    UFUNCTION(BlueprintCallable, Category = Ability)
    virtual TArray<FActiveGameplayEffectHandle> ApplyEffectContainerSpec(const FRPGGameplayEffectContainerSpec& ContainerSpec);
};
```

`EffectContainerMap` 的 key 是一个 `FGameplayTag`，value 是 `FRPGGameplayEffectContainer`。设计师在蓝图/数据资产里静态填好这张表，比如配一条 `EffectContainer.Default → { 目标类型: 命中的敌人, 效果列表: [伤害GE] }`。

容器本身的结构很简单（`Source/ActionRPG/Public/Abilities/RPGAbilityTypes.h`）：

```cpp
USTRUCT(BlueprintType)
struct FRPGGameplayEffectContainer
{
    GENERATED_BODY()
public:
    /** 怎么选目标 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = GameplayEffectContainer)
    TSubclassOf<URPGTargetType> TargetType;

    /** 要施加给目标的效果列表 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = GameplayEffectContainer)
    TArray<TSubclassOf<UGameplayEffect>> TargetGameplayEffectClasses;
};
```

关键设计是把"构建"和"施加"拆成两步，对应两个类型：`FRPGGameplayEffectContainer` 是设计师填的静态模板，`FRPGGameplayEffectContainerSpec` 是运行时算好目标、算好等级的"已处理"版本。注释原话："这些容器在蓝图或资产里静态定义，运行时再变成 Spec"。

为什么要拆？因为施加时机往往和构建时机不一样。想象一次连招：能力激活的瞬间你就能把容器做成 spec（`MakeEffectContainerSpec`），但要等动画播到挥砍那一帧、确认命中了谁，才真正 `ApplyEffectContainerSpec`。中间还能往 spec 里追加命中目标：

```cpp
// FRPGGameplayEffectContainerSpec 提供的方法
void AddTargets(const TArray<FHitResult>& HitResults, const TArray<AActor*>& TargetActors);
```

所以流程是：拿标签去 `MakeEffectContainerSpec` 查表并构建 spec（此时可能还没目标）→ 命中时 `AddTargets` 把打到的人塞进去 → `ApplyEffectContainerSpec` 一次性把容器里所有 GE 施加给所有目标。设计师只需在表里配标签和效果列表，不用碰这套时序代码。

---

## 6.4 物品槽授予能力：把 GAS 和背包焊在一起

阶段五讲过背包的槽位（slot）。现在把它和 GAS 接上：装备进某个槽的物品，能给角色授予一个对应的能力。比如把"火焰剑"装进武器槽，角色就获得"火焰斩"这个能力。

入口在物品上（`Source/ActionRPG/Public/Items/RPGItem.h`）：

```cpp
/** 这个物品被装入槽位时要授予的能力 */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Abilities)
TSubclassOf<URPGGameplayAbility> GrantedAbility;
```

角色这边维护一张"槽位 → 已授予能力句柄"的表（`RPGCharacterBase.h`）：

```cpp
/** 槽位到该槽授予能力的映射 */
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Inventory)
TMap<FRPGItemSlot, FGameplayAbilitySpecHandle> SlottedAbilities;
```

`FGameplayAbilitySpecHandle` 是 ASC 给每个已授予能力发的"凭证"。授予能力用 `ASC->GiveAbility(...)` 返回这个 handle，之后想撤销就拿 handle 去 `ClearAbility(handle)`。

核心同步逻辑在 `FillSlottedAbilitySpecs`（`Source/ActionRPG/Private/RPGCharacterBase.cpp`）。它先铺一层默认能力，再用背包里的实际装备覆盖：

```cpp
void ARPGCharacterBase::FillSlottedAbilitySpecs(TMap<FRPGItemSlot, FGameplayAbilitySpec>& SlottedAbilitySpecs)
{
    // 先放默认能力（背包加载前就有效）
    for (const TPair<FRPGItemSlot, TSubclassOf<URPGGameplayAbility>>& DefaultPair : DefaultSlottedAbilities)
    {
        if (DefaultPair.Value.Get())
        {
            SlottedAbilitySpecs.Add(DefaultPair.Key,
                FGameplayAbilitySpec(DefaultPair.Value, GetCharacterLevel(), INDEX_NONE, this));
        }
    }

    // 再用背包里实际装备的物品覆盖
    if (InventorySource)
    {
        const TMap<FRPGItemSlot, URPGItem*>& SlottedItemMap = InventorySource->GetSlottedItemMap();
        for (const TPair<FRPGItemSlot, URPGItem*>& ItemPair : SlottedItemMap)
        {
            URPGItem* SlottedItem = ItemPair.Value;
            int32 AbilityLevel = GetCharacterLevel();
            // 武器用物品自带的等级
            if (SlottedItem && SlottedItem->ItemType.GetName() == FName(TEXT("Weapon")))
            {
                AbilityLevel = SlottedItem->AbilityLevel;
            }
            if (SlottedItem && SlottedItem->GrantedAbility)
            {
                // 覆盖默认项
                SlottedAbilitySpecs.Add(ItemPair.Key,
                    FGameplayAbilitySpec(SlottedItem->GrantedAbility, AbilityLevel, INDEX_NONE, SlottedItem));
            }
        }
    }
}
```

`DefaultSlottedAbilities` 解决了一个时序问题：存档/背包是异步加载的，加载完成前角色不能是个空壳。所以先用一套默认能力垫着，背包到位后再覆盖。

那这套同步什么时候触发？看 `PossessedBy` 里的订阅（同文件）：

```cpp
if (InventorySource)
{
    // 槽位变化时调 OnItemSlotChanged
    InventoryUpdateHandle = InventorySource->GetSlottedItemChangedDelegate()
        .AddUObject(this, &ARPGCharacterBase::OnItemSlotChanged);
    // 背包加载完成时刷新
    InventoryLoadedHandle = InventorySource->GetInventoryLoadedDelegate()
        .AddUObject(this, &ARPGCharacterBase::RefreshSlottedGameplayAbilities);
}
```

玩家换装备时背包广播 `OnItemSlotChanged`，角色收到后走 `RefreshSlottedGameplayAbilities`，内部先 `RemoveSlottedGameplayAbilities(false)` 再 `AddSlottedGameplayAbilities()`。

这里的"增量同步"写得挺讲究，值得看一眼 `RemoveSlottedGameplayAbilities`。当 `bRemoveAll` 为 false 时，它不会粗暴清空再重建，而是先算出"现在应该有哪些能力"，逐个对比已授予的，只有"目标里没有、或者能力类变了、或者来源物品变了"的才真正 `ClearAbility`：

```cpp
if (!DesiredSpec || DesiredSpec->Ability != FoundSpec->Ability
    || DesiredSpec->SourceObject != FoundSpec->SourceObject)
{
    bShouldRemove = true;
}
```

为什么不无脑重建？因为重建会打断正在运行的能力。如果你只是换了头盔，没动武器，武器那个能力句柄就该原封不动留着，正在挥砍的连招不能因为换了顶帽子就被打断。

激活入口在角色的 `ActivateAbilitiesWithItemSlot`（`RPGCharacterBase.h`），蓝图里玩家按下技能键，就调它去激活对应槽位的能力。配套还有 `GetCooldownRemainingForTag` 用来给 UI 显示冷却。

---

## 6.5 自定义能力任务与标签

### AbilityTask：让能力"等"得起

能力激活往往不是一瞬间的事。挥一刀要播放蒙太奇动画，得等动画播到命中帧才结算伤害，还得能被连招中途打断。这种"跨多帧、要等待"的异步逻辑，GAS 用 AbilityTask 来处理。

ActionRPG 自己写了一个组合任务 `URPGAbilityTask_PlayMontageAndWaitForEvent`（`Source/ActionRPG/Public/Abilities/RPGAbilityTask_PlayMontageAndWaitForEvent.h`）。它的注释说得很直白："这个任务把 PlayMontageAndWait 和 WaitForEvent 合并成一个，这样你能等待多种激活，比如近战连招"。换句话说，它一边播放动画蒙太奇，一边盯着 gameplay 事件，谁先来响应谁。

它对外暴露一组多播委托，蓝图把分支接到不同出口：

```cpp
/** 蒙太奇完整播放结束 */
UPROPERTY(BlueprintAssignable)
FRPGPlayMontageAndWaitForEventDelegate OnCompleted;

/** 蒙太奇被打断 */
UPROPERTY(BlueprintAssignable)
FRPGPlayMontageAndWaitForEventDelegate OnInterrupted;

/** 收到了一个触发用的 gameplay 事件（比如动画通知里发出的命中事件） */
UPROPERTY(BlueprintAssignable)
FRPGPlayMontageAndWaitForEventDelegate EventReceived;
```

创建任务的静态工厂函数：

```cpp
UFUNCTION(BlueprintCallable, Category="Ability|Tasks",
    meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
static URPGAbilityTask_PlayMontageAndWaitForEvent* PlayMontageAndWaitForEvent(
    UGameplayAbility* OwningAbility,
    FName TaskInstanceName,
    UAnimMontage* MontageToPlay,
    FGameplayTagContainer EventTags,  // 匹配这些标签的事件会触发 EventReceived
    float Rate = 1.f,
    FName StartSection = NAME_None,
    bool bStopWhenAbilityEnds = true,
    float AnimRootMotionTranslationScale = 1.f);
```

注意那个 `EventTags` 参数。能力播动画时，动画里的 AnimNotify 可以发出一个带标签的 gameplay 事件，比如 `Event.Montage.Shared.WeaponHit`。这个任务只在事件标签匹配时才走 `EventReceived` 出口，于是"动画播到挥砍帧 → 发事件 → 能力结算伤害"就串起来了。`BlueprintInternalUseOnly` 标记意味着这是给蓝图节点用的内部工厂，你不会直接手写它，而是在蓝图里拖出对应节点。

它的注释最后一句很贴心："这是创建游戏专属任务时一个很好的参考例子"。想自己写 AbilityTask，照着它抄准没错。

### GameplayTag：GAS 的神经系统

标签在 `Config/DefaultGameplayTags.ini` 里集中定义。本项目用到的有：

```
Ability.Item / Ability.Melee / Ability.Melee.Close / Ability.Melee.Far / Ability.Ranged / Ability.Skill
Cooldown.Skill
EffectContainer.Default
Event.Montage.Player.Combo.BurstPound / ChestKick / FrontalAttack / GroundPound / JumpSlam
Event.Montage.Shared.UseItem / UseSkill / WeaponHit
Status.DamageImmune
```

看名字就能读出几类用途。`Ability.*` 给能力归类，`ActivateAbilitiesWithTags` 能按标签批量激活。`Cooldown.Skill` 标记冷却状态，`GetCooldownRemainingForTag` 查的就是它。`EffectContainer.Default` 正是 6.3 那张 `EffectContainerMap` 的 key。`Event.Montage.*` 是动画事件标签，喂给 6.5 那个任务的 `EventTags`。`Status.DamageImmune` 是无敌状态，配合能力的"激活拦截"用。

标签在 GAS 里的两个核心作用记一下。一是**分类与匹配**，层级化设计让 `Ability.Melee` 能匹配到 `Ability.Melee.Close`，做模糊查询很方便。二是**激活与拦截**，能力身上可以配 `ActivationRequiredTags`（必须拥有这些标签才能激活）和 `ActivationBlockedTags`（拥有这些就禁止激活）。比如给"无敌"配 `Status.DamageImmune`，伤害 GE 配上"目标有此标签则不施加"，一刀就被挡掉了，全程不用写一个 `if`。这种用标签做条件控制的思路，是 GAS 区别于传统硬编码的地方，习惯了会很上瘾。

---

## 动手练习：新增一个暴击率属性 CritChance

光看不练等于白看。这个练习会逼你把属性集、执行计算、伤害管线、UI 四块串一遍。目标：加一个暴击率属性，让它参与伤害计算并显示在 UI 上。

**第一步，在属性集里加属性。** 编辑 `RPGAttributeSet.h`，仿照现有属性加一个：

```cpp
/** 暴击率，0~1。命中时按此概率打出双倍伤害 */
UPROPERTY(BlueprintReadOnly, Category = "Damage", ReplicatedUsing = OnRep_CritChance)
FGameplayAttributeData CritChance;
ATTRIBUTE_ACCESSORS(URPGAttributeSet, CritChance)
```

别忘了在 protected 区加 `OnRep_CritChance` 声明。然后到 `RPGAttributeSet.cpp`：构造函数初始化列表里加 `, CritChance(0.0f)`；`GetLifetimeReplicatedProps` 里加 `DOREPLIFETIME(URPGAttributeSet, CritChance);`；再实现 `OnRep_CritChance`，照抄一行 `GAMEPLAYATTRIBUTE_REPNOTIFY(URPGAttributeSet, CritChance, OldValue);`。少一处复制就会出网络同步的怪问题，仔细点。

**第二步，让执行计算用上它。** 编辑 `RPGDamageExecution.cpp` 的 `RPGDamageStatics`：

```cpp
DECLARE_ATTRIBUTE_CAPTUREDEF(CritChance);
// 构造里加（来源属性，要快照）
DEFINE_ATTRIBUTE_CAPTUREDEF(URPGAttributeSet, CritChance, Source, true);
```

构造函数里加 `RelevantAttributesToCapture.Add(DamageStatics().CritChanceDef);`。然后在 `Execute_Implementation` 算完 `DamageDone` 后插一段暴击判定：

```cpp
float CritChance = 0.f;
ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
    DamageStatics().CritChanceDef, EvaluationParameters, CritChance);

// 掷一个随机数，命中暴击则伤害翻倍
if (CritChance > 0.f && FMath::FRand() < CritChance)
{
    DamageDone *= 2.0f;
}
```

伤害还是照旧通过 `Damage` 临时属性流回 `PostGameplayEffectExecute`，扣血那一段一行都不用改。这正是 6.2 解耦设计的好处，自己体会一下。

**第三步，给个初始值。** 暴击率默认是 0，得有地方把它顶上去。最简单的办法是做一个 Instant 的初始化 GE（或者直接用现有的初始化属性的 GE），把 `CritChance` 设成比如 0.25。设计师在数据资产里配，或者你在某个被动 GE 里加一条 modifier。

**第四步，UI 显示。** 编译后，`CritChance` 因为带了 `BlueprintReadOnly`，在蓝图里能直接读到。可以仿照角色已有的 `GetHealth()` 这类函数，在 `ARPGCharacterBase` 加一个 `GetCritChance()` 的 `UFUNCTION(BlueprintCallable)`，内部 `return AttributeSet->GetCritChance();`，再在 UMG 的属性面板 Widget 里绑一个文本显示它。

练完你会发现，加一个新属性要动的地方不少（属性集声明、复制、执行计算、初始化、UI），但每一处职责都很清晰。这种"改五处但都很明确"的体验，恰恰是 GAS 工程化的特征。

**踩坑提醒**：改了 `.h` 里的 `UPROPERTY` 或加了 `UFUNCTION`，必须重新编译 C++，光在编辑器里热重载经常出幺蛾子，建议关掉编辑器用 `Build.bat` 编译干净再开。属性不复制（漏 `DOREPLIFETIME`）的话，单机测试看不出问题，一联机就血量对不上，排查起来很痛苦。

---

## 验收清单

对照下面几条自检，能全部讲清楚才算过了这一关。

**口述完整伤害管线。** 不看代码，从"玩家点击攻击"一路讲到"血条下降、播放受击动画"，每一棒交接给谁：能力 → GE → `URPGDamageExecution` 算伤害写回 `Damage` → `PostGameplayEffectExecute` 转成 `-Health` → `HandleDamage` → 蓝图 `OnDamaged`。

**解释 GameplayEffect 的三种 Duration。** Instant（瞬时，永久改 `BaseValue`，比如一次扣血），Duration（限时，临时改 `CurrentValue`，到期自动还原，比如 10 秒加攻 buff），Infinite（无限，一直生效直到手动移除，比如装备提供的被动加成）。能说出"为什么伤害用 Instant、buff 用 Duration、装备被动用 Infinite"。

**解释 GameplayTag 在激活/拦截中的作用。** 能力靠 `ActivationRequiredTags` / `ActivationBlockedTags` 决定能不能放；`Status.DamageImmune` 这类状态标签如何让一次伤害被拦下；标签的层级匹配（`Ability.Melee` 命中 `Ability.Melee.Close`）有什么用。

**能解释这几个解耦设计的理由。** 为什么要有 `Damage` 临时属性而不直接扣 `Health`；为什么效果容器要拆成 Container 和 Spec 两层；为什么换装备时用增量同步而不是清空重建。

最后再啰嗦一句。GAS 这套东西，第一遍读完云里雾里是常态，别怀疑自己。它的回报在后面：真正动手做毕业项目（阶段八的"新增一件武器"）时，你会发现伤害、buff、冷却、连招这些需求，GAS 都给你铺好了路，那时候才会回过头来觉得这套设计是真的香。慢慢来。
