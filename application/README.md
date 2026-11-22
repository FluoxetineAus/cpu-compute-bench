# Benchmark 2.0 Application

一个 **Geekbench 风格**的应用型跑分，和 `Benchmark 2.0 Ultimate` 的纯计算模块完全独立。支持单线程和多线程。

## 技术白皮书

[Benchmark-Application-2.0-技术白皮书.pdf](docs/Benchmark-Application-2.0-技术白皮书.pdf)

13 页，包含应用任务、线程模型、单核/多核评分、跨架构边界、i7/M1 实测图表、优点与不足、使用与路线图。

## 和 CPU Compute 的区别

| | CPU Compute | Application |
|---|---|---|
| 题目 | 6 个寄存器驻留微内核 | 8 个应用型 workload |
| 向量 | 手写 AVX2 / NEON | 不手写 SIMD，交给编译器 |
| 内存 | 不测 | 包含解析、图像、排序、拷贝 |
| 分数 | ARCH / NATIVE / SYSTEM | SINGLE / MULTI / OVERALL |
| 跨架构 | ARCH 标量 | 统一基线，多任务摊薄 |

## 八个任务

| 任务 | 类型 | 单位 | i7-9750H 参考 |
|---|---|---|---:|
| SHA-256 | Crypto | MB/s | 242.0 |
| LZSS compress + decompress | Compression | MB/s | 75.9 |
| JSON parse | Parsing | MB/s | 686.6 |
| Text search (BMH) | Text | MB/s | 1592.2 |
| Image blur 5x5 | Imaging | MPix/s | 43.87 |
| Heap sort 1M u32 | Sorting | Melem/s | 8.28 |
| Matmul 256³ | Numeric | GFLOP/s | 7.96 |
| Memory copy 128 MB | Memory | GB/s | 13.64 |

单线程和多线程都支持。多线程模式下每个线程使用独立的工作区/输出缓冲，避免数据竞争；文本搜索、排序、拷贝使用自己的 C 实现，不依赖 `strstr`/`qsort`/`memcpy` 的平台差异。

## 评分

```text
单项单核分 = 1000 × 单核实测吞吐 / i7-9750H 参考吞吐
单项多核分 = 1000 × 多核实测吞吐 / 同一单核参考
SINGLE APP = 8 项单核分的几何平均
MULTI APP  = 8 项多核分的几何平均
MULTI SCALE = 8 项多核/单核倍率的几何平均
OVERALL = sqrt(SINGLE × MULTI)
```

统一基线：**i7-9750H = 1000**。

没有 ARM 单独基线，也不给任何架构特殊加权。多核分包含实测缩放，不是把单核分乘以核心数。

## 运行

macOS：

**`Benchmark Application 2.0.app`**

**`Benchmark-Application-2.0.zip`**

Windows：

**`Benchmark-Application-2.0-Windows.zip`** → `Run-Application-Bench.bat`

命令行：

```bash
./application-bench
./application-bench --quick
./application-bench --samples 5
./application-bench --threads 8
./application-bench --json result.json
```

## 参考机实测

i7-9750H，12 线程，3 次采样（热机示例）：

```text
SINGLE APP   953
MULTI APP    3892
MULTI SCALE  4.09x
OVERALL      1925
```

冷机单线程约 995。单项分基本都在 1000 附近；内存拷贝接近单核上限，是多核缩放的主要瓶颈。

## M1 参考（3 vCPU CI）

GitHub Actions Apple M1 (Virtual)，单线程，3 次采样：

```text
SINGLE APP   1328
MULTI APP    3550
MULTI SCALE  2.67x
OVERALL      2172
```

这是 3 vCPU CI，不是 8 核本机 M1。单线程下 M1 在压缩、图像、矩阵、排序和内存拷贝上较强，SHA-256/文本搜索仍低于 i7。

## 注意

- 这是应用型综合分，不是纯 CPU IPC，也不是峰值 FLOPS。
- 多线程模式下每个线程跑一份完整任务，使用独立缓冲；任务间不共享输出。
- 内存拷贝是共享内存带宽瓶颈，多核缩放会明显低于其它任务。
- 编译器、OS、电源模式和散热仍会影响结果。
- 和 CPU Compute 的 ARCH/NATIVE/SYSTEM 分数不可互换。
- 和 Geekbench 分数也不可直接比较：workload 组成和权重不同。
