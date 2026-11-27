"""Doi mo hinh openpilot fp16 -> fp32 (giu dau vao/dau ra nhu cu).
Ly do (18/11): ONNX Runtime tren ARM64 chay kernel fp16 thuan (cong don fp16) nen ket qua lech xa ban PC:
file chan doan cung khung: xe truoc 20% (dien thoai) vs 68% (PC), pose 13,8 vs 5,4 m/s -> app 'mu' xe truoc.
python fp32.py vao.onnx ra.onnx
"""
import sys, onnx, numpy as np
from onnx import TensorProto, helper, numpy_helper

src, dst = sys.argv[1], sys.argv[2]
m = onnx.load(src)
g = m.graph
F16, F32 = TensorProto.FLOAT16, TensorProto.FLOAT

for t in g.initializer:
    if t.data_type == F16:
        t.CopyFrom(numpy_helper.from_array(numpy_helper.to_array(t).astype(np.float32), t.name))
for n in g.node:
    for a in n.attribute:
        if a.name == 'to' and a.i == F16: a.i = F32
        if a.type == onnx.AttributeProto.TENSOR and a.t.data_type == F16:
            a.t.CopyFrom(numpy_helper.from_array(numpy_helper.to_array(a.t).astype(np.float32), a.t.name))
for vi in list(g.value_info) + list(g.output):
    if vi.type.tensor_type.elem_type == F16: vi.type.tensor_type.elem_type = F32
# dau vao fp16 (mo hinh policy): giu kieu, them Cast -> fp32 ngay sau
new = []
for i in g.input:
    if i.type.tensor_type.elem_type == F16:
        c = i.name + '_f32'
        for n in g.node:
            for k, x in enumerate(n.input):
                if x == i.name: n.input[k] = c
        new.append(helper.make_node('Cast', [i.name], [c], to=F32, name='cast_' + i.name))
for n in reversed(new): g.node.insert(0, n)
onnx.checker.check_model(m)
onnx.save(m, dst)
print('ok', dst)
