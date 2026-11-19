"""Nap trong so YOLO tu repo ngoai an toan: torch.load(weights_only=True) chi cho phep cac lop torch/ultralytics da liet ke."""
import torch, collections
import torch.nn as nn
from ultralytics.nn.modules import block, conv, head
from ultralytics.nn.tasks import DetectionModel
SAFE = [set, collections.OrderedDict, nn.SiLU, nn.BatchNorm2d, nn.ModuleList, nn.Sequential, nn.Conv2d, nn.Identity, nn.MaxPool2d, nn.Upsample,
        block.Attention, block.Bottleneck, block.C2PSA, block.C3k, block.C3k2, block.DFL, block.PSABlock, block.SPPF,
        conv.Concat, conv.Conv, conv.DWConv, head.Detect, DetectionModel]
def load(p):
    with torch.serialization.safe_globals(SAFE):
        ck = torch.load(p, map_location='cpu', weights_only=True)
    m = (ck.get('ema') or ck['model']).float().eval()
    return m, m.names
