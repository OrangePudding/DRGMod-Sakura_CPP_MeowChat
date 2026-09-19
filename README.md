# Sakura_CPP_MeowChat — DRG 聊天加喵 Mod（UE4SS）

给聊天消息正文和发送者名称按规则追加「喵」。需安装 UE4SS；**主机装 mod 即可，所有队友（无需装 mod）也能看到带喵的文本**。

## 功能
- 玩家聊天正文、系统消息（如“X 挂了！”）、发送者名称都按规则加喵。
- 幂等：结尾已是「喵」的消息不重复加。
- 异常受保护，不会导致游戏崩溃。

## 安装
1. 把 `dlls/main.dll` 放到游戏目录：
   `FSD\Binaries\Win64\ue4ss\Mods\Sakura_CPP_MeowChat\dlls\main.dll`
2. 确认 `ue4ss\Mods\mods.txt` 中有：
   `Sakura_CPP_MeowChat : 1`
3. 启动游戏，进入任意任务后生效。

## 配置
mod 目录下可选 `config.txt`（UTF-8），没有则用默认值，保存后约 1 秒内生效。

```ini
# 总开关
Enabled = true

# 追加到消息/发送者名末尾的文字（留空 = 不追加）
Suffix = 喵

# 是否也给发送者名称追加
SenderEnabled = false
```

## 加喵规则
- 去掉句尾空白后，从后往前跳过标点（`！？。，～!?.,~…、`）和空格，在正文后插入「喵」，标点留在句尾。
- 例：`hello` → `hello喵`；`hello!` → `hello喵!`；`你好！！` → `你好喵！！`。
- 正文结尾已是「喵」不重复加；纯占位符模板（如 `{0}`）不加。
- 发送者名称同样按此规则加喵。
