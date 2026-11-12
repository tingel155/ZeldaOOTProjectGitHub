# ZeldaOOTProject map tools for Blender (4.2+).
#
# Imports a mesh exported by the in-game ZOP map editor (.fbx + .zop.json) and rebuilds its materials the way the N64
# draws them, which plain FBX can't describe:
#   - texture wrap per axis: repeat, mirror or clamp (N64 tiles set this separately for S and T)
#   - texture colour multiplied with the vertex colours (OoT bakes the room lighting into them)
#   - alpha: cut-out for opaque meshes with alpha testing, blended for translucent ones
#   - unlit (emission), so it looks like in the game
#
# Install: Edit > Preferences > Add-ons > (arrow menu) Install from Disk... > this file.
# Use:     File > Import > ZOP Map-Mesh (.fbx)

bl_info = {
    "name": "ZOP Map Tools",
    "author": "ZeldaOOTProject",
    "version": (0, 1, 0),
    "blender": (4, 2, 0),
    "location": "File > Import > ZOP Map-Mesh (.fbx)",
    "description": "Import meshes from the ZeldaOOTProject in-game map editor with N64-accurate materials",
    "category": "Import-Export",
}

import json
import os
import re

import bpy
from bpy.props import StringProperty
from bpy_extras.io_utils import ImportHelper

G_TX_MIRROR = 1
G_TX_CLAMP = 2
CVG_X_ALPHA = 0x1000
ALPHA_CVG_SEL = 0x2000


def _axis_mode(bits):
    if bits & G_TX_CLAMP:
        return "CLAMP"
    if bits & G_TX_MIRROR:
        return "MIRROR"
    return "REPEAT"


def _wrap_axis(nodes, links, socket, mode, x):
    """Applies the N64 wrap mode to one texture coordinate (in N64 orientation, 0 = top of the texture)."""
    if mode == "REPEAT":
        node = nodes.new("ShaderNodeMath")
        node.operation = "WRAP"
        node.inputs[1].default_value = 1.0
        node.inputs[2].default_value = 0.0
    elif mode == "MIRROR":
        node = nodes.new("ShaderNodeMath")
        node.operation = "PINGPONG"
        node.inputs[1].default_value = 1.0
    else:
        node = nodes.new("ShaderNodeClamp")
        node.inputs["Min"].default_value = 0.0
        node.inputs["Max"].default_value = 1.0
    node.location = (x, 0)
    links.new(socket, node.inputs[0])
    return node.outputs[0]


def build_material(mat, info, base_dir, xlu):
    mat.use_nodes = True
    nt = mat.node_tree
    nodes, links = nt.nodes, nt.links
    nodes.clear()

    out = nodes.new("ShaderNodeOutputMaterial")
    out.location = (1400, 0)

    vcol = nodes.new("ShaderNodeVertexColor")
    vcol.layer_name = "Col"
    vcol.location = (300, -300)

    color = vcol.outputs["Color"]
    alpha = vcol.outputs["Alpha"]

    png = info.get("png") or ""
    img_path = os.path.join(base_dir, png) if png else ""
    if png and os.path.exists(img_path):
        uv = nodes.new("ShaderNodeUVMap")
        uv.uv_map = "UVMap"
        uv.location = (-900, 0)
        sep = nodes.new("ShaderNodeSeparateXYZ")
        sep.location = (-700, 0)
        links.new(uv.outputs["UV"], sep.inputs[0])

        # Blender's v points up, the N64's t points down: t = 1 - v, wrap, then back
        flip = nodes.new("ShaderNodeMath")
        flip.operation = "SUBTRACT"
        flip.inputs[0].default_value = 1.0
        flip.location = (-500, -150)
        links.new(sep.outputs["Y"], flip.inputs[1])

        s = _wrap_axis(nodes, links, sep.outputs["X"], _axis_mode(info.get("clampMirrorS", 0)), -300)
        t = _wrap_axis(nodes, links, flip.outputs[0], _axis_mode(info.get("clampMirrorT", 0)), -300)
        unflip = nodes.new("ShaderNodeMath")
        unflip.operation = "SUBTRACT"
        unflip.inputs[0].default_value = 1.0
        unflip.location = (-100, -150)
        links.new(t, unflip.inputs[1])

        comb = nodes.new("ShaderNodeCombineXYZ")
        comb.location = (100, 0)
        links.new(s, comb.inputs["X"])
        links.new(unflip.outputs[0], comb.inputs["Y"])

        tex = nodes.new("ShaderNodeTexImage")
        tex.location = (300, 100)
        tex.image = bpy.data.images.load(img_path, check_existing=True)
        tex.extension = "EXTEND"
        tex.interpolation = "Linear"
        links.new(comb.outputs[0], tex.inputs["Vector"])

        mul = nodes.new("ShaderNodeMix")
        mul.data_type = "RGBA"
        mul.blend_type = "MULTIPLY"
        mul.inputs[0].default_value = 1.0
        mul.location = (600, 0)
        links.new(tex.outputs["Color"], mul.inputs[6])
        links.new(vcol.outputs["Color"], mul.inputs[7])
        color = mul.outputs[2]

        amul = nodes.new("ShaderNodeMath")
        amul.operation = "MULTIPLY"
        amul.location = (600, -250)
        links.new(tex.outputs["Alpha"], amul.inputs[0])
        links.new(vcol.outputs["Alpha"], amul.inputs[1])
        alpha = amul.outputs[0]

    other_l = int(info.get("otherModeL", "0") or "0", 16)
    cutout = (not xlu) and bool(other_l & (CVG_X_ALPHA | ALPHA_CVG_SEL))
    if cutout:
        thr = nodes.new("ShaderNodeMath")
        thr.operation = "GREATER_THAN"
        thr.inputs[1].default_value = 0.5
        thr.location = (800, -250)
        links.new(alpha, thr.inputs[0])
        alpha = thr.outputs[0]

    emit = nodes.new("ShaderNodeEmission")
    emit.location = (900, 0)
    links.new(color, emit.inputs["Color"])
    transp = nodes.new("ShaderNodeBsdfTransparent")
    transp.location = (900, 150)
    mix = nodes.new("ShaderNodeMixShader")
    mix.location = (1150, 0)
    if xlu or cutout:
        links.new(alpha, mix.inputs[0])
    else:
        mix.inputs[0].default_value = 1.0
    links.new(transp.outputs[0], mix.inputs[1])
    links.new(emit.outputs[0], mix.inputs[2])
    links.new(mix.outputs[0], out.inputs["Surface"])

    if hasattr(mat, "surface_render_method"):
        mat.surface_render_method = "BLENDED" if xlu else "DITHERED"
    mat["zop_material"] = json.dumps(info)


def zop_import(filepath):
    """Imports one exported mesh; returns the imported objects."""
    base_dir = os.path.dirname(filepath)
    sidecar = os.path.splitext(filepath)[0] + ".zop.json"
    data = None
    if os.path.exists(sidecar):
        with open(sidecar, encoding="utf-8") as f:
            data = json.load(f)

    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=filepath)
    new_objs = [o for o in bpy.data.objects if o not in before]

    if data is not None:
        mats = {m["name"]: m for m in data.get("materials", [])}
        xlu = bool(data.get("xlu"))
        for obj in new_objs:
            if obj.type != "MESH":
                continue
            obj["zop_sidecar"] = sidecar
            for slot in obj.material_slots:
                if slot.material is None:
                    continue
                key = re.sub(r"\.\d+$", "", slot.material.name)
                if key in mats:
                    build_material(slot.material, mats[key], base_dir, xlu)
    return new_objs


class IMPORT_SCENE_OT_zop_fbx(bpy.types.Operator, ImportHelper):
    """Import a mesh exported by the ZOP in-game map editor"""

    bl_idname = "import_scene.zop_fbx"
    bl_label = "Import ZOP Map-Mesh"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".fbx"
    filter_glob: StringProperty(default="*.fbx", options={"HIDDEN"})

    def execute(self, context):
        objs = zop_import(self.filepath)
        self.report({"INFO"}, f"ZOP: {len(objs)} Objekt(e) importiert")
        return {"FINISHED"}


def _menu_import(self, context):
    self.layout.operator(IMPORT_SCENE_OT_zop_fbx.bl_idname, text="ZOP Map-Mesh (.fbx)")


def register():
    bpy.utils.register_class(IMPORT_SCENE_OT_zop_fbx)
    bpy.types.TOPBAR_MT_file_import.append(_menu_import)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(_menu_import)
    bpy.utils.unregister_class(IMPORT_SCENE_OT_zop_fbx)


if __name__ == "__main__":
    register()
