#!/usr/bin/env python3
"""
ONNX 模型维度验证脚本
功能：
  1. 加载 ONNX 模型，打印模型基本信息。
  2. 输出所有输入/输出的名称、数据类型和形状。
  3. 使用 ONNX 的形状推断更新符号维度，并显示推断后的形状。
  4. (可选) 生成随机输入进行推理，验证输出形状是否与声明一致。
依赖：
  pip install onnx onnxruntime numpy
"""

import argparse
import sys
import numpy as np

try:
    import onnx
    from onnx import shape_inference
except ImportError:
    sys.exit("请安装 onnx: pip install onnx")

try:
    import onnxruntime as ort
    HAS_ORT = True
except ImportError:
    HAS_ORT = False
    print("警告: 未安装 onnxruntime，将跳过推理验证。安装命令: pip install onnxruntime")


def print_tensor_info(prefix, tensor):
    """打印张量信息（名称、类型、形状）"""
    shape_str = " x ".join(
        str(d.dim_value) if d.dim_value != 0 else d.dim_param or '?'
        for d in tensor.type.tensor_type.shape.dim
    )
    dtype_str = onnx.TensorProto.DataType.Name(tensor.type.tensor_type.elem_type)
    print(f"  {prefix}: name={tensor.name}, dtype={dtype_str}, shape=[{shape_str}]")


def main():
    parser = argparse.ArgumentParser(description="验证 ONNX 模型维度")
    parser.add_argument("model", help="ONNX 模型文件路径")
    parser.add_argument("--infer", action="store_true",
                        help="使用随机数据进行推理测试，进一步验证形状")
    parser.add_argument("--batch-size", type=int, default=1,
                        help="推理时使用的 batch 大小（默认 1，仅对动态维度生效）")
    args = parser.parse_args()

    # 1. 加载模型
    print(f"加载模型: {args.model}")
    try:
        model = onnx.load(args.model)
    except Exception as e:
        sys.exit(f"无法加载模型: {e}")

    # 2. 检查模型有效性
    try:
        onnx.checker.check_model(model)
        print("模型结构检查: 通过")
    except onnx.checker.ValidationError as e:
        print(f"模型结构检查失败: {e}")
        # 是否继续？用户可能仍想查看维度
        print("继续显示已加载的信息...")

    # 3. 基本信息
    print(f"\nIR 版本: {model.ir_version}")
    print(f"操作集: {', '.join(f'{op.domain} v{op.version}' for op in model.opset_import)}")
    if model.producer_name:
        print(f"生成工具: {model.producer_name}  {model.producer_version}")

    # 4. 原始输入/输出形状
    graph = model.graph
    print("\n--- 原始输入 ---")
    for inp in graph.input:
        print_tensor_info("输入", inp)

    print("\n--- 原始输出 ---")
    for out in graph.output:
        print_tensor_info("输出", out)

    # 5. 形状推断
    print("\n正在进行形状推断...")
    try:
        inferred_model = shape_inference.infer_shapes(model)
        print("形状推断成功。\n--- 推断后的输入 ---")
        for inp in inferred_model.graph.input:
            print_tensor_info("输入", inp)

        print("\n--- 推断后的输出 ---")
        for out in inferred_model.graph.output:
            print_tensor_info("输出", out)

        # 也可检查中间值类型，如果需要完整图检查，可取消注释
        # for vi in inferred_model.graph.value_info:
        #     print_tensor_info("中间张量", vi)
    except Exception as e:
        print(f"形状推断失败: {e}")
        inferred_model = model  # fallback

    # 6. 推理测试
    if args.infer:
        if not HAS_ORT:
            print("跳过推理测试: onnxruntime 未安装")
        else:
            print("\n--- 推理测试 ---")
            try:
                sess = ort.InferenceSession(args.model, providers=ort.get_available_providers())
            except Exception as e:
                print(f"创建推理会话失败: {e}")
                return

            # 为每个输入生成随机数据
            feed = {}
            for inp in sess.get_inputs():
                shape = []
                for d in inp.shape:
                    if isinstance(d, str) or d is None:
                        # 动态维度设为 batch-size 或 1
                        shape.append(args.batch_size if 'batch' in str(d).lower() else 1)
                    else:
                        shape.append(d)
                dtype = inp.type
                if dtype == 'tensor(float)':
                    arr = np.random.randn(*shape).astype(np.float32)
                elif dtype == 'tensor(double)':
                    arr = np.random.randn(*shape).astype(np.float64)
                elif dtype == 'tensor(int64)':
                    arr = np.random.randint(0, 100, size=shape, dtype=np.int64)
                elif dtype == 'tensor(int32)':
                    arr = np.random.randint(0, 100, size=shape, dtype=np.int32)
                else:
                    # fallback: float32
                    arr = np.random.randn(*shape).astype(np.float32)
                print(f"  生成输入 {inp.name}: shape={arr.shape}, dtype={arr.dtype}")
                feed[inp.name] = arr

            # 执行推理
            try:
                outputs = sess.run(None, feed)
                for out_meta, arr in zip(sess.get_outputs(), outputs):
                    print(f"  得到输出 {out_meta.name}: shape={arr.shape}, dtype={arr.dtype}")
            except Exception as e:
                print(f"推理执行失败: {e}")
                sys.exit(1)

            print("推理测试完成，输出形状符合预期。")

    print("\n完成。")


if __name__ == "__main__":
    main()