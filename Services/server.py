import json
import subprocess
import threading
from pathlib import Path
from datetime import datetime
from tkinter import Tk, filedialog
from fastapi import Body, FastAPI, Form, HTTPException, Query, File, UploadFile
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware
import uvicorn

app = FastAPI()
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"]
)

# 配置路径
BASE_DIR = Path(__file__).parent
PROJECT_ROOT = BASE_DIR.parent
UE5_EXE = "D:/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor.exe"
CONDA_ENV = "UEDemo02"
TRAIN_SCRIPT = str(PROJECT_ROOT / "Training" / "train_headless.py")
DEPLOY_SCRIPT = str(PROJECT_ROOT / "Training" / "run_deploy_test.py")
OUTPUT_DIR = BASE_DIR / "training_jobs"
OUTPUT_DIR.mkdir(exist_ok=True)
DEPLOY_OUTPUT_DIR = BASE_DIR / "deploy_jobs"
DEPLOY_OUTPUT_DIR.mkdir(exist_ok=True)
TRAINING_ROOT = PROJECT_ROOT / "Training"
UE_PROJECT = "D:/uePro/OrangeRobot/OrangeRobot.uproject"
EXPORT_SCRIPT_MAP = {
    "orangerobot": PROJECT_ROOT / "Training" / "export_orangerobot_onnx.py",
    "navigation": PROJECT_ROOT / "Training" / "export_navigation_onnx.py",
}

# 可用的关卡列表
LEVELS = {
    "orangerobot": [
        {
            "id": "orangerobot_train",
            "name": "基础训练场",
            "path": "/Game/地图/OrangeRobotTrain",
            "thumbnail": "/maps/orangerobot_train.jpg",
            "description": "基础训练场地"
        },
    ],
    "navigation": [
        {
            "id": "nav_basic",
            "name": "导航基础场景",
            "path": "/Game/地图/NavigationTrain",
            "thumbnail": "/maps/nav_basic.jpg",
            "description": "简单的导航训练场景"
        },
        {
            "id": "nav_complex",
            "name": "导航复杂场景",
            "path": "/Game/地图/NavigationTrain2",
            "thumbnail": "/maps/nav_complex.jpg",
            "description": "复杂的导航训练场景"
        },
        {
            "id": "nav_advanced",
            "name": "导航高级场景",
            "path": "/Game/地图/NavigationTrain1",
            "thumbnail": "/maps/nav_advanced.jpg",
            "description": "高级的导航训练场景"
        }
    ]
}

DEPLOY_LEVELS = [
    {
        "id": "deploy_basic",
        "name": "部署测试示例场景",
        "path": "/Game/地图/Main1",
        "thumbnail": "/maps/main1.jpg",
        "description": "用于桥接机器人部署测试的直线关卡"
    },
    {
        "id": "deploy_complex",
        "name": "部署测试场景",
        "path": "/Game/地图/Main",
        "thumbnail": "/maps/main.jpg",
        "description": "用于桥接机器人部署测试的复杂关卡"
    }
]

# 进程管理
running_jobs = {}
running_deploy_sessions = {}
tensorboard_processes = {}


def read_runtime_info(job_dir: Path):
    runtime_info_path = job_dir / "runtime_process.json"
    if not runtime_info_path.exists():
        return {}
    try:
        with open(runtime_info_path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def terminate_pid(pid: int | None):
    if not pid:
        return False
    try:
        subprocess.run([
            "taskkill", "/PID", str(pid), "/T", "/F"
        ], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True
    except Exception:
        return False


def append_job_log(log_path: Path, message: str):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with open(log_path, "a", encoding="utf-8") as f:
        f.write(f"[{timestamp}] {message}\n")


def stream_subprocess_output(stream, log_path: Path, prefix: str):
    try:
        for line in iter(stream.readline, ""):
            if not line:
                break
            append_job_log(log_path, f"[{prefix}] {line.rstrip()}")
    finally:
        stream.close()


def watch_training_process(job_id: str, process: subprocess.Popen, log_path: Path):
    return_code = process.wait()
    append_job_log(log_path, f"训练进程退出 | returncode={return_code}")
    job = running_jobs.get(job_id)
    if job is not None:
        job["returncode"] = return_code
        job["ended_at"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def open_file_dialog_for_directory() -> str:
    root = Tk()
    root.withdraw()
    root.attributes("-topmost", True)
    try:
        selected_path = filedialog.askdirectory(
            title="选择 TensorBoard 日志目录",
        )
        return selected_path or ""
    finally:
        root.destroy()


def open_file_dialog_for_model() -> str:
    root = Tk()
    root.withdraw()
    root.attributes("-topmost", True)
    try:
        selected_path = filedialog.askopenfilename(
            title="选择导出模型文件",
            filetypes=[("ZIP Model", "*.zip"), ("All Files", "*.*")],
        )
        return selected_path or ""
    finally:
        root.destroy()


def open_file_dialog_for_onnx() -> str:
    root = Tk()
    root.withdraw()
    root.attributes("-topmost", True)
    try:
        selected_path = filedialog.askopenfilename(
            title="选择 ONNX 模型文件",
            filetypes=[("ONNX Model", "*.onnx"), ("All Files", "*.*")],
        )
        return selected_path or ""
    finally:
        root.destroy()


def build_default_onnx_path(export_type: str, model_path: str | None) -> str:
    if model_path:
        source = Path(model_path)
        if source.suffix.lower() == ".zip":
            return str(source.with_name(f"{source.stem}_actor.onnx"))
    default_name = "navigation_actor.onnx" if export_type == "navigation" else "orangerobot_actor.onnx"
    return str((PROJECT_ROOT / "Exports" / default_name).resolve())


def open_save_dialog_for_onnx(export_type: str, model_path: str | None) -> str:
    root = Tk()
    root.withdraw()
    root.attributes("-topmost", True)
    try:
        initial_path = build_default_onnx_path(export_type, model_path)
        selected_path = filedialog.asksaveasfilename(
            title="选择 ONNX 保存位置",
            defaultextension=".onnx",
            initialfile=Path(initial_path).name,
            initialdir=str(Path(initial_path).parent),
            filetypes=[("ONNX Model", "*.onnx")],
        )
        return selected_path or ""
    finally:
        root.destroy()


def validate_export_type(export_type: str) -> None:
    if export_type not in EXPORT_SCRIPT_MAP:
        raise HTTPException(400, f"不支持的导出类型: {export_type}")


def derive_export_sidecar_paths(onnx_path: Path) -> tuple[Path, Path]:
    metadata_path = onnx_path.with_name(f"{onnx_path.stem}_onnx_meta.json")
    log_path = onnx_path.with_name(f"{onnx_path.stem}_export_validation.log")
    return metadata_path, log_path


def normalize_existing_file(raw_path: str, suffix: str, label: str) -> Path:
    cleaned = str(raw_path or "").strip().strip('"')
    if not cleaned:
        raise HTTPException(400, f"缺少{label}路径")

    candidate = Path(cleaned).expanduser()
    if not candidate.is_absolute():
        candidate = (PROJECT_ROOT / candidate).resolve()

    if not candidate.exists() or not candidate.is_file():
        raise HTTPException(400, f"{label}不存在: {candidate}")
    if candidate.suffix.lower() != suffix.lower():
        raise HTTPException(400, f"{label}必须是 {suffix} 文件")
    return candidate.resolve()


def resolve_map_by_name(maps: list[dict], level_name: str) -> dict | None:
    for item in maps:
        if item["name"] == level_name:
            return item
    return None


def collect_all_training_maps() -> list[dict]:
    all_maps = []
    for items in LEVELS.values():
        all_maps.extend(items)
    return all_maps


def _resolve_tensorboard_log_dir(job_id: str) -> Path | None:
    """根据 job_id 查找实际的 TensorBoard 日志目录"""
    # 1) 正在运行的 job —— 从 config 中读取 output_dir
    job = running_jobs.get(job_id)
    if job and job.get("config", {}).get("output_dir"):
        candidate = Path(job["config"]["output_dir"]) / "tensorboard"
        if candidate.exists():
            return candidate

    # 2) 历史 job —— 遍历已知的训练类型子目录
    for sub in ("Robot", "Navigation"):
        candidate = TRAINING_ROOT / sub / job_id / "tensorboard"
        if candidate.exists():
            return candidate

    # 3) 兜底：检查 job_dir（server 的 training_jobs 目录，按类型分目录）
    for sub in ("Robot", "Navigation"):
        candidate = OUTPUT_DIR / sub / job_id / "tensorboard"
        if candidate.exists():
            return candidate
    return None


@app.get("/", response_class=HTMLResponse)
async def home():
    html_path = BASE_DIR / "frontend.html"
    if html_path.exists():
        with open(html_path, "r", encoding="utf-8") as f:
            return f.read()
    return "<h1>OrangeRobot Training Server</h1><p>API is running</p>"


@app.get("/api/maps")
async def get_maps(type: str = Query(..., description="Training type: orangerobot or navigation")):
    if type not in LEVELS:
        raise HTTPException(400, f"未知的训练类型: {type}")
    return LEVELS[type]


@app.get("/api/deploy/maps")
async def get_deploy_maps():
    return DEPLOY_LEVELS


@app.post("/api/dialog/select-directory")
async def select_directory():
    selected_path = open_file_dialog_for_directory()
    return {"path": selected_path}


@app.post("/api/dialog/select-model-file")
async def select_model_file(payload: dict = Body(...)):
    export_type = str(payload.get("export_type", "")).strip()
    validate_export_type(export_type)
    selected_path = open_file_dialog_for_model()
    return {"path": selected_path}


@app.post("/api/dialog/select-onnx-file")
async def select_onnx_file():
    selected_path = open_file_dialog_for_onnx()
    return {"path": selected_path}


@app.post("/api/dialog/select-export-path")
async def select_export_path(payload: dict = Body(...)):
    export_type = str(payload.get("export_type", "")).strip()
    model_path = str(payload.get("model_path", "")).strip() or None
    validate_export_type(export_type)
    selected_path = open_save_dialog_for_onnx(export_type, model_path)
    return {"path": selected_path}


@app.post("/api/export_onnx")
async def export_onnx(payload: dict = Body(...)):
    export_type = str(payload.get("export_type", "")).strip()
    model_path_raw = str(payload.get("model_path", "")).strip().strip('"')
    onnx_path_raw = str(payload.get("onnx_path", "")).strip().strip('"')

    validate_export_type(export_type)

    if not model_path_raw:
        raise HTTPException(400, "缺少模型路径")
    if not onnx_path_raw:
        raise HTTPException(400, "缺少 ONNX 保存路径")

    model_path = Path(model_path_raw)
    onnx_path = Path(onnx_path_raw)

    if not model_path.exists() or not model_path.is_file():
        raise HTTPException(400, f"模型文件不存在: {model_path}")
    if model_path.suffix.lower() != ".zip":
        raise HTTPException(400, "模型文件必须是 .zip")
    if onnx_path.suffix.lower() != ".onnx":
        raise HTTPException(400, "导出文件必须是 .onnx")

    onnx_path.parent.mkdir(parents=True, exist_ok=True)
    metadata_path, log_path = derive_export_sidecar_paths(onnx_path)

    export_script = EXPORT_SCRIPT_MAP[export_type]
    if not export_script.exists():
        raise HTTPException(500, f"导出脚本不存在: {export_script}")

    cmd = [
        "conda", "run", "-n", CONDA_ENV,
        "python", str(export_script),
        "--model-path", str(model_path),
        "--onnx-path", str(onnx_path),
        "--metadata-path", str(metadata_path),
        "--log-path", str(log_path),
    ]

    result = subprocess.run(
        cmd,
        cwd=str(export_script.parent),
        capture_output=True,
        text=True,
        encoding="utf-8",
    )

    if result.returncode != 0:
        raise HTTPException(
            500,
            {
                "message": "导出 ONNX 失败",
                "stdout": result.stdout,
                "stderr": result.stderr,
            },
        )

    return {
        "success": True,
        "export_type": export_type,
        "model_path": str(model_path),
        "onnx_path": str(onnx_path),
        "metadata_path": str(metadata_path),
        "log_path": str(log_path),
        "stdout": result.stdout,
        "stderr": result.stderr,
        "message": "导出成功",
    }


@app.post("/api/start_training")
async def start_training(
        level: str = Form(...),
        training_type: str = Form(...),
        total_timesteps: int = Form(1500000),
        learning_rate: float = Form(0.0001),
        batch_size: int = Form(1024),
        use_gpu: bool = Form(True),
        connection_mode: str = Form("standalone"),
        training_mode: str = Form("new"),
        resume_model_path: str = Form(""),
        GlobalTrainingStep: int = Form(0),
        resume_model_file: UploadFile | None = File(None),
        MaxSteps: int = Form(2000),
        SimulationFrequencyHz: float = Form(30.0),
        FallTiltThreshold: float = Form(60.0),
        BodyHeightThreshold: float = Form(45.0),
        HeadGroundHeightThreshold: float = Form(35.0),
        FallPenaltyHorizon: int = Form(50),
        BodyHeightRewardMax: float = Form(60.0),
        FootSupportTraceDistance: float = Form(12.0),
        FootStableSpeedThreshold: float = Form(35.0),
        bEnableCommandReward: bool = Form(True),
        bEnableHighLevelCommand: bool = Form(True),
        MaxForwardSpeed: float = Form(200.0),
        MaxTurnSpeedDegPerSec: float = Form(90.0),
        bEnableCurriculum: bool = Form(True),
        CurriculumStageBoundaries: str = Form("150000,300000,600000,1000000"),
        AliveReward: float = Form(0.1),
        HeightRewardScale: float = Form(0.08),
        LateralVelocityPenaltyScale: float = Form(0.02),
        StableDoubleSupportRewardScale: float = Form(0.15),
        TrunkTiltPenaltyScale: float = Form(0.05),
        CommandRewardScale: float = Form(1.0),
        CommandMatchBaseReward: float = Form(0.5),
        ForwardCommandRewardWeight: float = Form(0.7),
        TurnCommandRewardWeight: float = Form(0.3),
        ForwardCommandMatchSigmaMin: float = Form(25.0),
        TurnCommandMatchSigmaMin: float = Form(15.0),
        StandCommandSigma: float = Form(15.0),
        DynamicBalanceRewardWeight: float = Form(1.0),
        DualFootShufflePenaltyScale: float = Form(0.01),
        SwingFootMinHeight: float = Form(8.0),
        SwingFootHeightPenaltyScale: float = Form(0.01),
        SingleSupportBonusReward: float = Form(0.12),
        StepAlternationRewardScale: float = Form(0.10),
        SameLegDominancePenaltyScale: float = Form(0.05),
        GaitSymmetryPenaltyScale: float = Form(0.02),
        StepFrequencyRewardScale: float = Form(0.10),
        MinStepFrequencyHz: float = Form(1.0),
        MaxStepFrequencyHz: float = Form(3.0),
        FootImpactPenaltyScale: float = Form(0.01),
        FootImpactVelocityThreshold: float = Form(50.0),
        ActionSmoothPenaltyScale: float = Form(0.008),
        ActionMagnitudePenaltyScale: float = Form(0.005),
        EnergySigma: float = Form(0.5),
        CostOfTransportScale: float = Form(0.003),
        ForwardSpeedRewardMax: float = Form(150.0),
        TrunkSupportOffsetPenaltyScale: float = Form(0.003),
        TrunkSupportOffsetNormalizeDistance: float = Form(25.0),
        TrunkAngVelXYPenaltyScale: float = Form(0.00002),
        TrunkVerticalVelocityPenaltyScale: float = Form(0.00001),
        TrunkVerticalVelocityDeadzone: float = Form(5.0),
        HeightDropPenaltyScale: float = Form(0.5),
        ActionDeadzone: float = Form(0.05),
        ActionResponseExponent: float = Form(2.0),
        JointVelocityScale: float = Form(90.0),
        TwistVelocityLimit: float = Form(60.0),
        SwingVelocityLimit: float = Form(60.0),
        TrunkHeightNormalization: float = Form(100.0),
        DesiredStepPeriod: float = Form(0.5),
        RewardMaskCore: int = Form(-1),
        RewardMaskGait: int = Form(-1),
        RewardMaskReg: int = Form(-1),
        bLogRewardBreakdown: bool = Form(True),
        bApplyRandomJointOffsetsOnReset: bool = Form(False),
):
    map_info = resolve_map_by_name(collect_all_training_maps(), level)
    if not map_info:
        raise HTTPException(400, f"未找到地图: {level}")
    map_path = map_info["path"]

    if training_mode not in {"new", "resume"}:
        raise HTTPException(400, f"不支持的训练模式: {training_mode}")

    job_id = datetime.now().strftime("training_%Y%m%d_%H%M%S")

    if training_type == "orangerobot":
        type_subdir = "Robot"
    elif training_type == "navigation":
        type_subdir = "Navigation"
    else:
        raise HTTPException(400, f"不支持的训练类型: {training_type}")

    job_dir = OUTPUT_DIR / type_subdir / job_id
    job_dir.mkdir(parents=True, exist_ok=True)

    final_output_dir = TRAINING_ROOT / type_subdir / job_id
    final_output_dir.mkdir(parents=True, exist_ok=True)

    resolved_resume_model_path = None
    if training_mode == "resume":
        if GlobalTrainingStep < 0:
            raise HTTPException(400, "GlobalTrainingStep 必须大于等于 0")
        if resume_model_file is not None:
            original_name = Path(resume_model_file.filename or "")
            if original_name.suffix.lower() != ".zip":
                raise HTTPException(400, "继续训练模型文件必须是 .zip")
            upload_dir = job_dir / "resume_model"
            upload_dir.mkdir(parents=True, exist_ok=True)
            uploaded_path = upload_dir / original_name.name
            with open(uploaded_path, "wb") as f:
                f.write(await resume_model_file.read())
            resolved_resume_model_path = uploaded_path.resolve()
        elif resume_model_path.strip():
            candidate = Path(resume_model_path.strip().strip('"')).expanduser()
            if not candidate.is_absolute():
                candidate = (PROJECT_ROOT / candidate).resolve()
            resolved_resume_model_path = candidate
        else:
            raise HTTPException(400, "继续训练必须提供模型路径或上传模型文件")
        if not resolved_resume_model_path.exists() or not resolved_resume_model_path.is_file():
            raise HTTPException(400, f"继续训练模型不存在: {resolved_resume_model_path}")
        if resolved_resume_model_path.suffix.lower() != ".zip":
            raise HTTPException(400, "继续训练模型必须是 .zip 文件")

    curriculum_stage_boundaries = [
        int(item.strip())
        for item in CurriculumStageBoundaries.split(",")
        if item.strip()
    ]

    env_config = {
        "MaxSteps": MaxSteps,
        "SimulationFrequencyHz": SimulationFrequencyHz,
        "FallTiltThreshold": FallTiltThreshold,
        "BodyHeightThreshold": BodyHeightThreshold,
        "HeadGroundHeightThreshold": HeadGroundHeightThreshold,
        "FallPenaltyHorizon": FallPenaltyHorizon,
        "BodyHeightRewardMax": BodyHeightRewardMax,
        "FootSupportTraceDistance": FootSupportTraceDistance,
        "FootStableSpeedThreshold": FootStableSpeedThreshold,
        "bEnableCommandReward": bEnableCommandReward,
        "bEnableHighLevelCommand": bEnableHighLevelCommand,
        "MaxForwardSpeed": MaxForwardSpeed,
        "MaxTurnSpeedDegPerSec": MaxTurnSpeedDegPerSec,
        "bEnableCurriculum": bEnableCurriculum,
        "GlobalTrainingStep": GlobalTrainingStep,
        "CurriculumStageBoundaries": curriculum_stage_boundaries,
        "AliveReward": AliveReward,
        "HeightRewardScale": HeightRewardScale,
        "LateralVelocityPenaltyScale": LateralVelocityPenaltyScale,
        "StableDoubleSupportRewardScale": StableDoubleSupportRewardScale,
        "TrunkTiltPenaltyScale": TrunkTiltPenaltyScale,
        "CommandRewardScale": CommandRewardScale,
        "CommandMatchBaseReward": CommandMatchBaseReward,
        "ForwardCommandRewardWeight": ForwardCommandRewardWeight,
        "TurnCommandRewardWeight": TurnCommandRewardWeight,
        "ForwardCommandMatchSigmaMin": ForwardCommandMatchSigmaMin,
        "TurnCommandMatchSigmaMin": TurnCommandMatchSigmaMin,
        "StandCommandSigma": StandCommandSigma,
        "DualFootShufflePenaltyScale": DualFootShufflePenaltyScale,
        "SwingFootMinHeight": SwingFootMinHeight,
        "SwingFootHeightPenaltyScale": SwingFootHeightPenaltyScale,
        "SingleSupportBonusReward": SingleSupportBonusReward,
        "StepAlternationRewardScale": StepAlternationRewardScale,
        "SameLegDominancePenaltyScale": SameLegDominancePenaltyScale,
        "GaitSymmetryPenaltyScale": GaitSymmetryPenaltyScale,
        "StepFrequencyRewardScale": StepFrequencyRewardScale,
        "MinStepFrequencyHz": MinStepFrequencyHz,
        "MaxStepFrequencyHz": MaxStepFrequencyHz,
        "FootImpactPenaltyScale": FootImpactPenaltyScale,
        "FootImpactVelocityThreshold": FootImpactVelocityThreshold,
        "ActionSmoothPenaltyScale": ActionSmoothPenaltyScale,
        "ActionMagnitudePenaltyScale": ActionMagnitudePenaltyScale,
        "EnergySigma": EnergySigma,
        "CostOfTransportScale": CostOfTransportScale,
        "ForwardSpeedRewardMax": ForwardSpeedRewardMax,
        "TrunkSupportOffsetPenaltyScale": TrunkSupportOffsetPenaltyScale,
        "TrunkSupportOffsetNormalizeDistance": TrunkSupportOffsetNormalizeDistance,
        "TrunkAngVelXYPenaltyScale": TrunkAngVelXYPenaltyScale,
        "TrunkVerticalVelocityPenaltyScale": TrunkVerticalVelocityPenaltyScale,
        "TrunkVerticalVelocityDeadzone": TrunkVerticalVelocityDeadzone,
        "HeightDropPenaltyScale": HeightDropPenaltyScale,
        "ActionDeadzone": ActionDeadzone,
        "ActionResponseExponent": ActionResponseExponent,
        "JointVelocityScale": JointVelocityScale,
        "TwistVelocityLimit": TwistVelocityLimit,
        "SwingVelocityLimit": SwingVelocityLimit,
        "TrunkHeightNormalization": TrunkHeightNormalization,
        "DesiredStepPeriod": DesiredStepPeriod,
        "DynamicBalanceRewardWeight": DynamicBalanceRewardWeight,
        "RewardMaskCore": RewardMaskCore,
        "RewardMaskGait": RewardMaskGait,
        "RewardMaskReg": RewardMaskReg,
        "bLogRewardBreakdown": bLogRewardBreakdown,
        "bApplyRandomJointOffsetsOnReset": bApplyRandomJointOffsetsOnReset,
    }

    env_config_path = job_dir / "env_config.json"
    with open(env_config_path, "w") as f:
        json.dump(env_config, f, indent=2)

    config = {
        "training_id": job_id,
        "training_type": training_type,
        "training_mode": training_mode,
        "resume_model_path": str(resolved_resume_model_path) if resolved_resume_model_path else None,
        "map_path": map_path,
        "total_timesteps": total_timesteps,
        "learning_rate": learning_rate,
        "batch_size": batch_size,
        "device": "cuda" if use_gpu else "cpu",
        "connection_mode": connection_mode,
        "ue5_exe": UE5_EXE,
        "ue5_project": UE_PROJECT,
        "port": 50051,
        "output_dir": str(final_output_dir),
        "job_dir": str(job_dir),
        "env_config_path": str(env_config_path),
    }

    config_path = job_dir / "config.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    cmd = [
        "conda", "run", "-n", CONDA_ENV,
        "python", TRAIN_SCRIPT,
        "--config", str(config_path)
    ]
    train_log_path = job_dir / "train_runtime.log"
    append_job_log(train_log_path, f"启动训练进程 | job_id={job_id}")
    append_job_log(train_log_path, f"命令: {' '.join(cmd)}")

    try:
        process = subprocess.Popen(
            cmd,
            cwd=Path(TRAIN_SCRIPT).parent,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        running_jobs[job_id] = {
            "process": process,
            "dir": job_dir,
            "config": config,
            "training_pid": process.pid,
            "ue_pid": None,
            "connection_mode": connection_mode,
            "log_path": str(train_log_path),
            "returncode": None,
            "started_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        }
        append_job_log(train_log_path, f"训练进程已启动 | pid={process.pid}")

        threading.Thread(
            target=stream_subprocess_output,
            args=(process.stdout, train_log_path, "stdout"),
            daemon=True,
        ).start()
        threading.Thread(
            target=stream_subprocess_output,
            args=(process.stderr, train_log_path, "stderr"),
            daemon=True,
        ).start()
        threading.Thread(
            target=watch_training_process,
            args=(job_id, process, train_log_path),
            daemon=True,
        ).start()

        return JSONResponse({
            "job_id": job_id,
            "message": f"训练已在后台启动",
            "config_path": str(config_path),
            "tensorboard_log": str(final_output_dir / "tensorboard"),
            "train_log": str(train_log_path),
        })
    except Exception as e:
        raise HTTPException(500, f"启动训练失败: {str(e)}")


@app.post("/api/start_deploy_test")
async def start_deploy_test(payload: dict = Body(...)):
    map_name = str(payload.get("map_name", "")).strip()
    high_level_onnx_path = normalize_existing_file(payload.get("high_level_onnx_path", ""), ".onnx", "高层导航模型")
    low_level_onnx_path = normalize_existing_file(payload.get("low_level_onnx_path", ""), ".onnx", "低层步态模型")

    map_info = resolve_map_by_name(DEPLOY_LEVELS, map_name)
    if not map_info:
        raise HTTPException(400, f"未找到部署地图: {map_name}")

    for session_id, session in list(running_deploy_sessions.items()):
        process = session.get("process")
        if process and process.poll() is None:
            raise HTTPException(409, f"已有部署测试正在运行: {session_id}")
        if process and process.poll() is not None:
            running_deploy_sessions.pop(session_id, None)

    session_id = datetime.now().strftime("deploy_%Y%m%d_%H%M%S")
    session_dir = DEPLOY_OUTPUT_DIR / session_id
    session_dir.mkdir(parents=True, exist_ok=True)

    deploy_config = {
        "session_id": session_id,
        "map_name": map_info["name"],
        "map_path": map_info["path"],
        "high_level_onnx_path": str(high_level_onnx_path),
        "low_level_onnx_path": str(low_level_onnx_path),
        "display_logs": bool(payload.get("display_logs", True)),
        "headless_mode": bool(payload.get("headless_mode", False)),
        "target_actor_name": str(payload.get("target_actor_name", "")).strip(),
        "ue5_exe": UE5_EXE,
        "ue5_project": UE_PROJECT,
        "job_dir": str(session_dir),
    }

    config_path = session_dir / "deploy_config.json"
    with open(config_path, "w", encoding="utf-8") as f:
        json.dump(deploy_config, f, indent=2, ensure_ascii=False)

    cmd = [
        "conda", "run", "-n", CONDA_ENV,
        "python", DEPLOY_SCRIPT,
        "--config", str(config_path),
    ]

    try:
        process = subprocess.Popen(
            cmd,
            cwd=Path(DEPLOY_SCRIPT).parent,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        running_deploy_sessions[session_id] = {
            "process": process,
            "dir": session_dir,
            "config": deploy_config,
            "deploy_pid": process.pid,
        }
        return {
            "session_id": session_id,
            "message": "部署测试已在后台启动",
            "config_path": str(config_path),
        }
    except Exception as e:
        raise HTTPException(500, f"启动部署测试失败: {str(e)}")


@app.post("/api/stop_deploy_test")
async def stop_deploy_test(payload: dict = Body(...)):
    session_id = str(payload.get("session_id", "")).strip()
    if not session_id:
        raise HTTPException(400, "缺少 session_id")

    session = running_deploy_sessions.pop(session_id, None)
    session_dir = DEPLOY_OUTPUT_DIR / session_id
    if not session and not session_dir.exists():
        raise HTTPException(404, "部署测试任务未找到")

    process = session["process"] if session else None
    runtime_info = read_runtime_info(session_dir)
    deploy_pid = runtime_info.get("deploy_pid")
    ue_pid = runtime_info.get("ue_pid")

    deploy_stopped = False
    ue_stopped = False

    if process is not None:
        try:
            process.terminate()
            process.wait(timeout=5)
            deploy_stopped = True
        except subprocess.TimeoutExpired:
            process.kill()
            deploy_stopped = True
        except Exception:
            deploy_stopped = False
    elif deploy_pid:
        deploy_stopped = terminate_pid(deploy_pid)

    if ue_pid:
        ue_stopped = terminate_pid(ue_pid)

    return {
        "status": "stopped",
        "session_id": session_id,
        "deploy_stopped": deploy_stopped,
        "ue_stopped": ue_stopped,
    }


@app.get("/api/deploy_test/status")
async def get_deploy_test_status():
    for session_id, session in list(running_deploy_sessions.items()):
        process = session.get("process")
        if process and process.poll() is not None:
            running_deploy_sessions.pop(session_id, None)
            continue
        runtime_info = read_runtime_info(Path(session["dir"]))
        return {
            "status": runtime_info.get("status", "running"),
            "session_id": session_id,
            "config": session.get("config", {}),
            "runtime_info": runtime_info,
        }

    deploy_dirs = sorted(
        [item for item in DEPLOY_OUTPUT_DIR.iterdir() if item.is_dir()],
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    for session_dir in deploy_dirs:
        runtime_info = read_runtime_info(session_dir)
        if runtime_info:
            config = {}
            config_path = session_dir / "deploy_config.json"
            if config_path.exists():
                try:
                    with open(config_path, "r", encoding="utf-8") as f:
                        config = json.load(f)
                except Exception:
                    config = {}
            return {
                "status": runtime_info.get("status", "stopped"),
                "session_id": session_dir.name,
                "config": config,
                "runtime_info": runtime_info,
            }

    return {
        "status": "idle",
        "session_id": None,
        "config": {},
        "runtime_info": {},
    }


@app.post("/api/stop_training")
async def stop_training(job_id: str = Form(...)):
    job = running_jobs.pop(job_id, None)

    # 按类型子目录查找 job_dir（Robot/ 或 Navigation/）
    job_dir = None
    for sub in ("Robot", "Navigation"):
        candidate = OUTPUT_DIR / sub / job_id
        if candidate.exists():
            job_dir = candidate
            break
    if job_dir is None:
        job_dir = OUTPUT_DIR / job_id  # 兜底旧路径

    if not job and not job_dir.exists():
        raise HTTPException(404, "训练任务未找到")

    process = job["process"] if job else None
    connection_mode = (job or {}).get("connection_mode")
    runtime_info = read_runtime_info(job_dir)
    ue_pid = runtime_info.get("ue_pid")
    runtime_connection_mode = runtime_info.get("connection_mode")
    if runtime_connection_mode:
        connection_mode = runtime_connection_mode

    training_stopped = False
    ue_stopped = False

    if process is not None:
        try:
            process.terminate()
            process.wait(timeout=5)
            training_stopped = True
        except subprocess.TimeoutExpired:
            process.kill()
            training_stopped = True
        except Exception:
            training_stopped = False
    elif runtime_info.get("training_pid"):
        training_stopped = terminate_pid(runtime_info.get("training_pid"))

    if connection_mode == "standalone" and ue_pid:
        ue_stopped = terminate_pid(ue_pid)
    elif connection_mode == "editor":
        ue_stopped = False

    return {
        "status": "stopped",
        "job_id": job_id,
        "training_stopped": training_stopped,
        "ue_stopped": ue_stopped,
        "connection_mode": connection_mode,
    }


@app.get("/api/tensorboard/{job_id}")
async def get_tensorboard_url(job_id: str):
    log_dir = _resolve_tensorboard_log_dir(job_id)
    if log_dir is None:
        raise HTTPException(404, "训练任务未找到或 TensorBoard 日志目录不存在")

    if job_id in tensorboard_processes:
        process = tensorboard_processes[job_id]
        if process.poll() is None:
            port = 6006 + len(tensorboard_processes) - 1
            return {"url": f"http://localhost:{port}"}

    port = 6006 + len(tensorboard_processes)
    try:
        process = subprocess.Popen([
            "tensorboard",
            "--logdir", str(log_dir),
            "--port", str(port),
            "--host", "localhost"
        ])
        tensorboard_processes[job_id] = process
        return {"url": f"http://localhost:{port}"}
    except Exception as e:
        raise HTTPException(500, f"启动 TensorBoard 失败: {str(e)}")


@app.post("/api/tensorboard/start")
async def start_tensorboard_by_path(payload: dict = Body(...)):
    log_dir = str(payload.get("log_dir", "")).strip().strip('"')
    if not log_dir:
        raise HTTPException(400, "缺少日志目录路径")

    log_path = Path(log_dir).resolve()
    if not log_path.exists() or not log_path.is_dir():
        raise HTTPException(400, f"日志目录不存在: {log_path}")

    tb_key = str(log_path)

    if tb_key in tensorboard_processes:
        process = tensorboard_processes[tb_key]
        if process.poll() is None:
            keys = list(tensorboard_processes.keys())
            port = 6006 + keys.index(tb_key)
            return {"url": f"http://localhost:{port}"}

    port = 6006 + len(tensorboard_processes)
    try:
        process = subprocess.Popen([
            "tensorboard",
            "--logdir", str(log_path),
            "--port", str(port),
            "--host", "localhost"
        ])
        tensorboard_processes[tb_key] = process
        return {"url": f"http://localhost:{port}"}
    except Exception as e:
        raise HTTPException(500, f"启动 TensorBoard 失败: {str(e)}")


@app.get("/api/jobs")
async def list_jobs():
    jobs = []
    for job_id, job_info in running_jobs.items():
        jobs.append({
            "job_id": job_id,
            "status": "running" if job_info["process"].poll() is None else "stopped",
            "config": job_info.get("config", {})
        })
    return {"jobs": jobs}


if __name__ == "__main__":
    print("🚀 OrangeRobot Training Server")
    print(f"📁 输出目录: {OUTPUT_DIR}")
    print(f"🎮 UE5 路径: {UE5_EXE}")
    print(f"🐍 Conda 环境: {CONDA_ENV}")
    print("=" * 60)
    uvicorn.run(app, host="localhost", port=8000)
