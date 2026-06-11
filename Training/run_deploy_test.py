import argparse
import json
import os
import signal
import subprocess
import sys
from datetime import datetime
from pathlib import Path

_deploy_process = None
_job_dir = None
_session_id = None
_start_time = None
_ue_process = None


def write_runtime_info(status: str = "running"):
    if _job_dir is None:
        return

    runtime_info = {
        "deploy_pid": os.getpid(),
        "ue_pid": _ue_process.pid if _ue_process is not None else None,
        "connection_mode": "standalone",
        "status": status,
        "session_id": _session_id,
        "updated_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "started_at": _start_time.strftime("%Y-%m-%d %H:%M:%S") if _start_time else None,
    }
    runtime_info_path = _job_dir / "runtime_process.json"
    with open(runtime_info_path, "w", encoding="utf-8") as f:
        json.dump(runtime_info, f, indent=2, ensure_ascii=False)


def terminate_ue_process():
    global _ue_process
    if _ue_process is None:
        return

    _ue_process.terminate()
    try:
        _ue_process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        _ue_process.kill()
    _ue_process = None


def signal_handler(sig, frame):
    print("\n收到停止信号，正在关闭部署测试进程...")
    try:
        write_runtime_info("stopping")
        terminate_ue_process()
        write_runtime_info("stopped")
    except Exception as exc:
        print(f"关闭部署测试进程失败: {exc}")
    sys.exit(0)


def main():
    global _deploy_process, _job_dir, _session_id, _start_time, _ue_process

    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, help="部署配置 JSON 文件路径")
    args = parser.parse_args()

    with open(args.config, "r", encoding="utf-8") as f:
        config = json.load(f)

    _session_id = config.get("session_id", "deploy_001")
    _job_dir = Path(config.get("job_dir", "."))
    _job_dir.mkdir(parents=True, exist_ok=True)
    _start_time = datetime.now()

    ue5_exe = config.get("ue5_exe")
    ue5_project = config.get("ue5_project")
    map_path = config.get("map_path", "/Game/Maps/Default")
    headless_mode = config.get("headless_mode", False)
    display_logs = config.get("display_logs", True)
    deploy_config_path = Path(args.config).resolve()

    if not ue5_exe:
        raise RuntimeError("缺少 ue5_exe 配置")
    if not ue5_project:
        raise RuntimeError("缺少 ue5_project 配置")

    cmd_args = [
        ue5_exe,
        ue5_project,
        "-game",
        "-UNATTENDED",
    ]
    if headless_mode:
        cmd_args.append("-nullRHI")
    else:
        cmd_args.append("-WINDOWED")
    cmd_args.append(map_path)
    if display_logs:
        cmd_args.append("-LOG")
    cmd_args.append(f"-DeployConfig={deploy_config_path}")

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    effective_config_path = _job_dir / "effective_deploy_config.json"
    with open(effective_config_path, "w", encoding="utf-8") as f:
        json.dump(config, f, indent=2, ensure_ascii=False)

    print(f"启动部署测试: {' '.join(cmd_args)}")
    _ue_process = subprocess.Popen(cmd_args)
    write_runtime_info("running")

    return_code = _ue_process.wait()
    write_runtime_info("stopped")
    print(f"UE5 进程退出，返回码: {return_code}")


if __name__ == "__main__":
    main()
