# Ramen VRM MOD の表示用マテリアルを UE エディタ上で生成する(UnrealEditor-Cmd -run=pythonscript で実行)。
#
# 作るもの(/Game/RamenVRM/):
#   M_RVRM_Lit_{Opaque,Masked,Translucent}   … ライティングあり。影の部分も明るめに見えるよう ShadeLift を発光で足す
#   M_RVRM_Unlit_{Opaque,Masked,Translucent} … ライティングなし(アニメ調)。自動露出の影響を打ち消して見た目の明るさを保つ
# すべて両面表示。パラメータ名は MOD(mod/RamenVRM/src/vrm_character.cpp)と合わせること:
#   BaseColorTexture(テクスチャ) / BaseColorFactor(色) / AlphaCutoff / ShadeLift / Brightness / Roughness / Specular /
#   UseEyeAdaptation(1 = 自動露出を打ち消す。ロッカーのプレビューでは 0)
import unreal

PACKAGE_PATH = "/Game/RamenVRM"
WHITE = "/Engine/EngineResources/WhiteSquareTexture"

mel = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def expr(mat, cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def scalar(mat, name, value, x, y):
    e = expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def binary(mat, cls, a, a_out, b, b_out, x, y):
    e = expr(mat, cls, x, y)
    mel.connect_material_expressions(a, a_out, e, "A")
    mel.connect_material_expressions(b, b_out, e, "B")
    return e


def build(name, lit, blend):
    full = f"{PACKAGE_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    mat = asset_tools.create_asset(name, PACKAGE_PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("shading_model",
                            unreal.MaterialShadingModel.MSM_DEFAULT_LIT if lit else unreal.MaterialShadingModel.MSM_UNLIT)
    if blend == unreal.BlendMode.BLEND_MASKED:
        mat.set_editor_property("opacity_mask_clip_value", 0.5)

    # 色 = テクスチャ × 係数
    tex = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1200, 0)
    tex.set_editor_property("parameter_name", "BaseColorTexture")
    tex.set_editor_property("texture", unreal.load_asset(WHITE))
    factor = expr(mat, unreal.MaterialExpressionVectorParameter, -1200, 300)
    factor.set_editor_property("parameter_name", "BaseColorFactor")
    factor.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    factor_rgb = expr(mat, unreal.MaterialExpressionComponentMask, -950, 300)
    factor_rgb.set_editor_property("r", True)
    factor_rgb.set_editor_property("g", True)
    factor_rgb.set_editor_property("b", True)
    factor_rgb.set_editor_property("a", False)
    mel.connect_material_expressions(factor, "", factor_rgb, "")
    color = binary(mat, unreal.MaterialExpressionMultiply, tex, "RGB", factor_rgb, "", -700, 100)

    # 自動露出の打ち消し(発光の明るさを「画面上の明るさ」に合わせる)
    #   exposure = lerp(1, 1 / max(EyeAdaptation, 0.0001), UseEyeAdaptation)
    # シーンキャプチャ(ロッカーのプレビュー)では EyeAdaptation が 0 になることがあり、そのまま割ると
    # 画素が壊れて何も写らなくなる。0 割りを避け、プレビューでは MOD が UseEyeAdaptation = 0 にする
    eye = expr(mat, unreal.MaterialExpressionEyeAdaptation, -900, 600)
    tiny = expr(mat, unreal.MaterialExpressionConstant, -900, 680)
    tiny.set_editor_property("r", 0.0001)
    safe_eye = binary(mat, unreal.MaterialExpressionMax, eye, "", tiny, "", -750, 620)
    one = expr(mat, unreal.MaterialExpressionConstant, -750, 700)
    one.set_editor_property("r", 1.0)
    inv_eye = binary(mat, unreal.MaterialExpressionDivide, one, "", safe_eye, "", -600, 640)
    use_eye = scalar(mat, "UseEyeAdaptation", 1.0, -600, 740)
    exposure = expr(mat, unreal.MaterialExpressionLinearInterpolate, -450, 680)
    mel.connect_material_expressions(one, "", exposure, "A")
    mel.connect_material_expressions(inv_eye, "", exposure, "B")
    mel.connect_material_expressions(use_eye, "", exposure, "Alpha")

    if lit:
        mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(scalar(mat, "Roughness", 0.9, -400, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(scalar(mat, "Specular", 0.2, -400, 300), "", unreal.MaterialProperty.MP_SPECULAR)
        lift = binary(mat, unreal.MaterialExpressionMultiply, color, "", scalar(mat, "ShadeLift", 0.2, -700, 450), "", -450, 450)
        emissive = binary(mat, unreal.MaterialExpressionMultiply, lift, "", exposure, "", -250, 500)
    else:
        bright = binary(mat, unreal.MaterialExpressionMultiply, color, "", scalar(mat, "Brightness", 1.0, -700, 450), "", -450, 450)
        emissive = binary(mat, unreal.MaterialExpressionMultiply, bright, "", exposure, "", -250, 500)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # 透明度 = テクスチャのα × 係数のα
    # (VectorParameter の既定の出力 "" は RGB の 3 成分なので、α は専用の出力 "A" から取る)
    alpha = binary(mat, unreal.MaterialExpressionMultiply, tex, "A", factor, "A", -700, 250)
    if blend == unreal.BlendMode.BLEND_MASKED:
        # VRM の alphaCutoff を使う: alpha - cutoff + 0.5 を固定のしきい値 0.5 で切る
        cutoff = scalar(mat, "AlphaCutoff", 0.5, -700, 350)
        diff = binary(mat, unreal.MaterialExpressionSubtract, alpha, "", cutoff, "", -450, 300)
        half = expr(mat, unreal.MaterialExpressionConstant, -450, 380)
        half.set_editor_property("r", 0.5)
        mask = binary(mat, unreal.MaterialExpressionAdd, diff, "", half, "", -250, 320)
        mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    elif blend == unreal.BlendMode.BLEND_TRANSLUCENT:
        mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)

    mel.layout_material_expressions(mat)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(full)
    unreal.log(f"[RamenVRM] created {full}")


for lit in (True, False):
    for suffix, blend in (("Opaque", unreal.BlendMode.BLEND_OPAQUE),
                          ("Masked", unreal.BlendMode.BLEND_MASKED),
                          ("Translucent", unreal.BlendMode.BLEND_TRANSLUCENT)):
        build(f"M_RVRM_{'Lit' if lit else 'Unlit'}_{suffix}", lit, blend)
unreal.log("[RamenVRM] all materials created")
