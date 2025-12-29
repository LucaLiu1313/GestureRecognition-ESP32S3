import argparse
import time
from pathlib import Path

import numpy as np
import onnxruntime
import torch
import torch.nn.functional as F
from PIL import Image
from torchvision import transforms


def build_transform():
    return transforms.Compose(
        [
            transforms.Resize(224),
            transforms.CenterCrop(224),
            transforms.ToTensor(),
            transforms.Normalize(
                mean=[0.0979, 0.0979, 0.0979],
                std=[0.1986, 0.1986, 0.1986],
            ),
        ]
    )


def load_labels(labels_path):
    if labels_path is None:
        return {}
    loaded = np.load(labels_path, allow_pickle=True)
    if isinstance(loaded, np.ndarray) and loaded.shape == ():
        loaded = loaded.item()
    if isinstance(loaded, dict):
        return loaded
    try:
        return dict(loaded)
    except TypeError:
        return {}


def process_image(image_path, ort_session, idx_to_labels, topk):
    start_time = time.perf_counter()

    img = Image.open(image_path).convert("RGB")
    input_img = build_transform()(img)
    input_tensor = input_img.unsqueeze(0).numpy()

    input_name = ort_session.get_inputs()[0].name
    output_name = ort_session.get_outputs()[0].name
    pred_logits = ort_session.run([output_name], {input_name: input_tensor})[0]
    pred_softmax = F.softmax(torch.tensor(pred_logits), dim=1)

    top_n = torch.topk(pred_softmax, topk)
    pred_ids = top_n.indices.cpu().numpy().squeeze()
    confs = top_n.values.cpu().numpy().squeeze()

    if topk == 1:
        pred_ids = [int(pred_ids)]
        confs = [float(confs)]
    else:
        pred_ids = pred_ids.tolist()
        confs = confs.tolist()

    for i in range(len(pred_ids)):
        pred_class = idx_to_labels.get(pred_ids[i], "Unknown")
        confidence = confs[i] * 100
        print(f"Class: {pred_class}, Confidence: {confidence:.2f}%")

    fps = 1 / (time.perf_counter() - start_time)
    print(f"FPS: {fps:.2f}")


def parse_args():
    script_dir = Path(__file__).resolve().parent
    default_model = (script_dir / ".." / "models" / "mobile10.onnx").resolve()
    default_labels = (script_dir / ".." / "models" / "idx_to_labels.npy").resolve()
    default_image = (script_dir / ".." / "pic.png").resolve()

    parser = argparse.ArgumentParser(description="Run ONNX gesture inference.")
    parser.add_argument("--image", default=str(default_image), help="Path to image.")
    parser.add_argument("--model", default=str(default_model), help="Path to ONNX model.")
    parser.add_argument(
        "--labels",
        default=str(default_labels),
        help="Path to idx_to_labels.npy (optional).",
    )
    parser.add_argument("--topk", type=int, default=3, help="Top-k results to show.")
    return parser.parse_args()


def main():
    args = parse_args()
    image_path = Path(args.image)
    model_path = Path(args.model)
    labels_path = Path(args.labels) if args.labels else None

    if not image_path.exists():
        raise SystemExit(f"Image not found: {image_path}")
    if not model_path.exists():
        raise SystemExit(f"Model not found: {model_path}")

    if labels_path is not None and not labels_path.exists():
        labels_path = None

    ort_session = onnxruntime.InferenceSession(str(model_path))
    idx_to_labels = load_labels(str(labels_path)) if labels_path else {}
    process_image(str(image_path), ort_session, idx_to_labels, args.topk)


if __name__ == "__main__":
    main()
