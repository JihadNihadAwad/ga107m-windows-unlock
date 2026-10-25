# GA107M GPU Unlock Hold - visible tray-style console the user can close
# with a keypress. Runs hold_gpu.py logic; closing the window releases the
# dGPU back to normal D3cold parking (unlock erased on next park).
#
# Usage:  python hold_gpu_windowed.py
# Keep the window open to keep the unlock; press Ctrl+C (or close the
# window) to release. Meant to be launched from a shortcut or the
# Start Menu startup folder so it is easy to see and quit.
import time

import torch

a = torch.randn(64, 64, device="cuda")
torch.cuda.synchronize()
print("=" * 60)
print(f"  GA107M UNLOCK HOLD - {torch.cuda.get_device_name(0)}")
print("=" * 60)
print("  The dGPU is held awake (D3cold blocked) to preserve the")
print("  SS0=0x88888888 selector override from the unlock boot.")
print()
print("  KEEP THIS WINDOW OPEN to keep the unlock active.")
print("  Press Ctrl+C (or close this window) to release the GPU")
print("  back to normal power saving (the unlock then erases).")
print("=" * 60)
try:
    while True:
        time.sleep(30)
        a = (a @ a)[:64, :64]  # tiny keepalive to prove liveness
        torch.cuda.synchronize()
        print(f"  [hold alive {time.strftime('%H:%M:%S')}] CUDA context OK")
except KeyboardInterrupt:
    print()
    print("  Released. The dGPU may park to D3cold; if it does, rerun the")
    print("  unlock boot to restore 1024-class FP16/BF16 throughput.")
