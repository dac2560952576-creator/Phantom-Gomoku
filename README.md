# Phantom Gomoku（幻格五子棋）

基于 C++17 + SFML 3 的五子棋游戏，融合塔罗牌主题的卡牌事件系统，支持人机/双人对战与局域网联机。

## 特色

- **22 张大阿尔卡纳卡牌事件** — 每张塔罗牌有独特的棋盘效果和动画演出，在对局中随机触发
- **三种 AI 难度** — Easy / Medium / Hard，Hard 模式含动态障碍物管理
- **经典 & 幻格双模式** — 经典 15×15 五子棋 + 带随机障碍物的幻格棋盘
- **局域网联机** — TCP 房间制对战，支持自定义规则
- **动态音乐** — 不同难度切换不同 BGM，含卡牌事件专属配乐
- **像素字体** — 使用融合像素字体（简体中文）

## 构建

### 依赖

- **CMake** ≥ 3.20
- **SFML** 3.0（System / Window / Graphics / Network / Audio）
- **GCC**（MinGW-w64）或 MSVC
- **Ninja**（推荐，MinGW Make 不支持中文路径）

### 编译

```bash
cmake --preset default
cmake --build --preset default
```

`CMakePresets.json` 中的 `SFML_DIR` 需根据本地 SFML 安装路径修改。

## 操作

| 操作 | 按键/方式 |
|------|-----------|
| 落子 | 鼠标左键 |
| 悔棋 | U / Backspace |
| 重新开始 | R |
| 切换棋盘模式 | O |
| 切换人机/双人 | P |
| 返回菜单 | M / Esc |
| 查看规则 | 主菜单-规则说明 |

## 项目结构

```
├── CMakeLists.txt          # 构建配置
├── CMakePresets.json       # CMake 预设
├── assets/                 # 图片、音频、字体素材
└── src/
    ├── main.cpp            # 入口
    ├── Game.hpp/cpp        # 核心游戏逻辑与渲染
    ├── Board.hpp/cpp       # 棋盘数据与绘制
    ├── NetworkServer.hpp/cpp  # TCP 服务器
    ├── NetworkClient.hpp/cpp  # TCP 客户端
    └── NetworkCommon.hpp   # 网络协议定义
```

## 塔罗牌效果一览

| 卡牌 | 效果 |
|------|------|
| Fool（愚者） | 随机放置一颗幸运之星棋子 |
| Magician（魔术师） | 镜像复制棋子到对称位置 |
| High Priestess（女祭司） | 十字圣所，清除十字形区域棋子 |
| Empress（女皇） | 藤蔓生长，延伸至相邻空位 |
| Emperor（皇帝） | 金色放逐障碍物 |
| Hierophant（教皇） | 边界封锁，N 回合禁止落子边界区 |
| Lovers（恋人） | 交换两颗棋子位置 |
| Chariot（战车） | 推挤棋子沿方向移动并落子 |
| Strength（力量） | 保护棋子不被移除 |
| Hermit（隐士） | 清除 N 颗棋子 |
| Wheel of Fortune（命运之轮） | 重洗障碍物位置 |
| Justice（正义） | 移除双方各一颗棋子 |
| Hanged Man（倒吊人） | 牺牲己方一颗，清除对方两颗 |
| Death（死神） | 十字扩散清除周围棋子 |
| Temperance（节制） | 和谐回合，双方可在对方棋子旁落子 |
| Devil（恶魔） | 清除单颗棋子 |
| Tower（高塔） | 清除全部障碍物后再生 |
| Star（星星） | 高亮最佳落子位置 |
| Moon（月亮） | 隐藏一颗棋子（迷雾） |
| Sun（太阳） | 清除 3×3 区域 |
| Judgement（审判） | 清除全部障碍物 |
| World（世界） | 曼荼罗封印动画 |

## 许可

课程设计项目，仅供学习参考。
