import onnxruntime
import cv2
import torch
from torchvision import transforms
import torch.nn.functional as F
import numpy as np
from PIL import Image
import time

# 加载 ONNX 模型
ort_session = onnxruntime.InferenceSession('mobile10.onnx')

# 定义图像预处理步骤
test_transform = transforms.Compose([
    transforms.Resize(224),
    transforms.CenterCrop(224),
    transforms.ToTensor(),
    transforms.Normalize(
        mean=[0.0979, 0.0979, 0.0979],
        std=[0.1986, 0.1986, 0.1986]
    )
])

# 加载类别标签字典
loaded_idx_to_labels = np.load('idx_to_labels.npy', allow_pickle=True)
idx_to_labels = dict(loaded_idx_to_labels)

def process_image(image_path):
    # 记录该帧开始处理的时间
    start_time = time.time()

    # 读取图像并转换为 RGB 格式
    img = Image.open(image_path).convert('RGB')

    # 预处理图像
    input_img = test_transform(img)  # 预处理
    input_tensor = input_img.unsqueeze(0).numpy()

    # onnx runtime 预测
    ort_inputs = {'input': input_tensor}  # onnx runtime 输入
    pred_logits = ort_session.run(['output'], ort_inputs)[0]  # onnx runtime 输出
    pred_logits = torch.tensor(pred_logits)
    pred_softmax = F.softmax(pred_logits, dim=1)  # 对 logit 分数做 softmax 运算

    # 解析 top3 预测结果的类别和置信度
    n = 3
    top_n = torch.topk(pred_softmax, n)  # 取置信度最大的 n 个结果
    pred_ids = top_n[1].cpu().detach().numpy().squeeze()  # 解析出类别
    confs = top_n[0].cpu().detach().numpy().squeeze()  # 解析出置信度

    # 打印 top-n 分类结果及置信度
    for i in range(n):
        pred_class = idx_to_labels.get(pred_ids[i], "Unknown")  # 获取类别名称
        confidence = confs[i] * 100                             # 获取置信度
        print(f"Class: {pred_class}, Confidence: {confidence:.2f}%")

    # 记录该帧处理完毕的时间
    end_time = time.time()
    # 计算每秒处理图像帧数FPS
    FPS = 1 / (end_time - start_time)
    print(f"FPS: {FPS:.2f}")

# 调用函数处理单张图像
process_image('pic.png')
