// Standalone tool: prints input/output tensor names, shapes, and element
// types for an ONNX model. Used during engine integration to verify tensor
// names/shapes against the C++ pipeline code.
#include <cstdio>
#include <string>
#include <onnxruntime_cxx_api.h>

static const char* typeName(ONNXTensorElementDataType t) {
    switch (t) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: return "float32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: return "int64";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: return "int32";
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL: return "bool";
        default: return "other";
    }
}

static void printIO(const char* label, Ort::Session& session, Ort::AllocatorWithDefaultOptions& alloc, bool isInput) {
    size_t count = isInput ? session.GetInputCount() : session.GetOutputCount();
    for (size_t i = 0; i < count; ++i) {
        auto name = isInput ? session.GetInputNameAllocated(i, alloc) : session.GetOutputNameAllocated(i, alloc);
        auto typeInfo = isInput ? session.GetInputTypeInfo(i) : session.GetOutputTypeInfo(i);
        auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
        auto shape = tensorInfo.GetShape();
        std::printf("%s %s: %s shape=[", label, name.get(), typeName(tensorInfo.GetElementType()));
        for (size_t d = 0; d < shape.size(); ++d) {
            std::printf("%s%lld", d ? "," : "", static_cast<long long>(shape[d]));
        }
        std::printf("]\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: dump_onnx_io <model.onnx>\n");
        return 1;
    }
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "dump_onnx_io");
    Ort::SessionOptions opts;
    Ort::Session session(env, std::wstring(argv[1], argv[1] + std::string(argv[1]).size()).c_str(), opts);
    Ort::AllocatorWithDefaultOptions alloc;
    printIO("input ", session, alloc, true);
    printIO("output", session, alloc, false);
    return 0;
}
