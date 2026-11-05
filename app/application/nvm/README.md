# NVM 对象框架（已接入应用构建）

本目录为应用使用的 NVM 实现，框架文件置于根目录，
具体数据块置于 `instance/`。旧 `nvm.c/.h` 已删除，历史由 Git 保留。
平台适配头 `nvm_port.h` 位于 `app/ports/`。
Flash AO 内嵌 NvmManager，在 Flash_ctor 中依次装配 manager、
NvmBoot 和 NvmBlinky；初始化恢复后调用 NvmBoot_inc。
Blinky 等 NVM_READY 才开始修改活块；保存由 Flash AO 串行执行。
不保留旧的恢复前增量缓存或 Nvm_writeResult 结果表。

| 对象 | 数据与职责 |
|---|---|
| NvmMeta | vData、sSize（双份后备对子），ctor 只做绑定；零值由
          后备的静态存储期保证（.bss 清零） |
| NvmManager | 可实例化对象：`vptr` + `void *fs` +
             `nvmRegister/idNum`（自带槽位表，真正的多实例）；
             fs 种类差异内化进 vtable |
| 具体实例 | 内嵌 NvmMeta 基类，持有持久化数据的 RAM 缓冲区 |

分层 `manager → fs → 具体 load/store`：**fs 种类的多态面是
`NvmManagerVtbl`**（init/load/store 三操作），`me->fs` 是
`void *` 后端句柄——这类后端除 fs 对象本身没有别的上下文，
所以不需要子类型包装，`void*` 就是全部上下文。verbs 解析槽位后
`me->vptr->op(me, id, vData, sSize)` 派发；后端 op 经**块内上半
区**暂存 IO（vData 后备是 `T data[2]` 一对），flash 不直接碰
活半区。配对由构造保证：`NvmMeta_ctor` 收整块后备 extent、
断言偶数并折半出 sSize，实例侧另有 `_Static_assert` 钉死
`data` 恰为 2 元。
本应用的后端与装配全部收在 `nvm_manager.c` 文件静态区：
`{fs_、三个 op、常量 vtbl、恢复失败 trace}`——后端选择本来就是
应用装配，不另立文件；op 内 `(struct nvs_fs *)me->fs` 取回
句柄。

**方向备忘：建议绑死 sm_nvs。**按 id 索引是本 API 自身的契约
（mount/load/store 的 `uint16_t id` 就是它的形状），vtbl 留在
原位或下沉 port 都只藏实现、藏不住这层语义；真上第二种 fs
家族时按同形状另起平行模块、各自持有自己的语义，比塞进同一
多态面诚实。拆法已明确：删 `vptr`/`NvmManagerVtbl` 与
`void *fs`，`struct nvs_fs` 改内嵌成员（归属链字面为真、
每个 manager 持有真实 nvs 对象），三个 op 折进 load/store
本体；公开动词签名不动，实例侧零感知。落地时机挂在迁
common_c 或接入时执行。

**归属链 `Flash AO ⊃ NvmManager ⊃ fs`**：对象由 AO 侧内嵌持有
（本模块不代持 manager 对象，Sm_Flash 内嵌持有）；
槽位表随对象走——`nvmRegister` 指针 + `idNum` 容量是对象字段
（当前 ctor 一律绑 `nvm_manager.c` 的文件静态默认表；第二个
manager 要独立槽位表仍待参数化。fs 侧换绑已兑现为动词——
见下）。
多实例的槽位表侧仍是**结构预设**，fs 侧已可换绑。
fs 绑定是公开动词 `NvmManager_mountFs(me, fs)`：fs 对象与
驱动它的 nvs ops 一步绑进 manager——vptr+fs 只有这一条
写入路径。ctor 用它挂本应用的文件静态 `fs_`（对象级绑定，
不是 init 的卷挂载）；第二个 manager 用同一动词挂自己的
fs 对象即完成换绑。窗口字段（offset/sector/device）归 fs
对象的创建者填——mountFs 不代填，否则换绑就被本板窗口
顶死；本应用 `fs_` 的窗口由 ctor 填。没有 port hook：
同一文件内自己声明、自己实现、自己调用的"接口"不构成分界，
只藏直线代码。异种 fs 家族走平行模块（方向备忘）。
Meta 不含 ID、默认值指针或注册信息；不再另设 NvmInstance。
对外只有 `NvmManager_*` 的 `me` 指针方法面——多实例是预设
形状，不设全局单例入口。

`NvmManager_ctor(me)` 只做平面绑定（后端 + 窗口 + 槽位表），
**不跑实例花名册**——manager 点具体类型就是反向依赖，花名册
归拥有者的装配序列：AO 侧 ctor 之后依次调
`NvmBoot_ctor(mgr)`、`NvmBlinky_ctor(mgr)`。实例 ctor 收目标
manager 参数（表随对象后无参就没法选表），在各自文件内经
`NvmManager_mountBlock(mgr, id, meta)` 挂载自己的静态单例——
槽位号不经 ctor 参数传入。依赖方向因此单向：
`instances → manager`、`AO → {manager, instances}`，manager.c
不引任何实例头文件。
实例头文件提供约定 ID（NVM_BOOT_ID = 1、NVM_BLINKY_ID = 2）
作为自身槽位。新增类型在 owner 装配点登记一次 ctor 调用
（实例花名册归 AO 侧）。
框架配置入口为 `nvm_conf.h`：NVM_REGISTER_MAX 决定注册表数组容量；
默认表 arity 由 `_Static_assert` 钉住不小于该配置（idNum 从
数组实体推导，物理表更大无害）。
Manager 不特殊排除槽位 0；本应用从 1 起编号是装配约定，不是框架规则。
`NvmManager_load/store(me, id)` 解析槽位取 meta，
经 `me->vptr` 派发到后端 op（块内上半区暂存读写）；
越界与未挂载槽位是调用方违例（REQUIRE 断言，不走返回值）；
挂载写入集中在 NvmManager_mountBlock 断言（含槽位占用）。

## 实例命名空间

每个具体类型在 `instance/` 下独占一套头文件和实现，
持有自己的数据类型、私有单实例、ctor（内含挂载）和约定 ID。
实例 API 不限于 get/set，按数据语义扩展自有操作：

| 文件 | 持久化数据 | 语义操作 |
|---|---|---|
| `instance/nvm_boot.h/.c` | count | inc（恢复后 +1）、get（活块指针） |
| `instance/nvm_blinky.h/.c` | edges | add、get（活块指针） |

`get` 的形态是直接返回活块指针（`&data[0]`）——活块就是全局
唯一快照，不拷贝、不走策略层；`NvmMeta` 因此只剩
vData/sSize 描述，update 策略层与 container_of 全部退役。
写动词（inc/add）直写活块 `data[0]`，语义统一为 restore
后操作——restore 前写入会被 load 提交覆盖，没有 early
暂存层（真实次序上边沿生产者晚于 Flash AO restore 启动）。
实例命名空间归 `instance/` 下的具体块所有；
全槽位恢复遍历内化在 `NvmManager_init` 里（应用预设只启动时
加载一次，不单独暴露 restore 动词；其内部仍是逐槽 load）；
fixups（inc）由调用方在 init 之后跑，不承载具体类型。
新增类型分配唯一且不越界的槽位 ID；
已有持久化 ID 不因文件拆分或注册顺序改变。

## 生命周期

1. `NvmManager_ctor(&ao->nvm)`：ctor 体内平面绑定槽位表、填
   板级窗口，并经 `mountFs` 挂上本应用的 nvs 实例（vptr+fs）；
   随后 AO 装配序列逐个调 `NvmBoot_ctor(&ao->nvm)`、
   `NvmBlinky_ctor(&ao->nvm)` 完成实例自挂载。
2. `NvmManager_init(&ao->nvm)`：一次 `vptr->init`（nvs 侧即
   `nvs_mount`）成功后内化全槽位恢复遍历（-ENOENT 保留构造
   默认、不记错误，首错记录并返回）；重试策略归调用者。
3. init 返回后由调用方跑实例 fixups（`NvmBoot_inc`）；
   init 失败时是否照跑归调用方策略（照跑则计数从 0 起算）。
4. `NvmManager_store(&ao->nvm, id)`：持久化单个槽位。

```c
// 以下全部在 nvm_manager.c 文件私有：
static NvmMeta *NvmManager_registry_[NVM_REGISTER_MAX];  // 本应用槽位表
static struct nvs_fs fs_;                                // 后端 fs
static NvmManagerVtbl const nvsBackendVtbl_;             // 后端 ops
```

## 一致性模型

块内双份暂存：每个数据块把 vData 后备声明为 **`T data[2]` 一对**，
`NvmMeta_ctor` 收整块 extent（`sizeof(data)`）、断言偶数并折半
出 `sSize`——活半区 `[0]`、暂存半区 `[1]` 的切分由构造保证；
实例文件再补一条 `_Static_assert` 钉死数组恰为 2 元。
store 先在 NVM_PORT 临界段内把活半区拷进暂存半区再 `nvs_write`，
load 先 `nvs_read` 进暂存半区再在临界段内提交进活半区。
flash 永不直碰活半区；暂存随块走，无共享缓冲、无容量上限常量、
无 malloc。
锁只持 RAM 侧：实例的语义写操作与后端 op 的暂存拷入/提交都在
NVM_PORT 临界段内（本项目映射 `taskENTER_CRITICAL`，覆盖点在
`nvm_port.h`）；get 只借出只读活块指针，不返回稳定快照。
调用方须在 NVM_PORT_ENTER/EXIT 临界段内读取其内容，
即使当前只有单字字段也遵守此约定；需要的值复制到局部变量，
退出临界段后再绘制、记录或执行 IO。仅获取或比较地址不读取内容，
不需要临界段；不能因指针为 const 就省略读侧保护。由此：

- IO 次序归持有执行体：Flash AO 单线程驱动全部 verbs，
  临界段不跨存储 IO。
- 模型无框架级 loaded/有效标志，实例侧也没有；"已恢复"
  语义只归调用次序（`NvmManager_init` 返回即恢复完成、写动词
  全部在其后）。

## 错误上报与断言

`nvm_port.h` 提供 NVM_REQUIRE/ASSERT（映射 sm_assert，显式 ID）
与 NVM_PORT_ENTER/EXIT。ctor、mountFs/mountBlock 与
load/store 的参数前置条件全部断言（未定义 ID 是违例而非
错误返回）。
返回值只剩后端 errno 原样透传（-ENOENT 缺记录、-EIO 短 IO
为常见值；nvs 自身的 -EINVAL/-EACCES/-ENOSPC 也原样上抛），
恢复逐项失败经记录 10（UINT16 id + INT32 error，
沿用旧模块分配）上报。

## 接入状态与未保留的旧能力

- 已接入固件源码清单及 Flash AO/Blinky 调用侧；
  旧 API 的迁移映射见下表。接入不等于固件编译或硬件验证。
- 逐项 store 结果查询（旧 Nvm_writeResult）未保留：
  save 同步返回结果，需要观测时由调用方记录。
- init（含内部 restore）后的 fixup 由 Flash AO 调 NvmBoot_inc。
  挂载完全失败时仍从默认值累计本次启动，后续保存失败由 AO 记录。

| 旧 API | 新对应 |
|---|---|
| Nvm_load(fs) | NvmManager_init（含全槽位恢复）+ fixups |
| Nvm_store(fs, id) | NvmManager_store(me, id) |
| Nvm_addBlinky / Nvm_blinky | NvmBlinky_add / NvmBlinky_get |
| Nvm_bootCount | NvmBoot_get |
| Nvm_writeResult | 取消（save 同步返回） |
