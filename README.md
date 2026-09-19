# Sakura_CPP_MeowChat — DRG 聊天加喵 Mod（C++ / UE4SS）

给聊天消息正文和发送者名称按规则追加「喵」。基于 UE4SS C++ API 实现，**主机装 mod，客机（未装 mod）也能看到所有带喵的文本**。

## 功能
- 玩家聊天正文、系统消息（如“X 挂了！”）、发送者名称都按规则加喵。
- 主机在消息序列化进网络包之前就地改写，客机无需装 mod。
- 幂等：正文结尾已是「喵」的消息不重复加。
- 全部拦截点 SEH 保护，异常只记日志、不崩游戏。

## 安装
1. 把 `dlls/main.dll` 放到游戏目录：
   `FSD\Binaries\Win64\ue4ss\Mods\Sakura_CPP_MeowChat\dlls\main.dll`
2. 确认 `ue4ss\Mods\mods.txt` 中有：
   `Sakura_CPP_MeowChat : 1`
3. 旧 Lua 版保持关闭：`Sakura_Lua_MeowChat : 0`
4. 启动游戏，进入任意任务后生效。

## 配置
mod 目录下可选 `config.txt`（UTF-8 编码，用记事本保存即可）。文件不存在时使用默认值；修改保存后**约 1 秒内生效**，无需重启游戏。

```
# 总开关：关闭后不再追加任何字符
Enabled = true

# 追加到消息/发送者名末尾的字符（留空 = 不追加）
Suffix = 喵

# 是否也给发送者名称追加
SenderEnabled = true
```

布尔值支持 `true/false`、`1/0`、`yes/no`、`on/off`，键名不区分大小写，`#` 或 `;` 开头为注释。

## 加喵规则
- 去掉句尾空白后，从后往前跳过标点（`！？。，～!?.,~…、`）和空格，在正文后插入「喵」，标点留在句尾。
- 例：`hello` → `hello喵`；`hello!` → `hello喵!`；`你好！！` → `你好喵！！`。
- 正文结尾已是「喵」不重复加；纯占位符模板（如 `{0}`）不加。
- 同一规则同时作用于发送者名称（`Sender` 字段）。

## 工作原理
- `Server_NewMessage`（主机侧，Server RPC）：玩家聊天正文在主机收到后、进广播前加喵。
- **ProcessEvent pre-callback 广播前改写（主机真正生效）**：`ClientNewMessage` / `Client_NewLocalizedMessage` 是
  `FSDGameState` 上的原生 NetMulticast RPC。主机执行 `UObject::ProcessEvent` 时，引擎**先调用
  `CallRemoteFunction` 把负载序列化发给所有客机、再执行函数体**，所以普通 UFunction pre-hook（挂在函数体执行上）
  只能改到主机本地显示、改不到发给客机的负载。本 mod 注册 UE4SS 的 ProcessEvent pre-callback（位于
  ProcessEvent 开头、序列化之前），匹配到这两个 RPC 时直接改写 Parms 里的 Sender 与 Msg 模板再放行——
  所有客机（装不装 mod）都收到带喵的负载。
- `ClientNewMessage` / `Client_NewLocalizedMessage`（接收侧 pre-hook）：加入没装本 mod 的主机房间时，
  本机收到未加喵的负载，接收侧 hook 本地补喵（幂等），保证自己也能看到。
- `PostGameMessage` / `PostLocalizedGameMessage`（本机兜底）：覆盖不经过 multicast 的本地消息
  （实测系统消息走 `Client_NewLocalizedMessage`，这两个 hook 在测试中未触发，仅作兜底）。

本地化消息只改 `Msg` 模板与 `Sender`，不改 `Arguments`，`FText::Format` 语义保持不变。

## 健壮性
- 所有 hook 体包在 SEH（`__try/__except`）里，访问违例/C++ 异常一律吞掉并写日志。
- 所有 UE 反射（hook 注册、GameState 定位）与配置热重载都在**游戏线程**的
  ProcessEvent 回调里执行（与 WelcomeMod 的冻结修复同一机制），不在 UE4SS 后台线程
  做任何 UE 操作；配置读写也回到同一线程，消除了原来后台线程读写 `g_cfg_*` 的数据竞争。
- 聊天改写同样是 ProcessEvent pre-callback（与配置/反射共享同一游戏线程），每个
  ProcessEvent 只做两次函数指针比较，命中才做字符串改写，性能开销可忽略。
- 字符串写入走 UE 分配器（`FMemory::Malloc`）；FText 写入用 UE4SS 导出的 `FText::SetString`（引用计数正确）。
- 替换 FString 时故意**不释放旧缓冲区**（避免与其他 mod 手工构造的 FString 造成堆不匹配），代价是每条被改写的消息泄漏几百字节。
- 卸载/热重载（Ctrl+R）会注销全部 UFunction hook；ProcessEvent 回调由 UE4SS 全局持有，
  建议不要对该 mod 做 Ctrl+R 热重载（与 WelcomeMod 同限制）。

## 日志
日志写在 mod 目录 `meowchat.log`（每次启动清空）。预期输出：
```
hook: all hooks ready
meow[ServerNewMessage]: 测试 -> 测试喵
meow[SendSender]: ooyole_AR -> ooyole_AR喵
meow[SendMsg]: 测试 -> 测试喵
meow[SendLocMsg]: {0} 挂了！ -> {0} 挂了喵！
```

## 重新编译
源码在工程目录 `MeowChatMod`（依赖本机 UE4SS v3.0.1 SDK `RE-UE4SS` 与 VS2019），运行：

```
build.bat
```

产物 `main.dll` 复制到 `dlls\` 即可。
