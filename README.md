# Benchmark 2.0

一个面向研究和实测的 CPU 性能基准套件，包含三个**互相独立**的模块：

| 模块 | 测什么 | 分数 |
|---|---|---|
| **CPU Compute** | 6 个寄存器驻留的整数/浮点微内核 | ARCH / NATIVE / SYSTEM |
| **Application** | 8 个真实应用任务，单线程 + 多线程 | SINGLE APP / MULTI APP / OVERALL |
| **Memory / Cache** | L1/L2/L3/DRAM 延迟与顺序带宽 | 独立数据，不进入 CPU 总分 |

支持 **macOS / Windows**，**x86-64 / Apple Silicon**。

## 下载

预编译版本在 [Releases](https://github.com/FluoxetineAus/cpu-compute-bench/releases/latest)：

- macOS CPU：`Benchmark-2.0-Ultimate.zip`
- Windows CPU：`Benchmark-2.0-Ultimate-Windows.zip`
- macOS Application：`Benchmark-Application-2.0.zip`
- Windows Application：`Benchmark-Application-2.0-Windows.zip`
- macOS Memory：`Benchmark-Memory.zip`
- Windows Memory：`Benchmark-Memory-Windows.zip`

## 三个模块的区别

### CPU Compute

- 纯计算：整数加/乘/hash、FP32/FP64 FMA、FP64 sqrt
- 寄存器驻留，不测内存带宽
- x86 使用 AVX2 256 位；Apple Silicon 使用 NEON 128 位
- 每次运行自动包含：
  - **ARCH**：标量单线程，跨 x86/ARM 架构
  - **SINGLE**：原生向量单核
  - **MULTI**：全核，分数 = 单核分 × 实测倍率
  - **OVERALL** = sqrt(SINGLE × MULTI)

### Application

- Geekbench 风格，但不是 Geekbench
- 8 个任务：SHA-256、LZSS、JSON、文本搜索、图像模糊、堆排序、矩阵乘法、内存拷贝
- 统一基线：i7-9750H = 1000；没有 ARM 单独基线
- **SINGLE APP** 可做跨架构应用层比较
- **MULTI APP** 默认全核是整机分，不是架构分
- 不手写 AVX2/NEON；文本搜索、排序、拷贝使用自己的 C 实现

### Memory / Cache

- 指针追逐延迟：L1 / L2 / L3/LLC / DRAM
- 顺序带宽：Read / Write / Copy / Triad
- 自动识别缓存大小
- 完全独立，不污染 CPU 分数

## 快速开始

macOS / Linux：

```bash
git clone https://github.com/FluoxetineAus/cpu-compute-bench.git
cd cpu-compute-bench

make                 # 构建 CPU、Application、Memory
./cpu-bench          # CPU Compute
./application/application-bench --threads 8
./memory/memory-bench
```

只构建一个模块：

```bash
make cpu
make application
make memory
```

Windows：

```bat
cd windows
build-windows.bat
```

或直接用 MinGW-w64：

```bash
x86_64-w64-mingw32-gcc -O3 -march=haswell bench_win.c -o cpu-bench.exe -ladvapi32 -luser32
```

## 参考成绩

CPU Compute 2.0：

| 机器 | ARCH | SINGLE | MULTI | SCALE | OVERALL |
|---|---:|---:|---:|---:|---:|
| i7-9750H 6C/12T | 1078 | 1006 | 5693 | 5.66x | 2394 |
| i7-12700KF 12C/20T | 1345 | 1050 | 10440 | 9.95x | 3310 |
| Apple M1 3 vCPU CI | 991 | 893 | 2150 | 2.41x | 1385 |

Application 2.0：

| 机器 | SINGLE APP | MULTI APP | SCALE | OVERALL |
|---|---:|---:|---:|---:|
| i7-9750H 12T | 1154 | 5023 | 4.35x | 2408 |
| i7-12700KF 20T | 1778 | 13621 | 7.66x | 4922 |
| Apple M1 3 vCPU CI | 1328 | 3550 | 2.67x | 2172 |

> CPU 与 Application 的分数不可互换。M1 为 3 vCPU 虚拟环境，不是 8 核本机。

## 评分原则

### CPU Compute

```text
SINGLE = 1000 × 单核吞吐 / i7-9750H 单核参考
MULTI  = 1000 × 多核吞吐 / 同一单核参考
       ≈ SINGLE × MULTI SCALE
OVERALL = sqrt(SINGLE × MULTI)
ARCH = 标量单线程分，跨架构用
```

### Application

```text
SINGLE APP = 8 项单核分的几何平均
MULTI APP  = 8 项多核分的几何平均
MULTI SCALE = 实测多核倍率
OVERALL = sqrt(SINGLE × MULTI)
```

统一基线：i7-9750H = 1000。

## 跨架构怎么读

| 需求 | 指标 |
|---|---|
| 跨 x86 / ARM 的纯标量架构比较 | CPU Compute **ARCH** |
| 跨 x86 / ARM 的应用层单线程比较 | Application **SINGLE APP** |
| 同架构原生向量 | CPU Compute **NATIVE** |
| 整机多核 | CPU Compute **SYSTEM** / Application **MULTI** |
| 固定线程数的多核近似比较 | Application `--threads N` |
| 缓存/内存 | Memory 模块，单独看 |

**不要**拿 x86 的 AVX2 256 位和 M1 的 NEON 128 位直接比“架构”，这是 ISA 宽度差异，不是公平的架构分。

## 技术白皮书

- [CPU Compute 技术白皮书](docs/Benchmark-2.0-Ultimate-技术白皮书.pdf)
- [Application 技术白皮书](application/docs/Benchmark-Application-2.0-技术白皮书.pdf)

## 目录结构

```text
bench.c                       CPU Compute（macOS）
windows/bench_win.c           CPU Compute（Windows）
application/application_bench.c
memory/memory_bench.c
docs/                         文档与白皮书
application/docs/
results/                      参考结果
.github/workflows/            macOS CI
```

## 已知限制

- CPU Compute 的 NATIVE 分保留 AVX2/NEON 宽度差异，只适合同架构比较。
- Application 的 MULTI 默认全核是整机分，不是架构分。
- Memory 是单线程模块。
- 短时测试受 boost、散热、后台负载影响；9750H 热机可能低 10~20%。
- Windows ARM64 路径存在，但不是 Mac 的深度 NEON 优化版。
- 没有 AVX-512 / SVE 路径。
- 不测 GPU、NPU、磁盘、网络。

## License

MIT。见 [LICENSE](LICENSE)。

## 致谢

参考机：Intel Core i7-9750H。CI：GitHub Actions `macos-15` 的 Apple M1 虚拟环境。
