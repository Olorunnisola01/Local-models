#!/usr/bin/env python3
"""Replaces depthwise (group>1) ConvTranspose nodes in kokoro-v1.0.onnx with a
mathematically equivalent decomposition (zero-insertion upsample -> pad ->
regular grouped Conv with a reversed kernel -> bias add) that DirectML's
grouped-ConvTranspose kernel rejects at run time with DML_E_INVALIDARG on some
GPU drivers, but the decomposed ops (Conv with group=1..N, Slice, Pad, Reshape,
Concat, Add, Mul) all run fine on DML.

The decomposition is exact (verified to bit-identical float32 output against
ONNX Runtime's CPU ConvTranspose for the kernel_shape=[3], strides=[2],
pads=[1,1], output_padding=[1], dilations=[1] configuration used by all 3
affected nodes in this model).

Usage:
    python fix_kokoro_dml.py <in.onnx> <out.onnx>
"""
import sys
import onnx
from onnx import helper, numpy_helper
import numpy as np

def decompose(node, idx):
    """Returns (new_nodes, new_initializers) implementing `node` (a depthwise
    ConvTranspose with kernel_shape=[3], strides=[2], pads=[1,1],
    output_padding=[1], dilations=[1]) using DML-friendly ops."""
    attrs = {a.name: helper.get_attribute_value(a) for a in node.attribute}
    assert list(attrs["kernel_shape"]) == [3]
    assert list(attrs["strides"]) == [2]
    assert list(attrs["pads"]) == [1, 1]
    assert list(attrs["output_padding"]) == [1]
    assert list(attrs["dilations"]) == [1]
    group = attrs["group"]

    x, w, b = node.input[0], node.input[1], node.input[2]
    y = node.output[0]
    p = f"__dml_fix_{idx}"

    inits = [
        numpy_helper.from_array(np.array([0, 0, -1, 1], dtype=np.int64), f"{p}_shape_4d"),
        numpy_helper.from_array(np.array([0, 0, -1], dtype=np.int64), f"{p}_shape_3d"),
        numpy_helper.from_array(np.array([1, group, 1], dtype=np.int64), f"{p}_bias_shape"),
        numpy_helper.from_array(np.array(0.0, dtype=np.float32), f"{p}_zero_scalar"),
        numpy_helper.from_array(np.array([0, 0, 1, 0, 0, 1], dtype=np.int64), f"{p}_pad_amounts"),
        numpy_helper.from_array(np.array([2], dtype=np.int64), f"{p}_rev_start"),
        numpy_helper.from_array(np.array([-4], dtype=np.int64), f"{p}_rev_end"),
        numpy_helper.from_array(np.array([2], dtype=np.int64), f"{p}_rev_axis"),
        numpy_helper.from_array(np.array([-1], dtype=np.int64), f"{p}_rev_step"),
    ]

    nodes = [
        helper.make_node("Reshape", [x, f"{p}_shape_4d"], [f"{p}_x4"], name=f"{p}_x4"),
        helper.make_node("Mul", [f"{p}_x4", f"{p}_zero_scalar"], [f"{p}_zero4"], name=f"{p}_zero4"),
        helper.make_node("Concat", [f"{p}_x4", f"{p}_zero4"], [f"{p}_cat"], axis=3, name=f"{p}_cat"),
        helper.make_node("Reshape", [f"{p}_cat", f"{p}_shape_3d"], [f"{p}_seq2L"], name=f"{p}_seq2L"),
        helper.make_node("Pad", [f"{p}_seq2L", f"{p}_pad_amounts"], [f"{p}_padded"], mode="constant", name=f"{p}_padded"),
        helper.make_node("Slice", [w, f"{p}_rev_start", f"{p}_rev_end", f"{p}_rev_axis", f"{p}_rev_step"], [f"{p}_w_flip"], name=f"{p}_w_flip"),
        helper.make_node("Conv", [f"{p}_padded", f"{p}_w_flip"], [f"{p}_conv_out"], group=group,
                         kernel_shape=[3], strides=[1], pads=[0, 0], dilations=[1], name=f"{p}_conv"),
        helper.make_node("Reshape", [b, f"{p}_bias_shape"], [f"{p}_bias_r"], name=f"{p}_bias_r"),
        helper.make_node("Add", [f"{p}_conv_out", f"{p}_bias_r"], [y], name=f"{p}_add"),
    ]
    return nodes, inits


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <in.onnx> <out.onnx>", file=sys.stderr)
        return 1

    model = onnx.load(sys.argv[1])
    graph = model.graph

    new_nodes = []
    new_inits = list(graph.initializer)
    idx = 0
    replaced = []
    for node in graph.node:
        group = 1
        if node.op_type == "ConvTranspose":
            for a in node.attribute:
                if a.name == "group":
                    group = helper.get_attribute_value(a)
        if node.op_type == "ConvTranspose" and group > 1:
            decomp_nodes, decomp_inits = decompose(node, idx)
            new_nodes.extend(decomp_nodes)
            new_inits.extend(decomp_inits)
            replaced.append(node.name)
            idx += 1
        else:
            new_nodes.append(node)

    if not replaced:
        print("ERROR: no depthwise (group>1) ConvTranspose nodes found", file=sys.stderr)
        return 1

    del graph.node[:]
    graph.node.extend(new_nodes)
    del graph.initializer[:]
    graph.initializer.extend(new_inits)

    onnx.checker.check_model(model)
    onnx.save(model, sys.argv[2])
    print(f"Replaced {len(replaced)} ConvTranspose nodes: {replaced}")
    print(f"Wrote {sys.argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
