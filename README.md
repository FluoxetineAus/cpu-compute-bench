# CPU Compute Benchmark

一个只测 **CPU 计算吞吐** 的跑分程序。热循环全部驻留在寄存器中：不测内存带宽，不测磁盘、GPU、网络，也不在计时区间内分配内存。程序同时给出单核、多核和综合分，并输出每项原始吞吐，方便判断分数来源。

## 快速开始

```bash
cd cpu-bench
make
./cpu-bench
```

一次标准测试约 15 秒（6 项 × 单核/多核 × 3 次采样）。快速检查：

```bash
./cpu-bench --quick
```

保存 JSON：

```bash
./cpu-bench --json results/my-machine.json
```

### 桌面双击版（macOS）

桌面版 `CPU性能跑分 M优化版.app` 内置多套二进制：

| 目标机 | 使用的二进制 |
|---|---|
| Intel Mac，支持 AVX2 + FMA | `cpu-bench-x86_64-avx2` |
| Intel Mac，较老的 x86_64 | `cpu-bench-x86_64-generic` |
| Apple M1 系列 | `cpu-bench-arm64-m1` |
| Apple M2 系列 | `cpu-bench-arm64-m2` |
| Apple M3 系列 | `cpu-bench-arm64-m3` |
| Apple M4 系列 | `cpu-bench-arm64-m4` |
| 未知/更新的 Apple Silicon | `cpu-bench-arm64` 通用回退 |

启动时按 CPU 型号自动选择，换一台 Mac 通常直接双击就能用，**不需要安装编译器**。如果没有匹配的版本，会自动使用通用 arm64 版本；仍无法运行时才尝试用源码在本机编译（需要 Xcode Command Line Tools）。

Windows 不能直接运行 `.app`；源码使用 POSIX threads，Windows 需要 WSL 或 MinGW/pthreads 环境。

### Windows 双击版（Windows 10/11）

桌面版压缩包 `CPU性能跑分-Windows.zip` 解压后双击 `Run-CPU-Bench.bat` 即可。包内包含：

| 文件 | 用途 |
|---|---|
| `cpu-bench-x86_64-avx2.exe` | Intel/AMD，支持 AVX2 + FMA |
| `cpu-bench-x86_64-generic.exe` | 较老的 x86_64 CPU |
| `cpu-bench-arm64.exe` | Windows on ARM 原生版本 |
| `Run-CPU-Bench.bat` | 自动选择版本、运行并暂停窗口 |
| `src/bench_win.c` | Windows 移植源码 |

不需要安装编译器或管理员权限。首次运行 SmartScreen 可能提示未知发布者：选择“更多信息”→“仍要运行”。结果保存在 `%USERPROFILE%\CPU-Bench-Results`。

Windows 版已加入崩溃兜底：低层异常会写出 `cpu-bench-crash.log` 并弹出错误框；AVX2 版本失败时批处理会自动重试通用 x86_64 版本；直接双击 `.exe` 时程序也会在结束/崩溃后暂停，不再一闪而过。

去网吧或共享电脑时，双击 `Run-CPU-Bench-USB.bat`，结果会写到 U 盘里的 `results\` 文件夹，避免系统还原后丢失。

## 测试项目

| 项目 | 每次迭代 | 测量内容 | 单位 |
|---|---:|---|---|
| `INT64 add` | 8 次 64 位加法 | 整数 ALU 吞吐；8 条独立累加链 | Gop/s |
| `INT64 mul` | 8 次 64 位乘法 | 整数乘法器吞吐；8 条独立链 | Gop/s |
| `INT64 hash` | 1 轮 SplitMix64 | 乘法/异或/移位的串行依赖链，偏整数延迟 | Mhash/s |
| `FP32 FMA` | AVX2：8×8 lane；NEON：16×4 lane | 单精度融合乘加吞吐；均为 128 FLOP/迭代 | GFLOP/s |
| `FP64 FMA` | AVX2：8×4 lane；NEON：16×2 lane | 双精度融合乘加吞吐；均为 64 FLOP/迭代 | GFLOP/s |
| `FP64 sqrt` | x86 AVX2：8×4 lane；ARM/SSE：8 次标量 | 开方单元吞吐；AVX2 用 4 lane `vsqrtpd` | Gsqrt/s |

几个实现上的取舍：

- **独立累加链**：x86 用 8 条链覆盖 2 个 AVX2 FMA 端口；Apple Silicon 用 16 条链，覆盖最多 4 个 NEON FMA 管线，并把循环写成 `a = a*b + a`，让 AArch64 用单条 `fmls` 完成一次乘加，避免 `a*b + 1` 所需的寄存器搬运。
- **串行 hash**：`INT64 hash` 刻意保留 `x -> f(x)` 依赖，不会被编译器拆成多条并行链；它衡量的是整数混合/哈希类代码的延迟特性。
- **按架构匹配向量宽度**：x86 使用 256 位 AVX2 向量；Apple Silicon 使用原生 128 位 NEON 向量，不再把 256 位向量拆成两半。ARM 版本还会按 M1/M2/M3/M4 使用对应的 `-mcpu` 调度模型。若希望测 AVX-512 峰值，可自行把 x86 向量宽度改为 512 位并重新编译。
- **没有大数组**：被测循环只访问寄存器，因此分数主要反映执行单元、频率、IPC、SMT/多核扩展和功耗墙，而不是缓存或内存控制器。

## 评分方法

单项分：

```text
单项分 = 1000 × 实测吞吐 / 参考吞吐
```

单核分和多核分分别是 6 个单项分的**等权几何平均**：

```text
单核分 = exp( mean( ln(单项单核分) ) )
多核分 = exp( mean( ln(单项多核分) ) )
综合分 = sqrt(单核分 × 多核分)
```

综合分给单核和多核各 50% 权重，避免“核多就赢”完全压过单核性能。

参考机为 **Intel Core i7-9750H（6 物理核 / 12 逻辑线程）**，macOS 15.7.9，Apple Clang 17.0.0，编译参数见 Makefile。参考吞吐取两次标准模式实测中位数的均值后取三位有效数字：

| 项目 | 单核参考 | 12 线程参考 |
|---|---:|---:|
| `INT64 add` | 13.2 Gop/s | 67.4 Gop/s |
| `INT64 mul` | 3.74 Gop/s | 21.8 Gop/s |
| `INT64 hash` | 281 Mhash/s | 2780 Mhash/s |
| `FP32 FMA` | 114 GFLOP/s | 588 GFLOP/s |
| `FP64 FMA` | 57.0 GFLOP/s | 271 GFLOP/s |
| `FP64 sqrt` | 0.61 Gsqrt/s | 3.65 Gsqrt/s |

参考常数是 v1.0 在 i7-9750H 上测得的吞吐，因此 v1.0 参考机综合分约 993。换一台机器时，分数表示“相对 i7-9750H 的同一套工作负载吞吐”。这不是某个官方榜单的绝对分数，参考常数可以在 `bench.c` 的 `workloads[]` 中修改。

> v1.1 的 x86 版把 FP64 sqrt 改成 AVX2 4-lane 向量开方，参考机单核 sqrt 从 0.62 提升到 1.32 Gsqrt/s（约 2.1×），因此 v1.1 参考机综合分约 **1179**。这是实现优化，不是分数作弊；但 v1.0/v1.1 的分数不要直接混用。

> 多核参考值是 **12 线程全核**。如果使用 `--threads 4`，多核分会明显低于 1000，这是线程数不同的正常结果，不是性能下降。

> **跨架构提示**：Apple Silicon 的 FP 测试使用 128 位 NEON，而参考机是 256 位 AVX2。FP32/FP64 分数天然会低一些，不能直接当作“M1 只有 i7 的百分之多少”。M 系列上建议重点看原始吞吐、P 核模式和多核扩展。

## 用法

```text
./cpu-bench [options]

  --threads N      多核测试线程数（默认：逻辑核心数）
  --duration SEC   每个采样目标时长（默认 0.35 秒）
  --samples N      每个项目采样次数，取中位数（默认 3）
  --quick          快速模式：0.12 秒/次，1 次采样
  --pcores         多核测试只使用 P 核（Apple Silicon 异构芯片）
  --json PATH      额外写出 JSON 结果
  -h, --help       帮助
```

示例：

```bash
# 默认：全部逻辑线程
./cpu-bench

# 只测单核
./cpu-bench --threads 1

# 8 线程，5 次采样，结果写入 JSON
./cpu-bench --threads 8 --samples 5 --json result.json

# 快速检查
./cpu-bench --quick
```

## 参考机实测结果

环境：Intel Core i7-9750H，6C/12T，macOS 15.7.9，Apple Clang 17.0.0，`-O3 -march=native`，0.35 秒 × 3 次取中位数。

| 项目 | 单核吞吐 | 单核分 | 12 线程吞吐 | 多核分 |
|---|---:|---:|---:|---:|
| `INT64 add` | 14.06 Gop/s | 1066 | 71.47 Gop/s | 1060 |
| `INT64 mul` | 3.94 Gop/s | 1052 | 22.58 Gop/s | 1036 |
| `INT64 hash` | 297.27 Mhash/s | 1058 | 2991.90 Mhash/s | 1076 |
| `FP32 FMA` | 121.36 GFLOP/s | 1065 | 600.49 GFLOP/s | 1021 |
| `FP64 FMA` | 62.13 GFLOP/s | 1090 | 278.01 GFLOP/s | 1026 |
| `FP64 sqrt` | 1.32 Gsqrt/s | 2157 | 7.14 Gsqrt/s | 1958 |
| **合计** | | **1199** | | **1159** |

- **综合分：1179**
- **多核加速比：5.73×**（几何平均；12 逻辑线程 / 6 物理核）
- **物理核并行效率：95.4%**

完整输出和 JSON 见：

- `results/reference-i7-9750h-v1.1.txt`
- `results/reference-i7-9750h-v1.1.json`
- 旧版基线：`results/reference-i7-9750h.txt` / `.json`

运行间波动通常在几个百分点；`INT64 add` 和 `INT64 hash` 对后台负载、频率波动更敏感。比较两台机器时，建议使用相同的 `--duration`、`--samples` 和线程数，并在空闲、接电源、散热一致的环境下运行。

## x86 优化（v1.1）

Intel/AMD x86 版针对 AVX2 做了这些优化：

- **AVX2 向量开方**：`FP64 sqrt` 从 8 条标量 `sqrtsd` 改成 8 条 256 位 `vsqrtpd`，每次处理 4 个 double。参考机单核从 **0.62 → 1.32 Gsqrt/s**，多核从 **3.59 → 7.14 Gsqrt/s**，约 **2×**。
- **保持 FMA 满吞吐**：FP32/FP64 FMA 使用 8 条独立 AVX2 累加链，覆盖 2 个 FMA 端口；参考机已经接近理论峰值，继续加链不会更快。
- **整数项保持标量**：`INT64 add/mul` 用 8 条独立链覆盖 4 个 ALU 和乘法器，接近每周期 4 次加法 / 1 次乘法的上限。
- **高 QoS**：macOS 上使用 `USER_INTERACTIVE`，减少后台调度对单核测试的影响。

参考机 i7-9750H 的实测变化：

| 项目 | v1.0 | v1.1 | 变化 |
|---|---:|---:|---:|
| 单核综合 | 999 | 1199 | +20% |
| 多核综合 | 987 | 1159 | +17% |
| 综合 | 993 | 1179 | +19% |
| `FP64 sqrt` 单核 | 0.62 Gsqrt/s | 1.32 Gsqrt/s | **2.1×** |
| `FP64 sqrt` 多核 | 3.59 Gsqrt/s | 7.14 Gsqrt/s | **2.0×** |

其余项目的差异主要来自 QoS 和运行波动；真正的代码级提升集中在 sqrt。整体提升约 **10%**（用同一版高 QoS 基线 1076 对比 1179），因为 sqrt 只占 6 项中的 1 项，几何平均会摊薄。

## Apple Silicon 优化

- **型号专用编译**：M1/M2/M3/M4 分别用 `-mcpu=apple-m1` 到 `-mcpu=apple-m4` 编译，启动时按 CPU 型号选择。
- **128 位 NEON**：不再使用 256 位向量，避免在 128 位 NEON 上拆成两条指令。
- **16 条 FMA 链**：覆盖最多 4 个 NEON FMA 管线；循环使用单条 `fmls`，避免额外的 `mov`。
- **高 QoS**：线程设置为 `QOS_CLASS_USER_INTERACTIVE`，让 macOS 优先调度到 P 核。
- **P 核模式**：`--pcores` 只用性能核跑多核测试，适合看 M 系列的真实 P 核性能；默认仍使用全部逻辑核。
- **异构核心显示**：输出会显示 `4 performance + 4 efficiency` 这类信息，效率改为 `P-CORE RATIO`，不再按 8 个同构核算。

使用 P 核模式：

```bash
./cpu-bench --pcores
```

macOS 不提供硬亲和性绑定，`--pcores` 通过线程数 + 高 QoS 尽量把工作留在 P 核；系统仍可能在极端负载下迁移。

## 结果解读

- **先看原始吞吐**：分数是归一化结果，原始 Gop/s、GFLOP/s 才对应具体工作负载。
- **单核分**主要反映架构 IPC、频率和标量/向量执行能力；`FP32/FP64 FMA` 会明显受益于 AVX2/FMA。
- **多核分**还受物理核数、SMT、缓存、功耗墙和散热影响。普通 x86 机器显示 `PHYS EFF`，以物理核数为分母；Apple Silicon 显示 `P-CORE RATIO`，以 P 核数为分母，因此会超过 100%（E 核也在贡献吞吐）。`--pcores` 模式只跑 P 核，更适合看单核同构性能。
- **这不是内存基准**：所有测试都是寄存器驻留的算术循环，不反映缓存命中率、内存带宽或真实应用的访存行为。
- **不是峰值理论值**：编译器代码生成、操作系统调度、后台进程、温度和功耗策略都会影响结果。

## 实现要点

- `clock_gettime(CLOCK_MONOTONIC)` 计时，单次采样的目标时长可调。
- 每个项目先自适应校准迭代次数，使单次采样接近目标时长，避免固定迭代数在不同 CPU 上过快或过慢。
- 每个项目多次采样，取吞吐中位数；默认 3 次。
- 计时循环内的整数累加器通过空 `asm` 屏障强制落地，防止 LLVM 把循环折叠成闭式表达式。
- 所有 kernel 都是 `noinline`，并通过 volatile 校验和 sink 防止整个调用被死代码消除。
- Apple Silicon 路径使用 16 条独立 FMA 链和原生 128 位 NEON；x86 保持 8 条 AVX2 链。
- Apple Silicon 上线程使用 `USER_INTERACTIVE` QoS，并可通过 `--pcores` 只测 P 核。
- 多线程使用启动门闩（ready/go 两个条件变量）让线程尽量同时开始；线程创建不计入热循环时间。
- 编译时对整数 kernel 关闭自动向量化（`-fno-vectorize -fno-slp-vectorize`），确保测到的是预期的标量 ALU/乘法器；FP kernel 使用显式向量类型。

## 文件

```text
cpu-bench/
├── bench.c                         # 单文件实现
├── Makefile
├── README.md
├── cpu-bench-x86_64-avx2           # 预编译：Intel AVX2 + FMA
├── cpu-bench-x86_64-generic        # 预编译：通用 x86_64
├── cpu-bench-arm64-m1              # 预编译：M1 调优
├── cpu-bench-arm64-m2              # 预编译：M2 调优
├── cpu-bench-arm64-m3              # 预编译：M3 调优
├── cpu-bench-arm64-m4              # 预编译：M4 调优
├── cpu-bench-arm64                 # 预编译：通用 arm64 回退
└── results/
    ├── reference-i7-9750h.txt
    ├── reference-i7-9750h.json
    ├── reference-i7-9750h-v1.1.txt
    └── reference-i7-9750h-v1.1.json
```

依赖（仅重新编译时需要）：C11 编译器、POSIX threads、libm。支持 macOS 和 Linux（x86_64 / ARM64）；没有 Windows 线程层。

## 常见问题

**为什么我的分数不是 1000？**

1000 只对应参考机 i7-9750H。不同 CPU、不同编译器、不同 `-march=native` 代码生成、后台负载和散热都会改变分数。

**为什么多核加速比不是 12×？**

12 是逻辑线程数，物理核只有 6 个；同时全核 AVX/FMA 负载会触发功耗和频率限制。5.85× 相对 6 个物理核已经是 97.5% 的并行效率。

**为什么没有内存/缓存测试？**

这是刻意的设计目标：本程序只测计算能力。内存带宽、缓存延迟和真实应用性能需要另外的 benchmark。

**可以跨机器使用吗？**

可以，但要分情况：

- **桌面 `.app`**：内置 Intel AVX2、Intel 通用 x86_64、Apple Silicon 三套二进制，按 CPU 自动选择，换 Mac 通常可直接双击。
- **自己用 `make` 编译的 `cpu-bench`**：使用了 `-march=native`，不要直接拷到另一台机器；在目标机重新 `make`。
- **Windows**：不能直接运行 `.app`，源码也需要 WSL 或 MinGW/pthreads 环境。
- **没有匹配的预编译版本且没有编译器**：程序会给出编译失败提示，安装 Xcode Command Line Tools 后即可运行。

## 许可

可自由修改和使用。
