# Memory / Cache Benchmark

独立于 CPU Compute 和 Application 的缓存/内存模块。

## 测什么

- 指针追逐延迟：L1 / L2 / L3/LLC / DRAM
- 顺序带宽：Read / Write / Copy / Triad
- 自动识别缓存大小，选择工作集
- 默认 256 MB 带宽工作集

## 构建与运行

```bash
make
./memory-bench
./memory-bench --quick
./memory-bench --samples 5 --size 512 --json result.json
```

## 说明

- 单线程模块
- 不进入 CPU 或 Application 总分
- 延迟包含 TLB / page walk 等真实路径
- 不同缓存大小、内存通道和频率下不可直接套用

参考结果见 `results/`。
