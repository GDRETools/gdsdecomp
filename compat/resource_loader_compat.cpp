#include "resource_loader_compat.h"
#include "compat/resource_compat_binary.h"
#include "compat/resource_compat_text.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "utility/common.h"
#include "utility/gdre_settings.h"
#include "utility/resource_info.h"

Ref<CompatFormatLoader> ResourceCompatLoader::loaders[ResourceCompatLoader::MAX_LOADERS];
Ref<ResourceCompatConverter> ResourceCompatLoader::converters[ResourceCompatLoader::MAX_CONVERTERS];
int ResourceCompatLoader::loader_count = 0;
int ResourceCompatLoader::converter_count = 0;
bool ResourceCompatLoader::doing_gltf_load = false;
bool ResourceCompatLoader::globally_available = false;

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

HashMap<String, HashSet<String>> _init_ext_to_types() {
	HashMap<String, HashSet<String>> map;
	for (const auto &pair : core_recognized_extensions_v3) {
		if (!map.has(pair.first)) {
			map[pair.first] = HashSet<String>();
		}
		for (const String &type : pair.second) {
			map[pair.first].insert(type);
		}
	}
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

HashMap<String, HashSet<String>> _init_type_to_exts() {
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

static HashMap<String, HashSet<String>> ext_to_types = _init_ext_to_types();
static HashMap<String, HashSet<String>> type_to_exts = _init_type_to_exts();
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
	for (const auto &pair : ext_to_types) {
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
		HashSet<String> old_exts = type_to_exts.get(p_type);
		for (const String &ext : old_exts) {
			unique_extensions.insert(ext);
		}
	}
	for (const String &ext : unique_extensions) {
		p_extensions->push_back(ext);
	}
}

Ref<Resource> ResourceCompatLoader::fake_load(const String &p_path, const String &p_type_hint, Error *r_error) {
	auto loadr = get_loader_for_path(p_path, p_type_hint);
	FAIL_LOADER_NOT_FOUND(loadr);
	return loadr->custom_load(p_path, {}, ResourceInfo::LoadType::FAKE_LOAD, r_error, false, ResourceFormatLoader::CACHE_MODE_IGNORE);
}

Ref<Resource> ResourceCompatLoader::non_global_load(const String &p_path, const String &p_type_hint, Error *r_error) {
	auto loader = get_loader_for_path(p_path, p_type_hint);
	FAIL_LOADER_NOT_FOUND(loader);
	return loader->custom_load(p_path, {}, ResourceInfo::LoadType::NON_GLOBAL_LOAD, r_error, false, ResourceFormatLoader::CACHE_MODE_IGNORE);
}

Ref<Resource> ResourceCompatLoader::gltf_load(const String &p_path, const String &p_type_hint, Error *r_error) {
	return ResourceCompatLoader::custom_load(p_path, p_type_hint, ResourceInfo::LoadType::GLTF_LOAD, r_error);
}

Ref<Resource> ResourceCompatLoader::real_load(const String &p_path, const String &p_type_hint, Error *r_error, ResourceFormatLoader::CacheMode p_cache_mode) {
	return ResourceCompatLoader::custom_load(p_path, p_type_hint, ResourceInfo::LoadType::REAL_LOAD, r_error, true, p_cache_mode);
}
namespace {
String _validate_local_path(const String &p_path) {
	ResourceUID::ID uid = ResourceUID::get_singleton()->text_to_id(p_path);
	if (uid != ResourceUID::INVALID_ID) {
		return ResourceUID::get_singleton()->get_id_path(uid);
	} else if (p_path.is_relative_path()) {
		return ("res://" + p_path).simplify_path();
	} else if (GDRESettings::get_singleton()->is_pack_loaded() && p_path.is_absolute_path() && !p_path.begins_with("res://") && !p_path.begins_with("user://")) {
		return GDRESettings::get_singleton()->localize_path(p_path);
	}
	return p_path;
}
} //namespace

Ref<Resource> ResourceCompatLoader::custom_load(const String &p_path, const String &p_type_hint, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
	String local_path = _validate_local_path(p_path);
	String res_path = GDRESettings::get_singleton()->get_mapped_path(p_path);
	auto loader = get_loader_for_path(res_path, p_type_hint);
	bool is_real_load = p_type == ResourceInfo::LoadType::REAL_LOAD || p_type == ResourceInfo::LoadType::GLTF_LOAD;
	if (loader.is_null() && is_real_load) {
		return load_with_real_resource_loader(local_path, p_type_hint, r_error, use_threads, p_cache_mode);
	}
	if (p_cache_mode == ResourceFormatLoader::CACHE_MODE_REUSE) {
		auto res = ResourceCache::get_ref(local_path);
		if (res.is_valid()) {
			return res;
		}
	}
	FAIL_LOADER_NOT_FOUND(loader);

	if (!is_real_load) {
		local_path = "";
	}
	return loader->custom_load(res_path, local_path, p_type, r_error, use_threads, p_cache_mode);
}

Ref<Resource> ResourceCompatLoader::load_with_real_resource_loader(const String &p_path, const String &p_type_hint, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
	String local_path = _validate_local_path(p_path);
	if (use_threads) {
		return ResourceLoader::load(local_path, p_type_hint, p_cache_mode, r_error);
	}
	auto load_token = ResourceLoader::_load_start(local_path, p_type_hint, ResourceLoader::LoadThreadMode::LOAD_THREAD_FROM_CURRENT, p_cache_mode);
	if (!load_token.is_valid()) {
		if (r_error) {
			*r_error = FAILED;
		}
		return Ref<Resource>();
	}

	Ref<Resource> res = ResourceLoader::_load_complete(*load_token.ptr(), r_error);
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

Error ResourceCompatLoader::to_text(const String &p_path, const String &p_dst, uint32_t p_flags, const String &original_path) {
	auto loader = get_loader_for_path(p_path, "");
	ERR_FAIL_COND_V_MSG(loader.is_null(), ERR_FILE_NOT_FOUND, "Failed to load resource '" + p_path + "'. ResourceFormatLoader::load was not implemented for this resource type.");
	Error err;
	String orig_path = original_path;
	if (orig_path.is_empty() && GDRESettings::get_singleton()->is_pack_loaded()) {
		auto src_iinfo = GDRESettings::get_singleton()->get_import_info_by_dest(p_path);
		if (src_iinfo.is_valid() && src_iinfo->get_iitype() == ImportInfo::REMAP) {
			orig_path = src_iinfo->get_source_file();
		}
	}
	auto res = loader->custom_load(p_path, orig_path, ResourceInfo::LoadType::FAKE_LOAD, &err);
	ERR_FAIL_COND_V_MSG(err != OK || res.is_null(), err, "Failed to load " + p_path);
	ResourceFormatSaverCompatTextInstance saver;
	err = gdre::ensure_dir(p_dst.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to create directory for " + p_dst);
	return saver.save(p_dst, res, p_flags);
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
			*r_error = ERR_FILE_NOT_FOUND;
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

Vector<String> ResourceCompatLoader::_get_dependencies(const String &p_path, bool p_add_types) {
	auto loader = get_loader_for_path(p_path, "");
	ERR_FAIL_COND_V_MSG(loader.is_null(), Vector<String>(), "Failed to load resource '" + p_path + "'. ResourceFormatLoader::load was not implemented for this resource type.");
	List<String> dependencies;
	loader->get_dependencies(p_path, &dependencies, p_add_types);
	Vector<String> deps;
	for (List<String>::Element *E = dependencies.front(); E; E = E->next()) {
		deps.push_back(E->get());
	}
	return deps;
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

String ResourceCompatConverter::get_resource_name(const Ref<Resource> &res, int ver_major) {
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

Ref<Resource> ResourceCompatLoader::_fake_load(const String &p_path, const String &p_type_hint) {
	return fake_load(p_path, p_type_hint, nullptr);
}

Ref<Resource> ResourceCompatLoader::_non_global_load(const String &p_path, const String &p_type_hint) {
	return non_global_load(p_path, p_type_hint, nullptr);
}

Ref<Resource> ResourceCompatLoader::_gltf_load(const String &p_path, const String &p_type_hint) {
	return gltf_load(p_path, p_type_hint, nullptr);
}

Ref<Resource> ResourceCompatLoader::_real_load(const String &p_path, const String &p_type_hint, ResourceFormatLoader::CacheMode p_cache_mode) {
	return real_load(p_path, p_type_hint, nullptr, p_cache_mode);
}

Dictionary ResourceCompatLoader::_get_resource_info(const String &p_path, const String &p_type_hint) {
	Ref<ResourceInfo> info = get_resource_info(p_path, p_type_hint, nullptr);
	if (info.is_valid()) {
		return info->to_dict();
	}
	return Dictionary();
}

void ResourceCompatLoader::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("fake_load", "path", "type_hint"), &ResourceCompatLoader::_fake_load, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("non_global_load", "path", "type_hint"), &ResourceCompatLoader::_non_global_load, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("gltf_load", "path", "type_hint"), &ResourceCompatLoader::_gltf_load, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("real_load", "path", "type_hint", "cache_mode"), &ResourceCompatLoader::_real_load, DEFVAL(""), DEFVAL(ResourceFormatLoader::CACHE_MODE_REUSE));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("add_resource_format_loader", "loader", "at_front"), &ResourceCompatLoader::add_resource_format_loader, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("remove_resource_format_loader", "loader"), &ResourceCompatLoader::remove_resource_format_loader);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("add_resource_object_converter", "converter", "at_front"), &ResourceCompatLoader::add_resource_object_converter, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("remove_resource_object_converter", "converter"), &ResourceCompatLoader::remove_resource_object_converter);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_resource_info", "path", "type_hint"), &ResourceCompatLoader::_get_resource_info, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_dependencies", "path", "add_types"), &ResourceCompatLoader::_get_dependencies, DEFVAL(false));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("to_text", "path", "dst", "flags", "original_path"), &ResourceCompatLoader::to_text, DEFVAL(0), DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("to_binary", "path", "dst", "flags"), &ResourceCompatLoader::to_binary, DEFVAL(0));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("make_globally_available"), &ResourceCompatLoader::make_globally_available);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("unmake_globally_available"), &ResourceCompatLoader::unmake_globally_available);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("is_globally_available"), &ResourceCompatLoader::is_globally_available);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("set_default_gltf_load", "enable"), &ResourceCompatLoader::set_default_gltf_load);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("is_default_gltf_load"), &ResourceCompatLoader::is_default_gltf_load);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("handles_resource", "path", "type_hint"), &ResourceCompatLoader::handles_resource, DEFVAL(""));
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_resource_script_class", "path"), &ResourceCompatLoader::get_resource_script_class);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("get_resource_type", "path"), &ResourceCompatLoader::get_resource_type);
	ClassDB::bind_integer_constant(get_class_static(), "LoadType", "FAKE_LOAD", ResourceInfo::FAKE_LOAD);
	ClassDB::bind_integer_constant(get_class_static(), "LoadType", "NON_GLOBAL_LOAD", ResourceInfo::NON_GLOBAL_LOAD);
	ClassDB::bind_integer_constant(get_class_static(), "LoadType", "GLTF_LOAD", ResourceInfo::GLTF_LOAD);
	ClassDB::bind_integer_constant(get_class_static(), "LoadType", "REAL_LOAD", ResourceInfo::REAL_LOAD);
}

Ref<Resource> CompatFormatLoader::custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
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

Ref<Resource> ResourceCompatConverter::set_real_from_missing_resource(Ref<MissingResource> mr, Ref<Resource> res, ResourceInfo::LoadType load_type) {
	if (res.is_null()) {
		ERR_FAIL_V_MSG(Ref<Resource>(), "Failed to cast material to object: " + mr->get_path());
	}
	List<PropertyInfo> property_info;
	mr->get_property_list(&property_info);
	for (auto &property : property_info) {
		bool is_storage = property.usage & PROPERTY_USAGE_STORAGE;
		// if (property.usage & PROPERTY_USAGE_STORAGE) {
		if (property.name == "resource_path") {
			res->set_path_cache(mr->get(property.name));
		} else if (is_storage) {
			Ref<MissingResource> m_res = mr->get(property.name);
			if (m_res.is_valid()) {
				res->set(property.name, get_real_from_missing_resource(m_res, load_type));
			} else {
				res->set(property.name, mr->get(property.name));
			}
		} else {
			// WARN_PRINT("Property " + property.name + " is not storage");
		}
		// }
	}
	res->set_path_cache(mr->get_path());
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

Ref<Resource> ResourceCompatConverter::get_real_from_missing_resource(Ref<MissingResource> mr, ResourceInfo::LoadType load_type) {
	auto resource_info = ResourceInfo::get_info_from_resource(mr);
	if (is_external_resource(mr)) {
		if (load_type == ResourceInfo::LoadType::ERR) {
			load_type = resource_info.is_valid() ? resource_info->load_type : ResourceCompatLoader::get_default_load_type();
		}
		Error err;
		auto mat = ResourceCompatLoader::custom_load(mr->get_path(), "", load_type, &err, false, ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
		ERR_FAIL_COND_V_MSG(err != OK, Ref<Material>(), "Failed to load material: " + mr->get_path());
		return mat;
	}
	auto materialType = ClassDB::get_compatibility_remapped_class(mr->get_original_class());
	Ref<Resource> res;
	Object *obj = ClassDB::instantiate(materialType);
	if (!obj) {
		ERR_FAIL_V_MSG(Ref<Material>(), "Failed to cast material to object: " + mr->get_path());
	}
	res = obj;
	if (res.is_null()) {
		ERR_FAIL_V_MSG(Ref<Material>(), "Failed to cast material to object: " + mr->get_path());
	}
	return set_real_from_missing_resource(mr, res, load_type);
}
