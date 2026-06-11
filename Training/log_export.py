import os
from tbparse import SummaryReader
import pandas as pd

log_dir = input("请输入TensorBoard日志目录路径: ").strip()
output_dir = os.path.join(log_dir, "log")
os.makedirs(output_dir, exist_ok=True)

reader = SummaryReader(log_dir, extra_columns={'dir_name'})
df = reader.scalars

# 你要导出的指标列表
target_tags = [
    "custom/episode_index",
    "custom/episode_length",
    "custom/episode_length_mean_100",
    "custom/episode_reward",
    "custom/episode_reward_mean_100",
    "custom/reward_step",
    "custom/reward_step_mean",
    "time/fps",
    "train/actor_loss",
    "train/critic_loss",
    "train/ent_coef",
    "train/ent_coef_loss",
    "train/ent_coef_value",
    "train/entropy",
    "train/learning_rate",
    "train/q_value_max",
    "train/q_value_mean",
    "train/q_value_min",
    "train/q1_mean",
    "train/q2_mean",
    "train/q1_q2_diff",
]

for tag in target_tags:
    df_tag = df[df['tag'] == tag]
    if not df_tag.empty:
        file_name = tag.replace('/', '_') + '.csv'
        file_path = os.path.join(output_dir, file_name)
        df_tag.to_csv(file_path, index=False)
        print(f"已导出 {file_name}")
    else:
        print(f"[警告] 未找到指标 {tag}，跳过")

print("全部导出完成！")