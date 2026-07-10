"""One-time conversion: extract each voice's (510,1,256) float32 style array
from models/kokoro/voices-v1.0.bin (an .npz file) into a raw binary file
native/models/kokoro/voices/<name>.bin, so the C++ KokoroEngine can load it
with a plain ifstream (no npy/zip parser needed in C++)."""
import os
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "models", "kokoro", "voices-v1.0.bin")
OUT_DIR = os.path.join(HERE, "..", "models", "kokoro", "voices")

os.makedirs(OUT_DIR, exist_ok=True)

data = np.load(SRC)
print(f"{len(data.files)} voices found")
for name in data.files:
    arr = np.asarray(data[name], dtype=np.float32)
    assert arr.shape == (510, 1, 256), f"{name}: unexpected shape {arr.shape}"
    out_path = os.path.join(OUT_DIR, f"{name}.bin")
    arr.tofile(out_path)
    print(f"  {name}: {arr.shape} -> {out_path} ({os.path.getsize(out_path)} bytes)")

print("done")
