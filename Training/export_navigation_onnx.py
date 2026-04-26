import argparse
import json
from datetime import datetime
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch
from stable_baselines3 import SAC

OBSERVATION_LAYOUT = [
    "target_local_dir_x",
    "target_local_dir_y",
    "target_local_dir_z",
    "target_distance_norm",
    "ray_0",
    "ray_1",
    "ray_2",
    "ray_3",
    "ray_4",
    "ray_5",
    "ray_6",
    "ray_7",
    "target_direction_clearance",
    "target_side_clearance_pos_angle",
    "target_side_clearance_neg_angle",
    "previous_action_direction_safety",
]


class DeterministicActorWrapper(torch.nn.Module):
    def __init__(self, actor: torch.nn.Module, action_dim: int):
        super().__init__()
        self.actor = actor
        self.action_dim = action_dim
        self.export_mode = self._detect_export_mode()

    def _detect_export_mode(self) -> str:
        if hasattr(self.actor, "get_action_dist_params"):
            return "get_action_dist_params"
        return "forward"

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        if self.export_mode == "get_action_dist_params":
            mean_actions, _, _ = self.actor.get_action_dist_params(obs)
            return torch.tanh(mean_actions)

        output = self.actor(obs)
        if isinstance(output, (tuple, list)):
            if not output:
                raise RuntimeError("Actor forward returned an empty tuple/list.")
            first = output[0]
            if not isinstance(first, torch.Tensor):
                raise RuntimeError("Actor forward first output is not a tensor.")
            return torch.tanh(first)

        if isinstance(output, torch.Tensor):
            if output.shape[-1] != self.action_dim:
                raise RuntimeError(
                    f"Actor forward returned tensor with unexpected action dim {output.shape[-1]}."
                )
            return output

        raise RuntimeError(f"Unsupported actor output type: {type(output)!r}")


def inspect_model(model: SAC) -> dict:
    actor = model.policy.actor
    observation_shape = tuple(model.observation_space.shape)
    action_shape = tuple(model.action_space.shape)
    vec_normalize = model.get_vec_normalize_env()

    info = {
        "observation_shape": observation_shape,
        "action_shape": action_shape,
        "vec_normalize_present": vec_normalize is not None,
        "actor_class": actor.__class__.__name__,
        "actor_module": actor.__class__.__module__,
        "has_get_action_dist_params": hasattr(actor, "get_action_dist_params"),
    }
    return info


def export_actor(model_path: Path, onnx_path: Path) -> dict:
    model = SAC.load(str(model_path), device="cpu")
    info = inspect_model(model)

    obs_shape = info["observation_shape"]
    action_shape = info["action_shape"]
    if len(obs_shape) != 1:
        raise RuntimeError(f"Unexpected observation shape: {obs_shape}")
    if len(action_shape) != 1:
        raise RuntimeError(f"Unexpected action shape: {action_shape}")

    obs_dim = obs_shape[0]
    action_dim = action_shape[0]
    if obs_dim != 17:
        raise RuntimeError(f"Expected observation dim 17, but got {obs_dim}")
    if action_dim != 2:
        raise RuntimeError(f"Expected action dim 2, but got {action_dim}")

    if info["vec_normalize_present"]:
        raise RuntimeError("VecNormalize wrapper detected. Please freeze normalization before exporting ONNX.")

    wrapper = DeterministicActorWrapper(model.policy.actor, action_dim)
    wrapper.eval()

    dummy_input = torch.randn(1, obs_dim, dtype=torch.float32)

    with torch.no_grad():
        torch_output = wrapper(dummy_input).cpu().numpy()

    torch.onnx.export(
        wrapper,
        dummy_input,
        str(onnx_path),
        input_names=["obs"],
        output_names=["action"],
        dynamic_axes={"obs": {0: "batch"}, "action": {0: "batch"}},
        opset_version=17,
    )

    ort_session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    ort_output = ort_session.run(None, {"obs": dummy_input.cpu().numpy()})[0]
    max_abs_diff = float(np.max(np.abs(torch_output - ort_output)))

    export_info = {
        **info,
        "obs_dim": obs_dim,
        "action_dim": action_dim,
        "onnx_path": str(onnx_path),
        "max_abs_diff": max_abs_diff,
        "export_mode": wrapper.export_mode,
        "opset_version": 17,
    }
    return export_info


def write_metadata(metadata_path: Path, model_path: Path, export_info: dict) -> None:
    metadata = {
        "source_model": str(model_path),
        "observation_dim": export_info["obs_dim"],
        "observation_layout": OBSERVATION_LAYOUT,
        "action_dim": export_info["action_dim"],
        "action_meaning": ["forward", "right"],
        "deterministic": True,
        "vec_normalize_present": export_info["vec_normalize_present"],
        "actor_class": export_info["actor_class"],
        "actor_module": export_info["actor_module"],
        "export_mode": export_info["export_mode"],
        "opset_version": export_info["opset_version"],
        "max_abs_diff": export_info["max_abs_diff"],
        "export_time": datetime.now().isoformat(timespec="seconds"),
    }

    with open(metadata_path, "w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=2, ensure_ascii=False)


def write_validation_log(log_path: Path, model_path: Path, export_info: dict) -> None:
    lines = [
        "Navigation ONNX export validation",
        f"timestamp: {datetime.now().isoformat(timespec='seconds')}",
        f"model_path: {model_path}",
        f"observation_shape: {export_info['observation_shape']}",
        f"action_shape: {export_info['action_shape']}",
        f"vec_normalize_present: {export_info['vec_normalize_present']}",
        f"actor_class: {export_info['actor_class']}",
        f"actor_module: {export_info['actor_module']}",
        f"export_mode: {export_info['export_mode']}",
        f"onnx_path: {export_info['onnx_path']}",
        f"max_abs_diff: {export_info['max_abs_diff']:.6e}",
        "validation: PASS" if export_info["max_abs_diff"] < 1e-4 else "validation: FAIL",
    ]
    log_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export NavigationCube SAC actor to ONNX.")
    parser.add_argument(
        "--model-path",
        type=Path,
        default=None,
        help="Path to the .zip SAC model file.",
    )
    parser.add_argument(
        "--onnx-path",
        type=Path,
        default=None,
        help="Output path for ONNX file (auto-generated if not provided).",
    )
    parser.add_argument(
        "--metadata-path",
        type=Path,
        default=None,
        help="Output path for metadata JSON (auto-generated if not provided).",
    )
    parser.add_argument(
        "--log-path",
        type=Path,
        default=None,
        help="Output path for validation log (auto-generated if not provided).",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    model_path = args.model_path
    if model_path is None:
        user_input = input("请输入 SAC 模型文件路径（.zip）: ").strip()
        if not user_input:
            raise RuntimeError("未输入模型路径，程序终止。")
        model_path = Path(user_input)

    if not model_path.exists():
        raise FileNotFoundError(f"模型文件不存在: {model_path}")

    model_stem = model_path.stem
    base_dir = model_path.parent
    onnx_path = args.onnx_path or (base_dir / f"{model_stem}_actor.onnx")
    metadata_path = args.metadata_path or (base_dir / f"{model_stem}_actor_onnx_meta.json")
    log_path = args.log_path or (base_dir / f"{model_stem}_export_validation.log")

    for output_path in (onnx_path, metadata_path, log_path):
        output_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"📦 加载模型: {model_path}")
    export_info = export_actor(model_path, onnx_path)

    if export_info["max_abs_diff"] >= 1e-4:
        raise RuntimeError(
            f"ONNX 验证失败: max_abs_diff={export_info['max_abs_diff']:.6e}"
        )

    write_metadata(metadata_path, model_path, export_info)
    write_validation_log(log_path, model_path, export_info)

    print(f"✅ 导出成功: {onnx_path}")
    print(f"📄 元数据已保存: {metadata_path}")
    print(f"🧪 校验日志已保存: {log_path}")
    print(f"📐 观测维度: {export_info['obs_dim']} | 动作维度: {export_info['action_dim']}")
    print(f"🔍 VecNormalize: {export_info['vec_normalize_present']}")
    print(f"🧠 Actor 接口模式: {export_info['export_mode']}")
    print(f"📏 最大绝对误差: {export_info['max_abs_diff']:.6e}")


if __name__ == "__main__":
    main()

