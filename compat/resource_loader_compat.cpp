#include "resource_loader_compat.h"
#include "compat/resource_compat_binary.h"
#include "compat/resource_compat_text.h"
#include "compat/resource_format_xml.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/version_generated.gen.h"
#include "utility/common.h"
#include "utility/file_access_buffer.h"
#include "utility/gdre_settings.h"
#include "utility/resource_info.h"

Ref<CompatFormatLoader> ResourceCompatLoader::loaders[ResourceCompatLoader::MAX_LOADERS];
Ref<ResourceCompatConverter> ResourceCompatLoader::converters[ResourceCompatLoader::MAX_CONVERTERS];
int ResourceCompatLoader::loader_count = 0;
int ResourceCompatLoader::converter_count = 0;
bool ResourceCompatLoader::doing_gltf_load = false;
bool ResourceCompatLoader::globally_available = false;
bool ResourceCompatLoader::initialized = false;

#define FAIL_LOADER_NOT_FOUND(loader)                                                                                                                        \
	if (loader.is_null()) {                                                                                                                                  \
		if (r_error) {                                                                                                                                       \
			*r_error = ERR_FILE_NOT_FOUND;                                                                                                                   \
		}                                                                                                                                                    \
		ERR_FAIL_V_MSG(Ref<Resource>(), "Failed to load resource '" + p_path + "'. ResourceFormatLoader::load was not implemented for this resource type."); \
		return Ref<Resource>();                                                                                                                              \
	}

static Vector<Pair<String, Vector<String>>> core_recognized_extensions_v3 = {
	{ "theme", { "Theme", "Object", "Resource", "Reference", "Theme" } },
	{ "phymat", { "PhysicsMaterial", "Object", "Resource", "Reference", "PhysicsMaterial" } },
	{ "scn", { "PackedScene", "PackedSceneGLTF", "PackedScene", "Object", "Resource", "Reference" } },
	{ "largetex", { "LargeTexture", "Object", "LargeTexture", "Resource", "Reference", "Texture" } },
	{ "meshlib", { "MeshLibrary", "MeshLibrary", "Object", "Resource", "Reference" } },
	{ "lmbake", { "BakedLightmapData", "BakedLightmapData", "Object", "Resource", "Reference" } },
	{ "anim", { "Animation", "Animation", "Object", "Resource", "Reference" } },
	{ "cubemap", { "CubeMap", "CubeMap", "Object", "Resource", "Reference" } },
	{ "occ", { "OccluderShape", "OccluderShapePolygon", "Object", "OccluderShape", "Resource", "OccluderShapeSphere", "Reference" } },
	{ "mesh", { "ArrayMesh", "ArrayMesh", "Mesh", "Object", "Resource", "Reference" } },
	{ "oggstr", { "AudioStreamOGGVorbis", "AudioStream", "Object", "Resource", "Reference", "AudioStreamOGGVorbis" } },
	{ "curvetex", { "CurveTexture", "CurveTexture", "Object", "Resource", "Reference", "Texture" } },
	{ "meshtex", { "MeshTexture", "Object", "MeshTexture", "Resource", "Reference", "Texture" } },
	{ "atlastex", { "AtlasTexture", "AtlasTexture", "Object", "Resource", "Reference", "Texture" } },
	{ "font", { "BitmapFont", "BitmapFont", "Object", "Font", "Resource", "Reference" } },
	{ "material", { "Material", "SpatialMaterial", "Material3D", "ORMSpatialMaterial", "ParticlesMaterial", "Object", "CanvasItemMaterial", "ShaderMaterial", "Resource", "Reference", "Material" } },
	{ "translation", { "Translation", "Translation", "Object", "PHashTranslation", "Resource", "Reference" } },
	{ "world", { "World", "World", "Object", "Resource", "Reference" } },
	{ "multimesh", { "MultiMesh", "MultiMesh", "Object", "Resource", "Reference" } },
	{ "vs", { "VisualScript", "VisualScript", "Script", "Object", "Resource", "Reference" } },
	{ "mp3str", { "AudioStreamMP3", "AudioStream", "Object", "Resource", "AudioStreamMP3", "Reference" } },
	{ "tex", { "ImageTexture", "ImageTexture", "Object", "Resource", "Reference", "Texture" } },
	{ "shape", { "Shape", "RayShape", "CylinderShape", "HeightMapShape", "SphereShape", "Shape", "Object", "ConcavePolygonShape", "BoxShape", "CapsuleShape", "PlaneShape", "Resource", "ConvexPolygonShape", "Reference" } },
	{ "sample", { "AudioStreamSample", "AudioStreamSample", "AudioStream", "Object", "Resource", "Reference" } },
	{ "stylebox", { "StyleBox", "StyleBoxEmpty", "StyleBoxFlat", "StyleBox", "StyleBoxLine", "Object", "Resource", "StyleBoxTexture", "Reference" } },
	{ "res", { "Resource", "Curve2D", "AnimationNodeBlendTree", "InputEventMagnifyGesture", "AudioEffectHighPassFilter", "VisualShaderNodeColorFunc", "AnimationNodeTimeSeek", "AnimationNodeTimeScale", "AnimationNodeBlend2", "RayShape", "AnimationNodeBlend3", "GradientTexture", "GDScript", "VisualShaderNodeFresnel", "Animation", "VisualScriptSequence", "VisualScriptWhile", "GLTFBufferView", "VisualShaderNodeOutput", "VisualShaderNodeTextureUniform", "MeshLibrary", "PackedSceneGLTF", "VideoStream", "Image", "VisualShaderNodeVectorOp", "VisualShaderNodeVectorDerivativeFunc", "VisualShaderNodeVectorDecompose", "VisualShaderNodeSwitch", "AudioStreamGenerator", "AudioEffectReverb", "AnimationNode", "PrimitiveMesh", "AtlasTexture", "VisualScriptSceneTree", "Shape2D", "AnimationNodeStateMachineTransition", "CylinderMesh", "VisualScript", "StyleBoxEmpty", "AnimationRootNode", "TorusMesh", "VisualScriptFunctionCall", "CylinderShape", "GLTFDocument", "AudioEffectCompressor", "VisualShaderNodeVec3Constant", "VisualShaderNodeDeterminant", "ArrayMesh", "AnimationNodeBlendSpace2D", "Mesh", "AudioEffectDelay", "VisualScriptNode", "NoiseTexture", "LineShape2D", "InputEventJoypadMotion", "VisualShaderNodeColorOp", "VisualShaderNodeTextureUniformTriplanar", "InputEventWithModifiers", "TextureArray", "VisualShaderNodeScalarConstant", "VisualShaderNodeVectorClamp", "VisualShaderNodeBooleanUniform", "GLTFTextureSampler", "VisualScriptIterator", "SphereMesh", "ImageTexture", "ExternalTexture", "BitmapFont", "Skin", "Script", "AudioEffectLimiter", "Environment", "VisualScriptEmitSignal", "CurveTexture", "DynamicFontData", "TextureLayered", "RichTextEffect", "AudioEffectSpectrumAnalyzer", "VisualShaderNodeCompare", "MultiMesh", "PrismMesh", "SegmentShape2D", "VisualShaderNodeColorConstant", "VisualShaderNodeVectorCompose", "CameraTexture", "VisualScriptSwitch", "InputEventMouse", "InputEventKey", "VisualShaderNodeBooleanConstant", "VisualScriptComment", "ShortCut", "Curve3D", "VisualScriptMathConstant", "NavigationMesh", "PlaneMesh", "StreamTexture", "CubeMap", "BitMap", "VisualScriptYieldSignal", "GLTFTexture", "AudioEffect", "AudioStreamSample", "AnimationNodeAdd2", "VisualShaderNodeInput", "VisualShaderNodeExpression", "StyleBoxFlat", "AnimationNodeAdd3", "AudioEffectStereoEnhance", "GLTFLight", "PanoramaSky", "GDNativeLibrary", "BakedLightmapData", "HeightMapShape", "VisualScriptLocalVarSet", "ButtonGroup", "VisualScriptBuiltinFunc", "GLTFDocumentExtensionPhysics", "CubeMesh", "VisualShaderNodeTransformVecMult", "VideoStreamWebm", "VisualShaderNodeCubeMapUniform", "VisualScriptSubCall", "VisualScriptFunction", "VisualShaderNodeTransformMult", "VisualShaderNodeVectorDistance", "OccluderPolygon2D", "VisualScriptCondition", "VisualScriptPreload", "InputEventScreenDrag", "VisualShaderNodeColorUniform", "OpenSimplexNoise", "AudioStreamMicrophone", "RayShape2D", "AudioEffectChorus", "AnimatedTexture", "InputEventMIDI", "InputEventScreenTouch", "GradientTexture2D", "VisualScriptTypeCast", "PackedScene", "VisualScriptBasicTypeConstant", "InputEventMouseButton", "AudioEffectHighShelfFilter", "Sky", "NavigationPolygon", "SphereShape", "VisualScriptExpression", "CircleShape2D", "VisualShaderNodeGroupBase", "VisualShaderNodeTransformFunc", "VisualShaderNodeScalarInterp", "VisualShaderNodeScalarSwitch", "StyleBox", "NativeScript", "RectangleShape2D", "AnimationNodeStateMachine", "VisualShaderNodeScalarSmoothStep", "SpatialMaterial", "VisualScriptResourcePath", "VisualScriptComposeArray", "GLTFPhysicsBody", "AudioStream", "World2D", "GLTFAccessor", "VisualShaderNodeIf", "AudioEffectPhaser", "GIProbeData", "VisualShaderNodeTexture", "StyleBoxLine", "VisualShaderNodeVectorLen", "VisualShaderNodeTransformCompose", "PluginScript", "Curve", "VideoStreamTheora", "AnimationNodeAnimation", "VisualShaderNodeScalarOp", "VisualShaderNodeScalarDerivativeFunc", "Material3D", "VisualShaderNodeScalarUniform", "VisualShaderNodeVectorScalarMix", "World", "Texture3D", "VisualScriptReturn", "VisualScriptClassConstant", "GLTFDocumentExtension", "CryptoKey", "OccluderShapePolygon", "AudioEffectEQ6", "VisualShaderNodeVectorRefract", "ORMSpatialMaterial", "VisualShaderNodeCustom", "VisualShaderNodeTransformUniform", "VisualShaderNodeIs", "VisualScriptPropertyGet", "GLTFState", "ConcavePolygonShape2D", "Gradient", "VisualShaderNodeUniformRef", "VisualScriptVariableGet", "Translation", "InputEventPanGesture", "InputEventAction", "AnimationNodeTransition", "InputEventGesture", "InputEventMouseMotion", "AudioEffectAmplify", "AudioEffectBandPassFilter", "VisualShaderNodeUniform", "Shape", "VisualScriptEngineSingleton", "GLTFSkeleton", "VisualShaderNodeGlobalExpression", "GLTFNode", "AudioEffectCapture", "CapsuleShape2D", "VisualScriptOperator", "VideoStreamGDNative", "AudioEffectEQ", "AudioEffectPitchShift", "GLTFCamera", "AudioEffectEQ10", "ParticlesMaterial", "VisualScriptDeconstruct", "VisualScriptInputAction", "AnimationNodeOutput", "Object", "VisualShaderNodeTransformConstant", "PointMesh", "Font", "QuadMesh", "ConcavePolygonShape", "VisualShaderNodeDotProduct", "VisualScriptPropertySet", "GLTFMesh", "CanvasItemMaterial", "BoxShape", "VisualScriptVariableSet", "GLTFCollider", "LargeTexture", "DynamicFont", "VisualScriptLocalVar", "VisualScriptSceneNode", "AudioEffectRecord", "CapsuleShape", "MeshTexture", "VisualScriptYield", "GLTFSpecGloss", "VisualScriptIndexGet", "AudioEffectNotchFilter", "VisualShaderNodeOuterProduct", "PlaneShape", "GLTFSkin", "ConvexPolygonShape2D", "VisualShaderNodeVec3Uniform", "OccluderShape", "AnimationNodeStateMachinePlayback", "PHashTranslation", "AudioEffectPanner", "VisualScriptGlobalConstant", "PackedDataContainer", "AudioEffectFilter", "TextFile", "AnimationNodeOneShot", "ShaderMaterial", "Resource", "ProceduralSky", "VisualScriptSelect", "AudioEffectLowShelfFilter", "OccluderShapeSphere", "VisualScriptCustomNode", "EditorSpatialGizmoPlugin", "VisualShader", "StyleBoxTexture", "AnimationNodeBlendSpace1D", "VisualShaderNodeTransformDecompose", "ConvexPolygonShape", "VisualScriptIndexSet", "GLTFAnimation", "PolygonPathFinder", "AudioStreamMP3", "AudioEffectLowPassFilter", "AudioEffectDistortion", "VisualShaderNodeCubeMap", "VisualScriptConstructor", "Reference", "Material", "AudioStreamOGGVorbis", "VisualShaderNodeVectorFunc", "VisualShaderNodeVectorScalarSmoothStep", "ViewportTexture", "Texture", "VisualShaderNode", "InputEvent", "TextMesh", "PhysicsMaterial", "VisualScriptSelf", "VisualScriptConstant", "VisualShaderNodeScalarFunc", "ProxyTexture", "Theme", "VisualShaderNodeScalarClamp", "InputEventJoypadButton", "VisualShaderNodeFaceForward", "SpriteFrames", "VisualShaderNodeVectorScalarStep", "VisualShaderNodeVectorInterp", "AudioStreamRandomPitch", "Shader", "VisualScriptLists", "EditorSettings", "AudioEffectEQ21", "X509Certificate", "AudioEffectBandLimitFilter", "AudioBusLayout", "VisualShaderNodeVectorSmoothStep", "TileSet", "CapsuleMesh" } },
};

static Vector<Pair<String, Vector<String>>> core_recognized_extensions_v2 = {
	{ "shp", { "Shape", "RayShape", "Shape", "Object", "ConcavePolygonShape", "BoxShape", "CapsuleShape", "PlaneShape", "Resource", "ConvexPolygonShape", "Reference", "SphereShape" } },
	{ "gt", { "MeshLibrary", "MeshLibrary", "Object", "Resource", "Reference" } },
	{ "scn", { "PackedScene", "Object", "Resource", "Reference", "PackedScene" } },
	{ "anm", { "Animation", "Animation", "Object", "Resource", "Reference" } },
	{ "xl", { "Translation", "Translation", "Object", "PHashTranslation", "Resource", "Reference" } },
	{ "sbx", { "StyleBox", "StyleBoxImageMask", "StyleBoxEmpty", "Object", "StyleBoxFlat", "Resource", "StyleBoxTexture", "Reference", "StyleBox" } },
	{ "ltex", { "LargeTexture", "Object", "LargeTexture", "Resource", "Reference", "Texture" } },
	{ "wrd", { "World", "World", "Object", "Resource", "Reference" } },
	{ "mmsh", { "MultiMesh", "Object", "MultiMesh", "Resource", "Reference" } },
	{ "room", { "RoomBounds", "RoomBounds", "Object", "Resource", "Reference" } },
	{ "mtl", { "Material", "FixedMaterial", "Object", "ShaderMaterial", "Resource", "Reference", "Material" } },
	// not a res, discrete image type
	// { "pbm", { "BitMap", "Object", "BitMap", "Resource", "Reference" } },
	// "shd" is its own special text format that we don't support
	// { "shd", { "Shader", "Object", "CanvasItemShader", "MaterialShader", "Resource", "ShaderGraph", "Reference", "CanvasItemShaderGraph", "Shader", "MaterialShaderGraph" } },
	{ "smp", { "Sample", "Object", "Sample", "Resource", "Reference" } },
	{ "fnt", { "BitmapFont", "BitmapFont", "Object", "Font", "Resource", "Reference" } },
	{ "msh", { "Mesh", "Mesh", "Object", "Resource", "Reference" } },
	{ "thm", { "Theme", "Object", "Resource", "Reference", "Theme" } },
	{ "tex", { "ImageTexture", "ImageTexture", "Object", "Resource", "Reference", "Texture" } },
	{ "cbm", { "CubeMap", "Object", "CubeMap", "Resource", "Reference" } },
	{ "atex", { "AtlasTexture", "AtlasTexture", "Object", "Resource", "Reference", "Texture" } },
	{ "sgp", { "ShaderGraph", "Object", "Resource", "ShaderGraph", "Reference", "CanvasItemShaderGraph", "Shader", "MaterialShaderGraph" } },
	{ "res", { "Resource", "Curve2D", "RectangleShape2D", "RayShape", "AudioStreamMPC", "AudioStream", "World2D", "FixedMaterial", "GDScript", "Animation", "MeshLibrary", "AudioStreamSpeex", "VideoStream", "VideoStreamTheora", "AtlasTexture", "Shape2D", "World", "RoomBounds", "StyleBoxImageMask", "StyleBoxEmpty", "EventStreamChibi", "Mesh", "EventStream", "ConcavePolygonShape2D", "LineShape2D", "ColorRamp", "BakedLight", "Translation", "Shape", "CapsuleShape2D", "ImageTexture", "BitmapFont", "Script", "Environment", "DynamicFontData", "Object", "Font", "ConcavePolygonShape", "MultiMesh", "RenderTargetTexture", "SegmentShape2D", "BoxShape", "CanvasItemMaterial", "DynamicFont", "LargeTexture", "ShortCut", "Curve3D", "BitMap", "CubeMap", "NavigationMesh", "CapsuleShape", "StyleBoxFlat", "PlaneShape", "ConvexPolygonShape2D", "Sample", "CanvasItemShader", "PHashTranslation", "AudioStreamOpus", "PackedDataContainer", "MaterialShader", "ShaderMaterial", "Resource", "ShaderGraph", "StyleBoxTexture", "ConvexPolygonShape", "PolygonPathFinder", "Reference", "OccluderPolygon2D", "Material", "AudioStreamOGGVorbis", "Texture", "RayShape2D", "Theme", "CanvasItemShaderGraph", "SpriteFrames", "PackedScene", "SampleLibrary", "Shader", "EditorSettings", "NavigationPolygon", "SphereShape", "MaterialShaderGraph", "CircleShape2D", "StyleBox", "TileSet" } },
};

static inline HashMap<String, HashSet<String>> _init_v3_ext_to_types() {
	HashMap<String, HashSet<String>> map;
	for (const auto &pair : core_recognized_extensions_v3) {
		if (!map.has(pair.first)) {
			map[pair.first] = HashSet<String>();
		}
		for (const String &type : pair.second) {
			map[pair.first].insert(type);
		}
	}
	return map;
}

static inline HashMap<String, HashSet<String>> _init_v2_ext_to_types() {
	HashMap<String, HashSet<String>> map;
	for (const auto &pair : core_recognized_extensions_v2) {
		if (!map.has(pair.first)) {
			map[pair.first] = HashSet<String>();
		}
		for (const String &type : pair.second) {
			map[pair.first].insert(type);
		}
	}
	return map;
}

static inline HashMap<String, HashSet<String>> _init_type_to_exts() {
	HashMap<String, HashSet<String>> map;
	for (const auto &pair : core_recognized_extensions_v3) {
		if (!map.has(pair.first)) {
			map[pair.first] = HashSet<String>();
		}
		for (const String &type : pair.second) {
			map[type].insert(pair.first);
		}
	}
	for (const auto &pair : core_recognized_extensions_v2) {
		if (!map.has(pair.first)) {
			map[pair.first] = HashSet<String>();
		}
		for (const String &type : pair.second) {
			map[type].insert(pair.first);
		}
	}
	return map;
}

static HashMap<String, HashSet<String>> _init_v4_ext_to_types() {
	LocalVector<StringName> types;
	ClassDB::get_class_list(types);
	HashMap<String, HashSet<String>> map;
	for (const StringName &type : types) {
		List<String> extensions;
		ClassDB::get_extensions_for_type(type, &extensions);
		for (const String &extension : extensions) {
			if (!map.has(extension)) {
				map[extension] = HashSet<String>();
			}
			map[extension].insert(type);
		}
	}
	return map;
}

static HashMap<String, HashSet<String>> type_to_exts = _init_type_to_exts();
static HashMap<String, HashSet<String>> ext_to_v2_types = _init_v2_ext_to_types();
static HashMap<String, HashSet<String>> ext_to_v3_types = _init_v3_ext_to_types();

// NOTE: this has to be initialized at the END of the engine's initialization, so we can't do it here statically;
// _init() has to be called after all the modules have been initialized.
static HashMap<String, HashSet<String>> ext_to_v4_types;
//	static void get_base_extensions(List<String> *p_extensions);

void ResourceCompatLoader::get_base_extensions(List<String> *p_extensions, int ver_major) {
	HashSet<String> unique_extensions;
	if (ver_major > 0) {
		switch (ver_major) {
			case 1:
			case 2:
				for (const auto &pair : core_recognized_extensions_v2) {
					p_extensions->push_back(pair.first);
				}
				return;
			case 3:
				for (const auto &pair : core_recognized_extensions_v3) {
					p_extensions->push_back(pair.first);
				}
				return;
			case 4:
				ClassDB::get_resource_base_extensions(p_extensions);
				return;
			default:
				ERR_FAIL_MSG("Invalid version.");
		}
	}
	ClassDB::get_resource_base_extensions(p_extensions);

	for (const String &ext : *p_extensions) {
		unique_extensions.insert(ext);
	}
	for (const auto &pair : ext_to_v3_types) {
		if (!unique_extensions.has(pair.key)) {
			unique_extensions.insert(pair.key);
			p_extensions->push_back(pair.key);
		}
	}
	for (const auto &pair : ext_to_v2_types) {
		if (!unique_extensions.has(pair.key)) {
			unique_extensions.insert(pair.key);
			p_extensions->push_back(pair.key);
		}
	}
}

void ResourceCompatLoader::get_base_extensions_for_type(const String &p_type, List<String> *p_extensions) {
	List<String> extensions;
	HashSet<String> unique_extensions;
	ClassDB::get_extensions_for_type(p_type, &extensions);

	for (const String &ext : extensions) {
		unique_extensions.insert(ext);
	}
	if (type_to_exts.has(p_type)) {
		const HashSet<String> &old_exts = type_to_exts.get(p_type);
		for (const String &ext : old_exts) {
			unique_extensions.insert(ext);
		}
	}
	for (const String &ext : unique_extensions) {
		p_extensions->push_back(ext);
	}
}

Vector<String> ResourceCompatLoader::get_base_extension_set_for_type(const String &p_type, int ver_major) {
	Vector<String> extensions;
	bool has_res = false;

	if (ver_major > 0) {
		switch (ver_major) {
			case 1:
			case 2:
			case 3:
			case 4: {
				auto &ext_map = ver_major == 4 ? ext_to_v4_types : (ver_major == 3 ? ext_to_v3_types : ext_to_v2_types);
				for (const auto &[ext, types] : ext_map) {
					if (p_type.is_empty() || types.has(p_type)) {
						if (ext == "res") {
							has_res = true; // ensure it comes last
						} else {
							extensions.push_back(ext);
						}
					}
				}
			} break;
			default:
				ERR_FAIL_V_MSG(extensions, "Invalid version.");
		}
	} else {
		List<String> base_extensions;
		get_base_extensions_for_type(p_type, &base_extensions);
		for (const String &ext : extensions) {
			if (ext == "res") {
				has_res = true;
			} else {
				extensions.push_back(ext);
			}
		}
	}
	if (has_res) {
		extensions.push_back("res");
	}
	return extensions;
}

static inline void add_types_from_ext_to_types(const HashMap<String, HashSet<String>> &ext_to_types, const String &p_extension, HashSet<String> &unique_types) {
	if (ext_to_types.has(p_extension)) {
		const HashSet<String> &types = ext_to_types.get(p_extension);
		for (const String &type : types) {
			unique_types.insert(type);
		}
	}
}

void ResourceCompatLoader::get_type_for_extension(const String &p_extension, List<String> *p_types, int ver_major) {
	HashSet<String> unique_types;
	if (ver_major <= 2) {
		add_types_from_ext_to_types(ext_to_v2_types, p_extension, unique_types);
	}
	if (ver_major == 3 || ver_major <= 0) {
		add_types_from_ext_to_types(ext_to_v3_types, p_extension, unique_types);
	}
	if (ver_major == GODOT_VERSION_MAJOR || ver_major <= 0) {
		add_types_from_ext_to_types(ext_to_v4_types, p_extension, unique_types);
	}
}

Ref<Resource> ResourceCompatLoader::fake_load(const String &p_path, const String &p_type_hint, Error *r_error) {
	auto loadr = get_loader_for_path(p_path, p_type_hint);
	FAIL_LOADER_NOT_FOUND(loadr);
	Ref<Resource> res = loadr->custom_load(p_path, {}, ResourceInfo::LoadType::FAKE_LOAD, r_error, false, ResourceFormatLoader::CACHE_MODE_IGNORE);
	if (res.is_valid() && res->get_path().is_empty()) {
		res->set_path_cache(p_path);
	}
	return res;
}

Ref<Resource> ResourceCompatLoader::non_global_load(const String &p_path, const String &p_type_hint, Error *r_error) {
	auto loader = get_loader_for_path(p_path, p_type_hint);
	FAIL_LOADER_NOT_FOUND(loader);
	Ref<Resource> res = loader->custom_load(p_path, {}, ResourceInfo::LoadType::NON_GLOBAL_LOAD, r_error, false, ResourceFormatLoader::CACHE_MODE_IGNORE);
	if (res.is_valid() && res->get_path().is_empty()) {
		res->set_path_cache(p_path);
	}
	return res;
}

Ref<Resource> ResourceCompatLoader::gltf_load(const String &p_path, const String &p_type_hint, Error *r_error) {
	return ResourceCompatLoader::custom_load(p_path, p_type_hint, ResourceInfo::LoadType::GLTF_LOAD, r_error);
}

Ref<Resource> ResourceCompatLoader::real_load(const String &p_path, const String &p_type_hint, Error *r_error, ResourceCompatLoader::CacheMode p_cache_mode) {
	return ResourceCompatLoader::custom_load(p_path, p_type_hint, ResourceInfo::LoadType::REAL_LOAD, r_error, true, p_cache_mode);
}
namespace {
String _validate_local_path(const String &p_path) {
	ResourceUID::ID uid = ResourceUID::get_singleton()->text_to_id(p_path);
	if (uid != ResourceUID::INVALID_ID) {
		return ResourceUID::get_singleton()->get_id_path(uid);
	} else if (p_path.is_relative_path()) {
		return ("res://" + p_path).simplify_path();
	} else if ((GDRESettings::get_singleton()->is_pack_loaded() || !GDRESettings::get_singleton()->get_project_path().is_empty()) && p_path.is_absolute_path() && !p_path.begins_with("res://") && !p_path.begins_with("user://")) {
		return GDRESettings::get_singleton()->localize_path(p_path);
	}
	return p_path;
}

void _ensure_path(const Ref<Resource> &res, const String &p_path, bool is_real_load, ResourceCompatLoader::CacheMode p_cache_mode) {
	if (res.is_valid() && res->get_path().is_empty()) {
		if (is_real_load && p_cache_mode != ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP && p_cache_mode != ResourceFormatLoader::CACHE_MODE_IGNORE) {
			res->set_path(p_path, p_cache_mode == ResourceFormatLoader::CACHE_MODE_REPLACE || p_cache_mode == ResourceFormatLoader::CACHE_MODE_REPLACE_DEEP);
		} else {
			res->set_path_cache(p_path);
		}
	}
}
} //namespace

thread_local HashSet<String> currently_loading_paths;
Ref<Resource> ResourceCompatLoader::custom_load(const String &p_path, const String &p_type_hint, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceCompatLoader::CacheMode p_cache_mode) {
	String local_path = _validate_local_path(p_path);
	String res_path = GDRESettings::get_singleton()->get_mapped_path(p_path);
	bool is_real_load = p_type == ResourceInfo::LoadType::REAL_LOAD || p_type == ResourceInfo::LoadType::GLTF_LOAD;
	if (p_cache_mode == ResourceFormatLoader::CACHE_MODE_REUSE) {
		auto res = ResourceCache::get_ref(local_path);
		if (res.is_valid()) {
			return res;
		}
	}
	ERR_FAIL_COND_V_MSG(currently_loading_paths.has(res_path), Ref<Resource>(), "Circular dependency detected: " + local_path);

	auto loader = get_loader_for_path(res_path, p_type_hint);
	if (loader.is_null() && is_real_load) {
		currently_loading_paths.insert(res_path);
		Ref<Resource> res = load_with_real_resource_loader(local_path, p_type_hint, r_error, use_threads, p_cache_mode);
		currently_loading_paths.erase(res_path);
		_ensure_path(res, local_path, is_real_load, p_cache_mode);
		return res;
	}
	FAIL_LOADER_NOT_FOUND(loader);

	if (!is_real_load) {
		local_path = "";
	}
	currently_loading_paths.insert(res_path);
	Ref<Resource> res = loader->custom_load(res_path, local_path, p_type, r_error, use_threads, p_cache_mode);
	currently_loading_paths.erase(res_path);
	_ensure_path(res, local_path, is_real_load, p_cache_mode);
	return res;
}

Ref<Resource> ResourceCompatLoader::load_with_real_resource_loader(const String &p_path, const String &p_type_hint, Error *r_error, bool use_threads, ResourceCompatLoader::CacheMode p_cache_mode) {
	String local_path = _validate_local_path(p_path);
	Ref<Resource> res;
	if (use_threads) {
		res = ResourceLoader::load(local_path, p_type_hint, p_cache_mode, r_error);
	} else {
		auto load_token = ResourceLoader::_load_start(local_path, p_type_hint, ResourceLoader::LoadThreadMode::LOAD_THREAD_FROM_CURRENT, p_cache_mode);
		if (!load_token.is_valid()) {
			if (r_error) {
				*r_error = FAILED;
			}
			return Ref<Resource>();
		}
		res = ResourceLoader::_load_complete(*load_token.ptr(), r_error);
	}

	_ensure_path(res, local_path, true, p_cache_mode);

	return res;
}

ResourceInfo::LoadType ResourceCompatLoader::get_default_load_type() {
	if (doing_gltf_load) {
		return ResourceInfo::LoadType::GLTF_LOAD;
	}
	return ResourceInfo::LoadType::REAL_LOAD;
}

void ResourceCompatLoader::add_resource_format_loader(Ref<CompatFormatLoader> p_format_loader, bool p_at_front) {
	ERR_FAIL_COND(p_format_loader.is_null());
	ERR_FAIL_COND(loader_count >= MAX_LOADERS);

	if (p_at_front) {
		for (int i = loader_count; i > 0; i--) {
			loaders[i] = loaders[i - 1];
		}
		loaders[0] = p_format_loader;
		loader_count++;
	} else {
		loaders[loader_count++] = p_format_loader;
	}
	if (globally_available) {
		ResourceLoader::add_resource_format_loader(p_format_loader, p_at_front);
	}
}

void ResourceCompatLoader::remove_resource_format_loader(Ref<CompatFormatLoader> p_format_loader) {
	ERR_FAIL_COND(p_format_loader.is_null());
	if (globally_available) {
		ResourceLoader::remove_resource_format_loader(p_format_loader);
	}

	// Find loader
	int i = 0;
	for (; i < loader_count; ++i) {
		if (loaders[i] == p_format_loader) {
			break;
		}
	}

	ERR_FAIL_COND(i >= loader_count); // Not found

	// Shift next loaders up
	for (; i < loader_count - 1; ++i) {
		loaders[i] = loaders[i + 1];
	}
	loaders[loader_count - 1].unref();
	--loader_count;
}

void ResourceCompatLoader::add_resource_object_converter(Ref<ResourceCompatConverter> p_converter, bool p_at_front) {
	ERR_FAIL_COND(p_converter.is_null());
	ERR_FAIL_COND(converter_count >= MAX_CONVERTERS);

	if (p_at_front) {
		for (int i = converter_count; i > 0; i--) {
			converters[i] = converters[i - 1];
		}
		converters[0] = p_converter;
		converter_count++;
	} else {
		converters[converter_count++] = p_converter;
	}
}

void ResourceCompatLoader::remove_resource_object_converter(Ref<ResourceCompatConverter> p_converter) {
	ERR_FAIL_COND(p_converter.is_null());

	// Find converter
	int i = 0;
	for (; i < converter_count; ++i) {
		if (converters[i] == p_converter) {
			break;
		}
	}

	ERR_FAIL_COND(i >= converter_count); // Not found

	// Shift next converters up
	for (; i < converter_count - 1; ++i) {
		converters[i] = converters[i + 1];
	}
	converters[converter_count - 1].unref();
	--converter_count;
}

//get_loader_for_path
Ref<CompatFormatLoader> ResourceCompatLoader::get_loader_for_path(const String &p_path, const String &p_type_hint) {
	for (int i = 0; i < loader_count; i++) {
		if (loaders[i]->recognize_path(p_path, p_type_hint)) {
			return loaders[i];
		}
	}
	return Ref<CompatFormatLoader>();
}

Ref<ResourceCompatConverter> ResourceCompatLoader::get_converter_for_type(const String &p_type, int ver_major) {
	for (int i = 0; i < converter_count; i++) {
		if (converters[i]->handles_type(p_type, ver_major)) {
			return converters[i];
		}
	}
	return Ref<ResourceCompatConverter>();
}

Ref<Resource> ResourceCompatLoader::_load_for_text_conversion(const String &p_path, const String &original_path, Error *r_error) {
	Error err = OK;
	if (!r_error) {
		r_error = &err;
	}
	*r_error = OK;
	auto loader = get_loader_for_path(p_path, "");
	if (loader.is_null()) {
		*r_error = ERR_FILE_NOT_FOUND;
	}
	ERR_FAIL_COND_V_MSG(loader.is_null(), Ref<Resource>(), "Failed to load resource '" + p_path + "'. ResourceFormatLoader::load was not implemented for this resource type.");
	String orig_path = original_path;
	if (orig_path.is_empty() && GDRESettings::get_singleton()->is_pack_loaded()) {
		auto src_iinfo = GDRESettings::get_singleton()->get_import_info_by_dest(p_path);
		if (src_iinfo.is_valid()) {
			orig_path = src_iinfo->get_source_file();
		}
	}
	auto res = loader->custom_load(p_path, orig_path, ResourceInfo::LoadType::FAKE_LOAD, &err);
	if (err != OK || res.is_null()) {
		*r_error = err == OK ? ERR_CANT_ACQUIRE_RESOURCE : err;
	}
	if (res.is_valid() && res->get_path().is_empty()) {
		res->set_path_cache(orig_path.is_empty() ? p_path : orig_path);
	}
	return res;
}

Error ResourceCompatLoader::to_text(const String &p_path, const String &p_dst, uint32_t p_flags, const String &original_path) {
	Error err = OK;
	auto res = _load_for_text_conversion(p_path, original_path, &err);
	ERR_FAIL_COND_V_MSG(err != OK || res.is_null(), err, "Failed to load " + p_path);
	err = gdre::ensure_dir(p_dst.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create directory for " + p_dst);
	String ext = p_dst.get_extension().to_lower();
	if (ext == "xml" || ext == "x" + p_path.get_extension().to_lower()) {
		ResourceFormatSaverXML saver;
		return saver.save(res, p_dst, p_flags);
	}
	ResourceFormatSaverCompatTextInstance saver;
	return saver.save(p_dst, res, p_flags);
}

String ResourceCompatLoader::resource_to_string(const String &p_path, bool p_skip_cr) {
	ERR_FAIL_COND_V_MSG(p_path.is_empty(), "", "Path is empty");
	String orig_path;
	String path = p_path;
	if (!FileAccess::exists(path)) {
		orig_path = path;
		path = GDRESettings::get_singleton()->get_mapped_path(path);
		if (!FileAccess::exists(path)) {
			return "ERROR: File does not exist: " + path;
		}
	}
	if (path.get_file().to_lower() == "engine.cfb" || path.get_file().to_lower() == "project.binary") {
		return ProjectConfigLoader::get_project_settings_as_string(path);
	}
	String ext = path.get_extension().to_lower();
	if (ext == "tres" || ext == "tscn" || !handles_resource(path, "")) {
		Error err;
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ, &err);
		if (f.is_null() || err != OK) {
			if (err == ERR_UNAUTHORIZED || err == ERR_FILE_CORRUPT) {
				return "ERROR: Can't read encrypted file: " + path;
			}
			return "ERROR: Failed to open file: " + path;
		}
		auto buf = f->get_buffer(f->get_length());
		if (ext == "tres" || ext == "tscn" || gdre::detect_utf8(buf)) {
			String str;
			str.append_utf8((const char *)buf.ptr(), buf.size());
			return str;
		}
		return "ERROR: Failed to detect UTF-8 encoding for " + path;
	}

	Error err = OK;
	Ref<Resource> res = _load_for_text_conversion(path, orig_path, &err);
	if (err != OK || res.is_null()) {
		if (err == ERR_UNAUTHORIZED || err == ERR_FILE_CORRUPT) {
			return "ERROR: Can't read encrypted file: " + path;
		}
		return "ERROR: Failed to load " + path;
	}

	int64_t length = FileAccess::get_size(path);
	Ref<Script> script = res;
	if (script.is_valid()) {
		return script->get_source_code();
	}

	Ref<FileAccessBuffer> f = FileAccessBuffer::create(FileAccessBuffer::RESIZE_OPTIMIZED);
	f->reserve(length * 3);

	String save_path;
	String base_ext = path.get_basename().get_basename().get_extension();
	bool optimized = path.contains(".optimized.");
	bool optimized_or_converted = optimized || path.contains(".converted.");
	if (optimized && path.has_extension("scn")) {
		base_ext = "xml";
	}
	if (optimized_or_converted && (base_ext == "xml" || base_ext == "x" + path.get_extension().to_lower())) {
		ResourceFormatSaverXMLInstance saver;
		save_path = path.get_basename() + ".xml";
		err = saver.save_to_file(f, save_path, res, 0);
	} else {
		ResourceFormatSaverCompatTextInstance saver;
		save_path = path.get_basename() + (res->get_save_class() == "PackedScene" ? ".tscn" : ".tres");
		err = saver.save_to_file(f, save_path, res, 0);
	}
	if (err != OK) {
		return "ERROR: Failed to save resource '" + path + "'.";
	}
	return f->whole_file_as_utf8_string();
}

Error ResourceCompatLoader::to_binary(const String &p_path, const String &p_dst, uint32_t p_flags) {
	auto loader = get_loader_for_path(p_path, "");
	ERR_FAIL_COND_V_MSG(loader.is_null(), ERR_FILE_NOT_FOUND, "Failed to load resource '" + p_path + "'. ResourceFormatLoader::load was not implemented for this resource type.");
	Error err;

	auto res = loader->custom_load(p_path, {}, ResourceInfo::LoadType::FAKE_LOAD, &err);
	ERR_FAIL_COND_V_MSG(err != OK || res.is_null(), err, "Failed to load " + p_path);
	err = gdre::ensure_dir(p_dst.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create directory for " + p_dst);
	ResourceFormatSaverCompatBinaryInstance saver;
	return saver.save(p_dst, res, p_flags);
}

bool ResourceCompatLoader::handles_resource(const String &p_path, const String &p_type_hint) {
	return !get_loader_for_path(p_path, p_type_hint).is_null();
}

// static ResourceInfo get_resource_info(const String &p_path, const String &p_type_hint = "", Error *r_error = nullptr);
Ref<ResourceInfo> ResourceCompatLoader::get_resource_info(const String &p_path, const String &p_type_hint, Error *r_error) {
	auto loader = get_loader_for_path(p_path, p_type_hint);
	if (loader.is_null()) {
		if (r_error) {
			*r_error = ERR_UNAVAILABLE;
		}
		ERR_PRINT("Failed to load resource '" + p_path + "'. ResourceFormatLoader::load was not implemented for this resource type.");
		return Ref<ResourceInfo>();
	}
	return loader->get_resource_info(p_path, r_error);
}

//	static void get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types = false);

void ResourceCompatLoader::get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types) {
	auto loader = get_loader_for_path(p_path, "");
	if (loader.is_null()) {
		ResourceLoader::get_dependencies(p_path, p_dependencies, p_add_types);
		return;
	}
	loader->get_dependencies(p_path, p_dependencies, p_add_types);
}

// static String get_resource_script_class(const String &p_path);
String ResourceCompatLoader::get_resource_script_class(const String &p_path) {
	auto loader = get_loader_for_path(p_path, "");
	if (loader.is_null()) {
		return ResourceLoader::get_resource_script_class(p_path);
	}
	return loader->get_resource_script_class(p_path);
}

String ResourceCompatLoader::get_resource_type(const String &p_path) {
	auto loader = get_loader_for_path(p_path, "");
	if (loader.is_null()) {
		return ResourceLoader::get_resource_type(p_path);
	}
	return loader->get_resource_type(p_path);
}

bool ResourceCompatLoader::exists(const String &p_path) {
	String local_path = _validate_local_path(p_path);
	if (ResourceCache::has(local_path)) {
		return true; // If cached, it probably exists
	}

	String path = GDRESettings::get_singleton()->get_mapped_path(local_path);

	for (int i = 0; i < loader_count; i++) {
		if (!loaders[i]->recognize_path(path, "")) {
			continue;
		}

		if (loaders[i]->exists(path)) {
			return true;
		}
	}
	return ResourceLoader::exists(path);
}

bool ResourceCompatLoader::has_custom_uid_support(const String &p_path) {
	if (FileAccess::exists(p_path + ".import")) {
		return true;
	}

	auto loader = get_loader_for_path(p_path, "");
	if (loader.is_null()) {
		return false;
	}
	return loader->has_custom_uid_support();
}

bool ResourceCompatLoader::is_default_gltf_load() {
	return doing_gltf_load;
}

void ResourceCompatLoader::set_default_gltf_load(bool p_enable) {
	doing_gltf_load = p_enable;
}

void ResourceCompatLoader::make_globally_available() {
	if (globally_available) {
		return;
	}
	for (int i = loader_count - 1; i >= 0; i--) {
		ResourceLoader::add_resource_format_loader(loaders[i], true);
	}
	globally_available = true;
}

void ResourceCompatLoader::unmake_globally_available() {
	if (!globally_available) {
		return;
	}
	for (int i = 0; i < loader_count; i++) {
		ResourceLoader::remove_resource_format_loader(loaders[i]);
	}
	globally_available = false;
}

bool ResourceCompatLoader::is_globally_available() {
	return globally_available;
}

Error ResourceCompatLoader::save_custom(const Ref<Resource> &p_resource, const String &p_path, int ver_major, int ver_minor, uint32_t p_flags) {
	String path = GDRESettings::get_singleton()->globalize_path(p_path);
	ERR_FAIL_COND_V_MSG(ver_major < 0, ERR_INVALID_PARAMETER, "Invalid version info");
	Error err = gdre::ensure_dir(path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not ensure directory for " + path);
	String ext = path.get_extension().to_lower();
	if (ext == "xml" || ext == "xscn" || ext == "xres" || (ver_major <= 2 && ext.begins_with("x") && ext_to_v2_types.has(ext.substr(1)))) {
		ResourceFormatSaverXML saver;
		return saver.save_custom(p_resource, path, 0, ver_major, ver_minor, p_flags);
	}
	if (path.get_extension() == "tres" || path.get_extension() == "tscn") {
		int ver_format = ResourceFormatSaverCompatText::get_default_format_version(ver_major, ver_minor);
		ResourceFormatSaverCompatText saver;
		return saver.save_custom(p_resource, path, ver_format, ver_major, ver_minor);
	}
	int ver_format = ResourceFormatSaverCompatBinary::get_default_format_version(ver_major, ver_minor);
	ResourceFormatSaverCompatBinary saver;
	return saver.save_custom(p_resource, path, ver_format, ver_major, ver_minor);
}

String ResourceCompatConverter::get_resource_name(const Ref<MissingResource> &res, int ver_major) {
	String name;
	Variant n = ver_major < 3 ? res->get("resource/name") : res->get("resource_name");
	if (n.get_type() == Variant::STRING) {
		name = n;
	}
	if (ver_major == 0) {
		n = res->get("resource_name");
		if (n.get_type() == Variant::STRING) {
			name = n;
		}
	}
	if (name.is_empty()) {
		name = res->get_name();
	}
	return name;
}

Error ResourceCompatLoader::save_custom_to_file(const Ref<Resource> &p_resource, const String &p_path, Ref<FileAccess> &p_f, int ver_major, int ver_minor, uint32_t p_flags) {
	String path = GDRESettings::get_singleton()->globalize_path(p_path);
	ERR_FAIL_COND_V_MSG(ver_major < 0, ERR_INVALID_PARAMETER, "Invalid version info");
	Error err = gdre::ensure_dir(path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not ensure directory for " + path);
	String ext = path.get_extension().to_lower();
	if (ext == "xml" || ext == "xscn" || ext == "xres" || (ver_major <= 2 && ext.begins_with("x") && ext_to_v2_types.has(ext.substr(1)))) {
		uint32_t flags = CompatFormatLoader::set_version_info_in_flags(p_flags, 0, ver_major, ver_minor);
		ResourceFormatSaverXMLInstance saver;
		return saver.save_to_file(p_f, path, p_resource, flags);
	}
	if (path.get_extension() == "tres" || path.get_extension() == "tscn") {
		int ver_format = ResourceFormatSaverCompatText::get_default_format_version(ver_major, ver_minor);
		uint32_t flags = CompatFormatLoader::set_version_info_in_flags(p_flags, ver_format, ver_major, ver_minor);
		ResourceFormatSaverCompatTextInstance saver;
		return saver.save_to_file(p_f, path, p_resource, flags);
	}
	int ver_format = ResourceFormatSaverCompatBinary::get_default_format_version(ver_major, ver_minor);
	uint32_t flags = CompatFormatLoader::set_version_info_in_flags(p_flags, ver_format, ver_major, ver_minor);
	ResourceFormatSaverCompatBinaryInstance saver;
	return saver.save_to_file(p_f, path, p_resource, flags);
}

void ResourceCompatLoader::_init() {
	if (ResourceCompatLoader::initialized) {
		return;
	}
	ext_to_v4_types = _init_v4_ext_to_types();
	initialized = true;
	return;
}

#ifdef TESTS_ENABLED
void ResourceCompatLoader::_deinit() {
	if (!ResourceCompatLoader::initialized) {
		return;
	}
	ext_to_v4_types.clear();
	initialized = false;
}
#endif

namespace CoreBind {

Vector<String> ResourceCompatLoader::_get_dependencies(const String &p_path, bool p_add_types) {
	auto loader = ::ResourceCompatLoader::get_loader_for_path(p_path, "");
	ERR_FAIL_COND_V_MSG(loader.is_null(), Vector<String>(), "Failed to load resource '" + p_path + "'. ResourceFormatLoader::load was not implemented for this resource type.");
	List<String> dependencies;
	loader->get_dependencies(p_path, &dependencies, p_add_types);
	Vector<String> deps;
	for (List<String>::Element *E = dependencies.front(); E; E = E->next()) {
		deps.push_back(E->get());
	}
	return deps;
}

Ref<Resource> ResourceCompatLoader::_fake_load(const String &p_path, const String &p_type_hint) {
	return ::ResourceCompatLoader::fake_load(p_path, p_type_hint, nullptr);
}

Ref<Resource> ResourceCompatLoader::_non_global_load(const String &p_path, const String &p_type_hint) {
	return ::ResourceCompatLoader::non_global_load(p_path, p_type_hint, nullptr);
}

Ref<Resource> ResourceCompatLoader::_gltf_load(const String &p_path, const String &p_type_hint) {
	return ::ResourceCompatLoader::gltf_load(p_path, p_type_hint, nullptr);
}

Ref<Resource> ResourceCompatLoader::_real_load(const String &p_path, const String &p_type_hint, CacheMode p_cache_mode) {
	return ::ResourceCompatLoader::real_load(p_path, p_type_hint, nullptr, (::ResourceCompatLoader::CacheMode)p_cache_mode);
}

Dictionary ResourceCompatLoader::_get_resource_info(const String &p_path, const String &p_type_hint) {
	Ref<ResourceInfo> info = ::ResourceCompatLoader::get_resource_info(p_path, p_type_hint, nullptr);
	if (info.is_valid()) {
		return info->to_dict();
	}
	return Dictionary();
}

void ResourceCompatLoader::_bind_methods() {
	BIND_ENUM_CONSTANT(CACHE_MODE_IGNORE);
	BIND_ENUM_CONSTANT(CACHE_MODE_REUSE);
	BIND_ENUM_CONSTANT(CACHE_MODE_REPLACE);
	BIND_ENUM_CONSTANT(CACHE_MODE_IGNORE_DEEP);
	BIND_ENUM_CONSTANT(CACHE_MODE_REPLACE_DEEP);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("fake_load", "path", "type_hint"), &ResourceCompatLoader::_fake_load, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("non_global_load", "path", "type_hint"), &ResourceCompatLoader::_non_global_load, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("gltf_load", "path", "type_hint"), &ResourceCompatLoader::_gltf_load, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("real_load", "path", "type_hint", "cache_mode"), &ResourceCompatLoader::_real_load, DEFVAL(""), DEFVAL(CACHE_MODE_REUSE));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("add_resource_format_loader", "loader", "at_front"), &::ResourceCompatLoader::add_resource_format_loader, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("remove_resource_format_loader", "loader"), &::ResourceCompatLoader::remove_resource_format_loader);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("add_resource_object_converter", "converter", "at_front"), &::ResourceCompatLoader::add_resource_object_converter, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("remove_resource_object_converter", "converter"), &::ResourceCompatLoader::remove_resource_object_converter);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_resource_info", "path", "type_hint"), &ResourceCompatLoader::_get_resource_info, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_dependencies", "path", "add_types"), &ResourceCompatLoader::_get_dependencies, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("to_text", "path", "dst", "flags", "original_path"), &::ResourceCompatLoader::to_text, DEFVAL(0), DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("to_binary", "path", "dst", "flags"), &::ResourceCompatLoader::to_binary, DEFVAL(0));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("make_globally_available"), &::ResourceCompatLoader::make_globally_available);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("unmake_globally_available"), &::ResourceCompatLoader::unmake_globally_available);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("is_globally_available"), &::ResourceCompatLoader::is_globally_available);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("set_default_gltf_load", "enable"), &::ResourceCompatLoader::set_default_gltf_load);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("is_default_gltf_load"), &::ResourceCompatLoader::is_default_gltf_load);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("handles_resource", "path", "type_hint"), &::ResourceCompatLoader::handles_resource, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_resource_script_class", "path"), &::ResourceCompatLoader::get_resource_script_class);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_resource_type", "path"), &::ResourceCompatLoader::get_resource_type);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("save_custom", "resource", "path", "ver_major", "ver_minor"), &::ResourceCompatLoader::save_custom);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("resource_to_string", "path", "skip_cr"), &::ResourceCompatLoader::resource_to_string, DEFVAL(true));
	ClassDB::bind_integer_constant(get_class_static(), "LoadType", "FAKE_LOAD", ResourceInfo::FAKE_LOAD);
	ClassDB::bind_integer_constant(get_class_static(), "LoadType", "NON_GLOBAL_LOAD", ResourceInfo::NON_GLOBAL_LOAD);
	ClassDB::bind_integer_constant(get_class_static(), "LoadType", "GLTF_LOAD", ResourceInfo::GLTF_LOAD);
	ClassDB::bind_integer_constant(get_class_static(), "LoadType", "REAL_LOAD", ResourceInfo::REAL_LOAD);
}
} //namespace CoreBind
Ref<Resource> CompatFormatLoader::custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceCompatLoader::CacheMode p_cache_mode) {
	if (r_error) {
		*r_error = ERR_UNAVAILABLE;
	}
	ERR_FAIL_V_MSG(Ref<Resource>(), "Not implemented.");
}
Ref<ResourceInfo> CompatFormatLoader::get_resource_info(const String &p_path, Error *r_error) const {
	if (r_error) {
		*r_error = ERR_UNAVAILABLE;
	}
	ERR_FAIL_V_MSG(Ref<ResourceInfo>(), "Not implemented.");
}
bool CompatFormatLoader::handles_fake_load() const {
	return false;
}

Ref<Resource> ResourceCompatConverter::set_real_from_missing_resource(Ref<MissingResource> mr, Ref<Resource> res, ResourceInfo::LoadType load_type, const HashMap<String, String> &prop_map) {
	if (res.is_null()) {
		ERR_FAIL_V_MSG(Ref<Resource>(), "Failed to cast material to object: " + mr->get_path());
	}
	List<PropertyInfo> property_info;
	mr->get_property_list(&property_info);
	for (auto &property : property_info) {
		bool is_storage = property.usage & PROPERTY_USAGE_STORAGE;
		if (is_storage) {
			Ref<MissingResource> m_res = mr->get(property.name);
			const String &set_prop = prop_map.has(property.name) ? prop_map[property.name] : property.name;
			if (m_res.is_valid()) {
				res->set(set_prop, get_real_from_missing_resource(m_res, load_type));
			} else {
				res->set(set_prop, mr->get(property.name));
			}
		}
	}
	res->set_local_to_scene(mr->is_local_to_scene());
	res->set_scene_unique_id(mr->get_scene_unique_id());
	return res;
}

bool ResourceCompatConverter::is_external_resource(Ref<MissingResource> mr) {
	if (mr.is_null()) {
		return false;
	}
	auto resource_info = ResourceInfo::get_info_from_resource(mr);
	bool is_external = false;
	if (resource_info.is_valid()) {
		is_external = resource_info->topology_type == ResourceInfo::UNLOADED_EXTERNAL_RESOURCE;
	}
	return is_external;
}

Ref<MissingResource> ResourceCompatConverter::get_missing_resource_from_real(Ref<Resource> res, int ver_major, const HashMap<String, String> &required_prop_map) {
	Ref<MissingResource> mr;
	mr.instantiate();
	set_missing_resource_from_real(mr, res, ver_major, required_prop_map);
	return mr;
}

void ResourceCompatConverter::set_missing_resource_from_real(Ref<MissingResource> mr, Ref<Resource> res, int ver_major, const HashMap<String, String> &required_prop_map) {
	mr->set_original_class(res->get_class());
	mr->set_recording_properties(true);
	mr->set_path_cache(res->get_path());
	mr->set_name(res->get_name());
	mr->set_local_to_scene(res->is_local_to_scene());
	mr->set_script(res->get_script());
	mr->set_scene_unique_id(res->get_scene_unique_id());
	List<PropertyInfo> property_info;
	res->get_property_list(&property_info);
	HashSet<String> properties_to_set;
	for (auto &property : property_info) {
		properties_to_set.insert(property.name);
	}
	for (auto &[new_prop, old_prop] : required_prop_map) {
		if (properties_to_set.has(new_prop)) {
			mr->set(old_prop, res->get(new_prop));
		}
	}
}

Ref<Resource> ResourceCompatConverter::get_real_from_missing_resource(Ref<MissingResource> mr, ResourceInfo::LoadType load_type, const HashMap<String, String> &prop_map) {
	auto resource_info = ResourceInfo::get_info_from_resource(mr);
	if (is_external_resource(mr)) {
		if (load_type == ResourceInfo::LoadType::FAKE_LOAD || load_type == ResourceInfo::LoadType::NON_GLOBAL_LOAD) {
			return mr;
		}
		if (load_type == ResourceInfo::LoadType::ERR) {
			load_type = resource_info.is_valid() ? resource_info->load_type : ResourceCompatLoader::get_default_load_type();
		}
		Error err;
		auto mat = ResourceCompatLoader::custom_load(mr->get_path(), "", load_type, &err, false, ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
		ERR_FAIL_COND_V_MSG(err != OK, Ref<Resource>(), "Failed to load material: " + mr->get_path());
		return mat;
	}
	auto materialType = ClassDB::get_compatibility_remapped_class(mr->get_original_class());
	Ref<Resource> res;
	Object *obj = ClassDB::instantiate(materialType);
	if (!obj) {
		ERR_FAIL_V_MSG(Ref<Resource>(), "Failed to cast material to object: " + mr->get_path());
	}
	res = obj;
	if (res.is_null()) {
		ERR_FAIL_V_MSG(Ref<Resource>(), "Failed to cast material to object: " + mr->get_path());
	}
	return set_real_from_missing_resource(mr, res, load_type, prop_map);
}

bool CompatFormatLoader::resource_is_resource(Ref<Resource> p_res, int ver_major) {
	return p_res.is_valid() && !(ver_major <= 2 && (p_res->get_save_class() == "Image" || Ref<InputEvent>(p_res).is_valid()));
}

bool CompatFormatLoader::try_force_set_property(const Ref<Resource> &p_res, const StringName &p_name, const Variant &p_value) {
	if (p_res.is_null()) {
		return false;
	}
	ScriptInstance *si = p_res->get_script_instance();
	if (si) {
		if (FakeScriptInstance *fsi = dynamic_cast<FakeScriptInstance *>(si); fsi != nullptr) {
			Ref<FakeScript> script = fsi->get_script();
#if DEBUG_ENABLED
			// If the script is recording properties and is not a fake embedded script, then this is a bug in the script parsing.
			if (script.is_valid() && script->is_instance_recording_properties() && script->get_class() != "FakeScript") {
				WARN_PRINT(vformat("Script %s is recording properties, but initial property set failed! Please report this!", script->get_path()));
			}
#endif
			fsi->force_set_property(p_name, p_value);
			return true;
		}
		return false;
	}
	return false;
}
