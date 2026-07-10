"""Export the Victoria German female Kokoro voice (kikiri-tts checkpoint) to ONNX.

Mirrors the I/O signature of models/kokoro_de_martin/kokoro-martin.onnx:
  inputs:  tokens int64[1,-1], style float32[1,256], speed float32[1]
  outputs: waveform float32[-1], duration int64[-1]
"""
import os
import sys

import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "kokoro_pkg"))

from kokoro.model import KModel, KModelForONNX  # noqa: E402

SRC_DIR = os.path.join(HERE, "..", "..", "models", "kokoro_de_victoria_src")
OUT_DIR = os.path.join(HERE, "..", "..", "models", "kokoro_de_victoria")
CONFIG_PATH = os.path.join(SRC_DIR, "config.json")
CHECKPOINT_PATH = os.path.join(SRC_DIR, "kikiri_german_victoria_ep10.pth")
OUTPUT_PATH = os.path.join(OUT_DIR, "kokoro-victoria.onnx")


def main():
    print("Loading KModel from checkpoint...")
    kmodel = KModel(
        repo_id="kikiri-tts/kikiri-german-victoria",
        config=CONFIG_PATH,
        model=CHECKPOINT_PATH,
        disable_complex=True,
    )
    kmodel.eval()

    wrapper = KModelForONNX(kmodel)
    wrapper.eval()

    # Dummy inputs matching the (tokens, style, speed) signature.
    dummy_tokens = torch.LongTensor([[0, 50, 83, 54, 54, 57, 0]])  # [1, L]
    dummy_style = torch.randn(1, 256, dtype=torch.float32)
    dummy_speed = torch.ones(1, dtype=torch.float32)

    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Exporting to {OUTPUT_PATH} ...")
    torch.onnx.export(
        wrapper,
        (dummy_tokens, dummy_style, dummy_speed),
        OUTPUT_PATH,
        input_names=["tokens", "style", "speed"],
        output_names=["waveform", "duration"],
        dynamic_axes={
            "tokens": {1: "tokens_len"},
            "waveform": {0: "samples"},
            "duration": {0: "duration_len"},
        },
        opset_version=18,
        dynamo=False,
    )
    print("Done.")


if __name__ == "__main__":
    main()
