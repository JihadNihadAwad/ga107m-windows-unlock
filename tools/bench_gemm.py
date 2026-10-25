# GA107M tensor-throughput benchmark with live clock sampling.
# Computes ops/SM/clock so the result is independent of GPU boost clocks.
# Locked GA107M reference (IHaBiS02 measured at 855 MHz): FP16/BF16-FP32acc = 512,
# TF32 = 256, FP32 FFMA = 128 ops/SM/clk. Unlocked: FP16/BF16 = 1024, TF32 = 512.
import statistics
import subprocess
import threading
import time

import torch

SM_COUNT = 20  # RTX 3050 Ti Laptop (GA107M): 2560 CUDA cores / 128 per SM
N = 8192
WARMUP = 10
ITERS = 30


def smi(*fields):
    out = subprocess.run(
        ["nvidia-smi", f"--query-gpu={','.join(fields)}", "--format=csv,noheader,nounits"],
        capture_output=True, text=True)
    return [v.strip() for v in out.stdout.strip().split(",")]


class Sampler(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.stop_evt = threading.Event()
        self.clocks = []
        self.power = []

    def run(self):
        while not self.stop_evt.is_set():
            try:
                vals = smi("clocks.sm", "power.draw")
                self.clocks.append(int(vals[0]))
                self.power.append(float(vals[1]))
            except (ValueError, IndexError, OSError):
                pass
            time.sleep(0.05)


def bench(a, b, label, tf32):
    torch.backends.cuda.matmul.allow_tf32 = tf32
    for _ in range(WARMUP):
        torch.matmul(a, b)
    torch.cuda.synchronize()
    s = Sampler()
    s.start()
    start = time.perf_counter()
    for _ in range(ITERS):
        torch.matmul(a, b)
    torch.cuda.synchronize()
    elapsed = (time.perf_counter() - start) / ITERS
    s.stop_evt.set()
    s.join()
    flops = 2 * N**3
    tflops = flops / (elapsed * 1e12)
    clk = statistics.median(s.clocks) if s.clocks else float("nan")
    pwr = statistics.median(s.power) if s.power else float("nan")
    ops = tflops * 1e12 / (SM_COUNT * clk * 1e6) if s.clocks else float("nan")
    print(f"{label:<24} {tflops:7.2f} TFLOPS   SM clock {clk:5.0f} MHz   "
          f"~{pwr:4.1f} W   {ops:7.1f} ops/SM/clk")
    return tflops, clk, ops


print(f"PyTorch {torch.__version__} (CUDA {torch.version.cuda})")
print(f"Device: {torch.cuda.get_device_name(0)}")
info = smi("driver_version", "vbios_version", "power.limit")
print(f"Driver: {info[0]}   VBIOS: {info[1]}   Power limit: {info[2]} W")
print(f"\n{N}x{N} GEMM, {ITERS} iterations, median SM clock during run.\n")

a16 = torch.randn(N, N, device="cuda", dtype=torch.float16)
b16 = torch.randn(N, N, device="cuda", dtype=torch.float16)
results = {}
results["FP16 in / FP32 acc"] = bench(a16, b16, "FP16 in / FP32 acc", tf32=False)
del a16, b16
torch.cuda.empty_cache()

a_bf = torch.randn(N, N, device="cuda", dtype=torch.bfloat16)
b_bf = torch.randn(N, N, device="cuda", dtype=torch.bfloat16)
results["BF16 in / FP32 acc"] = bench(a_bf, b_bf, "BF16 in / FP32 acc", tf32=False)
del a_bf, b_bf
torch.cuda.empty_cache()

a32 = torch.randn(N, N, device="cuda", dtype=torch.float32)
b32 = torch.randn(N, N, device="cuda", dtype=torch.float32)
results["TF32 in / FP32 acc"] = bench(a32, b32, "TF32 in / FP32 acc", tf32=True)
results["FP32 (pedantic)"] = bench(a32, b32, "FP32 (pedantic)", tf32=False)
del a32, b32

print("\nLocked reference : FP16/BF16 ~512, TF32 ~256, FP32 ~128 ops/SM/clk")
print("Unlocked (SS0)   : FP16/BF16 ~1024 ops/SM/clk")
print("Unlocked (SS0+SS1): FP16/BF16 ~1024, TF32 ~512 ops/SM/clk")
locked_fp16 = results["FP16 in / FP32 acc"][2]
if locked_fp16 < 700:
    print(f"\nVERDICT: FP16/BF16 issue rate ~{locked_fp16:.0f} ops/SM/clk -> CARD IS LOCKED (512-class)")
elif locked_fp16 > 850:
    print(f"\nVERDICT: FP16/BF16 issue rate ~{locked_fp16:.0f} ops/SM/clk -> UNLOCKED (1024-class)")
else:
    print(f"\nVERDICT: ambiguous ({locked_fp16:.0f} ops/SM/clk) - rerun with locked clocks")
