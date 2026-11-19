# V2.0.1 冒险关卡与调试

从 **START → ADVENTURE** 直接选择世界即可开玩。从 **LEVELS** 可单独进入任意关卡。

| 世界 | 关卡顺序 | Boss 关 |
|---|---|---|
| 1 草原冒险（MEADOW） | First Roll · Switchback · Meadow Run · Amber Lock | **Meadow Boss** |
| 2 海洋探险（OCEAN） | Ice Drift · Warp Line · Blue Pass · Glacier Gate | **Frost Boss** |
| 3 火焰峡谷（FIRE CANYON） | Red Patrol · Spin Room · Ember Timer · Cinder Bridge | **Forge Boss** |
| 4 雷电乐园（THUNDER PARK） | Moon Walk · Night Lantern · Zero Tide · Mirror Stars | **Nova Boss** |
| 5 神秘星空（STARFIELD） | Orbit Fall · Cross Current · No U Turn · Mirror Core | **Final Boss** |

## 添加或修改关卡

编辑 `src/levels.c` 的 `maze_levels` 常量表。冒险关卡顺序由 `src/adventure.c` 的 `campaign` 指定，存档按关卡 ID 保存，保留既有 ID。坐标单位为像素，右、下为正，可玩圆心为 (120,128)，半径为 101，小球半径为 6。各关设置起终点、静态墙、圆弧、危险点、机关、金币、道具、目标秒数 `target_seconds` 和全收集奖励 `all_bonus`。可用 `expert_seconds` 和 `coin_target` 分别覆盖三星时间和星级金币门槛。超过目标时间仍可通关。

- `WALLS`：线段端点；含圆弧拆分后每关最多 48 段。静态陷阱最多 4 个。
- `M01` 的 a/b 是往返端点，周期字段单位 ms；`M07` 的 a 是旋转中心，radius 是半臂长。两个运动机关的周期要大于 0。建议至少给球的直径、墙厚和操作余量留出 22–28 px 的净空间。
- `M02` 的 a/b 是门线段；允许穿过的方向为 `(-dy, dx)`。传送门的 a/b 是成对入口和出口。区域机关以 a 为圆心、radius 为半径；传送带的 b 是推力方向单位向量。
- 同色钥匙和 `GATE` 使用相同的 id（0 琥珀、1 紫色；主机寻路测试当前覆盖这两色）。瞬移 `ITEM_BLINK` 只有一条预设安全路径，不作为解锁必经门的捷径。
- `TIMED` 的 a/b 是门线段，周期单位为 ms。周期前 55% 关闭，橙灯表示关闭，绿灯表示开放。小球已占据门缝时会延迟闭合。
- `COINS` 指定最多 12 个候选点，`random_count` 为 0 时全摆放，否则每局从候选点取指定数量。道具最多 16 个、机关最多 32 个、门色 ID 必须小于 16，各限额对应固定状态掩码。不要把金币摆在墙或必经危险区内。

## 独立开关与计分

在 `platformio.ini` 的 `build_flags` 设置 `-DMAZE_DISABLED_MECHANISMS=64` 可禁用 M07。M01–M10 依次对应位 0–9，限时门对应位 10（值 1024），禁用多个机关时将这些值相加。禁用传送门会影响 Warp Line 等依赖传送门的关卡。也可覆盖 `MAZE_COIN_POINTS`、`MAZE_COMBO_MS`、`MAZE_COMBO_FIRST/SECOND`、`MAZE_COMBO_FIRST_PERCENT/SECOND_PERCENT` 和 `MAZE_CLEAN_BONUS`。

主机寻路检查覆盖每关的终点和各金币点，考虑钥匙、单向门和传送，将移动障碍扫过的范围视为禁区，并在开放阶段验证限时门所在路段。需要在实机上验证倾斜操作和目标时间的难度。
