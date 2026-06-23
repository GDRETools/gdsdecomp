/*************************************************************************/
/*  register_types.cpp                                                   */
/*************************************************************************/

#include "register_types.h"
#include "compat/fake_script.h"
#include "core/io/image_loader.h"
#include "core/object/class_db.h"
#include "exporters/dialogue_exporter.h"
#include "exporters/func_godot_exporter.h"
#include "gui/gdre_audio_stream_preview.h"
#include "gui/gdre_color_channel_selector.h"
#include "gui/gdre_progress.h"
#include "gui/gdre_standalone.h"
#include "gui/texture_layered_previewer.h"
#include "gui/texture_previewer.h"
#include "modules/regex/regex.h"
#include "modules/register_module_types.h"
#include "servers/rendering/rendering_server.h"
#include "utility/app_version_getter.h"
#include "utility/file_access_gdre.h"
#include "utility/file_access_patched_gdre.h"
#include "utility/image_saver.h"
#include "utility/text_diff.h"
#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/gdre_editor.h"

#endif

#include "bytecode/bytecode_versions.h"
#include "compat/config_file_compat.h"
#include "compat/fake_csharp_script.h"
#include "compat/fake_gdscript.h"
#include "compat/fake_mesh.h"
#include "compat/ico_loader.h"
#include "compat/input_event_parser_v2.h"
#include "compat/oggstr_loader_compat.h"
#include "compat/optimized_translation_extractor.h"
#include "compat/resource_compat_binary.h"
#include "compat/resource_compat_obdb.h"
#include "compat/resource_compat_text.h"
#include "compat/resource_loader_compat.h"
#include "compat/sample_loader_compat.h"
#include "compat/script_loader.h"
#include "compat/texture_loader_compat.h"
#include "compat/video_stream_compat.h"
#include "compat/visual_shader_compat.h"
#include "crypto/crypto_core_gdre_contexts.h"
#include "crypto/custom_decryptor.h"
#include "crypto/file_access_encrypted_custom.h"
#include "exporters/autoconverted_exporter.h"
#include "exporters/csharp_exporter.h"
#include "exporters/dialogue_exporter.h"
#include "exporters/export_report.h"
#include "exporters/fontfile_exporter.h"
#include "exporters/gdextension_exporter.h"
#include "exporters/gdscript_exporter.h"
#include "exporters/mp3str_exporter.h"
#include "exporters/obj_exporter.h"
#include "exporters/oggstr_exporter.h"
#include "exporters/resource_exporter.h"
#include "exporters/sample_exporter.h"
#include "exporters/scene_exporter.h"
#include "exporters/shaderfile_exporter.h"
#include "exporters/spine_exporter.h"
#include "exporters/texture_exporter.h"
#include "exporters/translation_exporter.h"
#include "gui/find_replace_bar.h"
#include "gui/gdre_window.h"
#include "gui/gdre_xml_highlighter.h"
#include "gui/gui_icons.h"
#include "gui/mesh_previewer.h"
#include "gui/scene_previewer.h"
#include "main/gdre_main_loop.h"
#include "plugin_manager/asset_library_source.h"
#include "plugin_manager/codeberg_source.h"
#include "plugin_manager/github_source.h"
#include "plugin_manager/gitlab_source.h"
#include "plugin_manager/plugin_manager.h"
#include "utility/common.h"
#include "utility/gdre_config.h"
#include "utility/gdre_settings.h"
#include "utility/glob.h"
#include "utility/godot_mono_decomp_wrapper.h"
#include "utility/godotver.h"
#include "utility/import_exporter.h"
#include "utility/packed_file_info.h"
#include "utility/pck_creator.h"
#include "utility/pck_dumper.h"
#include "utility/task_manager.h"

#ifdef TOOLS_ENABLED
void gdsdecomp_init_callback() {
	EditorNode *editor = EditorNode::get_singleton();
	editor->add_child(memnew(GodotREEditor(editor)));
	editor->add_child(memnew(GDREAudioStreamPreviewGeneratorNode));
	editor->add_child(memnew(GDREProgressDialog));
	GDREMainLoopNode::setup();
}
#endif

static GDREMainLoop *gdre_main_loop = nullptr;
static GDRESettings *gdre_singleton = nullptr;
static GDREAudioStreamPreviewGenerator *audio_stream_preview_generator = nullptr;
static TaskManager *task_manager = nullptr;
static GDREConfig *gdre_config = nullptr;
static GDREGuiIcons *gui_icons = nullptr;
// TODO: move this to its own thing
static Ref<ResourceFormatLoaderCompatText> text_loader = nullptr;
static Ref<ResourceFormatLoaderCompatBinary> binary_loader = nullptr;
static Ref<ResourceFormatLoaderCompatOBDB> obdb_loader = nullptr;
static Ref<ResourceFormatLoaderCompatTexture2D> texture_loader = nullptr;
static Ref<ResourceFormatLoaderCompatTexture3D> texture3d_loader = nullptr;
static Ref<ResourceFormatLoaderCompatTextureLayered> texture_layered_loader = nullptr;
static Ref<ResourceFormatLoaderImageTextureCompat> image_texture_loader = nullptr;
static Ref<ResourceFormatGDScriptLoader> script_loader = nullptr;
static Ref<ResourceFormatLoaderCompatImage> image_loader = nullptr;
static Ref<ResourceFormatLoaderCompatVideo> video_loader = nullptr;
static Ref<ImageLoaderICO> ico_loader = nullptr;

//converters
static Ref<SampleConverterCompat> sample_converter = nullptr;
static Ref<ResourceConverterTexture2D> texture_converter = nullptr;
static Ref<ImageConverterCompat> image_converter = nullptr;
static Ref<ImageTextureConverterCompat> image_texture_converter = nullptr;
static Ref<OggStreamConverterCompat> ogg_converter = nullptr;
static Ref<LargeTextureConverterCompat> large_texture_converter = nullptr;
static Ref<FakeScriptConverterCompat> fake_script_converter = nullptr;
static Ref<TranslationConverterCompat> translation_converter = nullptr;
static Ref<InputEventConverterCompat> input_event_converter = nullptr;
static Ref<VisualShaderConverterCompat> visual_shader_converter = nullptr;

//exporters
static Ref<AutoConvertedExporter> auto_converted_exporter = nullptr;
static Ref<CSharpExporter> csharp_exporter = nullptr;
static Ref<DialogueExporter> dialogue_exporter = nullptr;
static Ref<FuncGodotLmpExporter> func_godot_lmp_exporter = nullptr;
static Ref<FuncGodotMapExporter> func_godot_map_exporter = nullptr;
static Ref<FuncGodotWADExporter> func_godot_wad_exporter = nullptr;
static Ref<FontFileExporter> fontfile_exporter = nullptr;
static Ref<GDExtensionExporter> gdextension_exporter = nullptr;
static Ref<GDScriptExporter> gdscript_exporter = nullptr;
static Ref<Mp3StrExporter> mp3str_exporter = nullptr;
static Ref<OggStrExporter> oggstr_exporter = nullptr;
static Ref<SampleExporter> sample_exporter = nullptr;
static Ref<SceneExporter> scene_exporter = nullptr;
static Ref<ShaderFileExporter> shaderfile_exporter = nullptr;
static Ref<SpineAtlasExporter> spine_atlas_exporter = nullptr;
static Ref<SpineSkeletonExporter> spine_skeleton_exporter = nullptr;
static Ref<TextureExporter> texture_exporter = nullptr;
static Ref<TranslationExporter> translation_exporter = nullptr;
static Ref<ObjExporter> obj_exporter = nullptr;
static GDREPackedData *gdre_packeddata_singleton = nullptr;

//plugin manager sources
static Ref<CodebergSource> codeberg_source = nullptr;
static Ref<GitHubSource> github_source = nullptr;
static Ref<AssetLibrarySource> asset_library_source = nullptr;
static Ref<GitLabSource> gitlab_source = nullptr;

void init_ver_regex() {
	SemVer::strict_regex = RegEx::create_from_string(GodotVer::strict_regex_str);
	GodotVer::non_strict_regex = RegEx::create_from_string(GodotVer::non_strict_regex_str);
	Glob::magic_check = RegEx::create_from_string(Glob::magic_pattern);
	Glob::escapere = RegEx::create_from_string(Glob::escape_pattern);
}

void free_ver_regex() {
	SemVer::strict_regex = Ref<RegEx>();
	GodotVer::non_strict_regex = Ref<RegEx>();
	Glob::magic_check = Ref<RegEx>();
	Glob::escapere = Ref<RegEx>();
}

void init_loaders() {
	text_loader = memnew(ResourceFormatLoaderCompatText);
	binary_loader = memnew(ResourceFormatLoaderCompatBinary);
	obdb_loader = memnew(ResourceFormatLoaderCompatOBDB);
	texture_loader = memnew(ResourceFormatLoaderCompatTexture2D);
	texture3d_loader = memnew(ResourceFormatLoaderCompatTexture3D);
	texture_layered_loader = memnew(ResourceFormatLoaderCompatTextureLayered);
	image_texture_loader = memnew(ResourceFormatLoaderImageTextureCompat);
	script_loader = memnew(ResourceFormatGDScriptLoader);
	image_loader = memnew(ResourceFormatLoaderCompatImage);
	video_loader = memnew(ResourceFormatLoaderCompatVideo);
	sample_converter = memnew(SampleConverterCompat);
	texture_converter = memnew(ResourceConverterTexture2D);
	image_converter = memnew(ImageConverterCompat);
	image_texture_converter = memnew(ImageTextureConverterCompat);
	ogg_converter = memnew(OggStreamConverterCompat);
	large_texture_converter = memnew(LargeTextureConverterCompat);
	fake_script_converter = memnew(FakeScriptConverterCompat);
	translation_converter = memnew(TranslationConverterCompat);
	input_event_converter = memnew(InputEventConverterCompat);
	visual_shader_converter = memnew(VisualShaderConverterCompat);
	ResourceCompatLoader::add_resource_format_loader(binary_loader, true);
	ResourceCompatLoader::add_resource_format_loader(obdb_loader, true);
	ResourceCompatLoader::add_resource_format_loader(text_loader, true);
	ResourceCompatLoader::add_resource_format_loader(image_texture_loader, true);
	ResourceCompatLoader::add_resource_format_loader(texture_loader, true);
	ResourceCompatLoader::add_resource_format_loader(texture3d_loader, true);
	ResourceCompatLoader::add_resource_format_loader(texture_layered_loader, true);
	ResourceCompatLoader::add_resource_format_loader(script_loader, true);
	ResourceCompatLoader::add_resource_format_loader(image_loader, true);
	ResourceCompatLoader::add_resource_format_loader(video_loader, true);
	ResourceCompatLoader::add_resource_object_converter(sample_converter, true);
	ResourceCompatLoader::add_resource_object_converter(texture_converter, true);
	ResourceCompatLoader::add_resource_object_converter(image_converter, true);
	ResourceCompatLoader::add_resource_object_converter(image_texture_converter, true);
	ResourceCompatLoader::add_resource_object_converter(ogg_converter, true);
	ResourceCompatLoader::add_resource_object_converter(large_texture_converter, true);
	ResourceCompatLoader::add_resource_object_converter(fake_script_converter, true);
	ResourceCompatLoader::add_resource_object_converter(translation_converter, true);
	ResourceCompatLoader::add_resource_object_converter(input_event_converter, true);
	ResourceCompatLoader::add_resource_object_converter(visual_shader_converter, true);
}

void init_exporters() {
	fontfile_exporter = memnew(FontFileExporter);
	mp3str_exporter = memnew(Mp3StrExporter);
	oggstr_exporter = memnew(OggStrExporter);
	sample_exporter = memnew(SampleExporter);
	texture_exporter = memnew(TextureExporter);
	obj_exporter = memnew(ObjExporter);
	translation_exporter = memnew(TranslationExporter);
	scene_exporter = memnew(SceneExporter);
	auto_converted_exporter = memnew(AutoConvertedExporter);
	dialogue_exporter = memnew(DialogueExporter);
	func_godot_lmp_exporter = memnew(FuncGodotLmpExporter);
	func_godot_map_exporter = memnew(FuncGodotMapExporter);
	func_godot_wad_exporter = memnew(FuncGodotWADExporter);
	gdscript_exporter = memnew(GDScriptExporter);
	csharp_exporter = memnew(CSharpExporter);
	gdextension_exporter = memnew(GDExtensionExporter);
	shaderfile_exporter = memnew(ShaderFileExporter);
	spine_atlas_exporter = memnew(SpineAtlasExporter);
	spine_skeleton_exporter = memnew(SpineSkeletonExporter);
	Exporter::add_exporter(auto_converted_exporter);
	Exporter::add_exporter(fontfile_exporter);
	Exporter::add_exporter(mp3str_exporter);
	Exporter::add_exporter(oggstr_exporter);
	Exporter::add_exporter(sample_exporter);
	Exporter::add_exporter(texture_exporter);
	Exporter::add_exporter(obj_exporter);
	Exporter::add_exporter(dialogue_exporter);
	Exporter::add_exporter(func_godot_lmp_exporter);
	Exporter::add_exporter(func_godot_map_exporter);
	Exporter::add_exporter(func_godot_wad_exporter);
	Exporter::add_exporter(translation_exporter);
	Exporter::add_exporter(scene_exporter);
	Exporter::add_exporter(gdscript_exporter);
	Exporter::add_exporter(csharp_exporter);
	Exporter::add_exporter(gdextension_exporter);
	Exporter::add_exporter(shaderfile_exporter);
	Exporter::add_exporter(spine_atlas_exporter);
	Exporter::add_exporter(spine_skeleton_exporter);
}

void init_plugin_manager_sources() {
	codeberg_source = memnew(CodebergSource);
	github_source = memnew(GitHubSource);
	gitlab_source = memnew(GitLabSource);
	asset_library_source = memnew(AssetLibrarySource);

	PluginManager::register_source(codeberg_source, false);
	PluginManager::register_source(github_source, false);
	PluginManager::register_source(gitlab_source, false);
	PluginManager::register_source(asset_library_source, false);
}

void deinit_plugin_manager_sources() {
	if (codeberg_source.is_valid()) {
		PluginManager::unregister_source(codeberg_source);
	}
	if (github_source.is_valid()) {
		PluginManager::unregister_source(github_source);
	}
	if (gitlab_source.is_valid()) {
		PluginManager::unregister_source(gitlab_source);
	}
	if (asset_library_source.is_valid()) {
		PluginManager::unregister_source(asset_library_source);
	}

	codeberg_source = nullptr;
	github_source = nullptr;
	gitlab_source = nullptr;
	asset_library_source = nullptr;
}

void deinit_exporters() {
	if (auto_converted_exporter.is_valid()) {
		Exporter::remove_exporter(auto_converted_exporter);
	}
	if (fontfile_exporter.is_valid()) {
		Exporter::remove_exporter(fontfile_exporter);
	}
	if (gdextension_exporter.is_valid()) {
		Exporter::remove_exporter(gdextension_exporter);
	}
	if (mp3str_exporter.is_valid()) {
		Exporter::remove_exporter(mp3str_exporter);
	}
	if (oggstr_exporter.is_valid()) {
		Exporter::remove_exporter(oggstr_exporter);
	}
	if (sample_exporter.is_valid()) {
		Exporter::remove_exporter(sample_exporter);
	}
	if (scene_exporter.is_valid()) {
		Exporter::remove_exporter(scene_exporter);
	}
	if (texture_exporter.is_valid()) {
		Exporter::remove_exporter(texture_exporter);
	}
	if (translation_exporter.is_valid()) {
		Exporter::remove_exporter(translation_exporter);
	}
	if (gdscript_exporter.is_valid()) {
		Exporter::remove_exporter(gdscript_exporter);
	}
	if (obj_exporter.is_valid()) {
		Exporter::remove_exporter(obj_exporter);
	}
	if (csharp_exporter.is_valid()) {
		Exporter::remove_exporter(csharp_exporter);
	}
	if (shaderfile_exporter.is_valid()) {
		Exporter::remove_exporter(shaderfile_exporter);
	}
	if (spine_atlas_exporter.is_valid()) {
		Exporter::remove_exporter(spine_atlas_exporter);
	}
	if (spine_skeleton_exporter.is_valid()) {
		Exporter::remove_exporter(spine_skeleton_exporter);
	}
	if (dialogue_exporter.is_valid()) {
		Exporter::remove_exporter(dialogue_exporter);
	}
	if (func_godot_lmp_exporter.is_valid()) {
		Exporter::remove_exporter(func_godot_lmp_exporter);
	}
	if (func_godot_map_exporter.is_valid()) {
		Exporter::remove_exporter(func_godot_map_exporter);
	}
	if (func_godot_wad_exporter.is_valid()) {
		Exporter::remove_exporter(func_godot_wad_exporter);
	}
	auto_converted_exporter = nullptr;
	fontfile_exporter = nullptr;
	gdextension_exporter = nullptr;
	mp3str_exporter = nullptr;
	oggstr_exporter = nullptr;
	sample_exporter = nullptr;
	scene_exporter = nullptr;
	texture_exporter = nullptr;
	translation_exporter = nullptr;
	gdscript_exporter = nullptr;
	obj_exporter = nullptr;
	dialogue_exporter = nullptr;
	csharp_exporter = nullptr;
	shaderfile_exporter = nullptr;
	spine_atlas_exporter = nullptr;
	spine_skeleton_exporter = nullptr;
	func_godot_lmp_exporter = nullptr;
	func_godot_map_exporter = nullptr;
	func_godot_wad_exporter = nullptr;
}

void deinit_loaders() {
	if (text_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(text_loader);
	}
	if (binary_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(binary_loader);
	}
	if (obdb_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(obdb_loader);
	}
	if (texture_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(texture_loader);
	}
	if (texture3d_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(texture3d_loader);
	}
	if (texture_layered_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(texture_layered_loader);
	}
	if (image_texture_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(image_texture_loader);
	}
	if (script_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(script_loader);
	}
	if (image_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(image_loader);
	}
	if (video_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(video_loader);
	}
	if (sample_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(sample_converter);
	}
	if (texture_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(texture_converter);
	}
	if (image_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(image_converter);
	}
	if (image_texture_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(image_texture_converter);
	}
	if (ogg_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(ogg_converter);
	}
	if (large_texture_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(large_texture_converter);
	}
	if (fake_script_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(fake_script_converter);
	}
	if (translation_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(translation_converter);
	}
	if (input_event_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(input_event_converter);
	}
	if (visual_shader_converter.is_valid()) {
		ResourceCompatLoader::remove_resource_object_converter(visual_shader_converter);
	}
	text_loader = nullptr;
	binary_loader = nullptr;
	obdb_loader = nullptr;
	texture_loader = nullptr;
	texture3d_loader = nullptr;
	texture_layered_loader = nullptr;
	image_texture_loader = nullptr;
	script_loader = nullptr;
	image_loader = nullptr;
	video_loader = nullptr;
	sample_converter = nullptr;
	texture_converter = nullptr;
	image_converter = nullptr;
	image_texture_converter = nullptr;
	ogg_converter = nullptr;
	large_texture_converter = nullptr;
	fake_script_converter = nullptr;
	translation_converter = nullptr;
	input_event_converter = nullptr;
	visual_shader_converter = nullptr;
}

void initialize_gdsdecomp_module(ModuleInitializationLevel p_level) {
	// We rely on MissingResource to be available for "fake" loading (primarily used during text conversion)
	ResourceLoader::set_create_missing_resources_if_class_unavailable(true);
	// Do not abort on missing resources (in case of circular dependencies or other issues that would not impede resource export)
	ResourceLoader::set_abort_on_missing_resources(false);
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	if (!Engine::get_singleton()->is_editor_hint()) {
		float scale = GDRESettings::get_auto_display_scale();
		ProjectSettings::get_singleton()->set_setting("display/window/stretch/scale", scale);
	}

#ifdef TOOLS_ENABLED
	ClassDB::register_class<PackDialog>();
	ClassDB::register_class<NewPackDialog>();
	ClassDB::register_class<ScriptCompDialog>();
	ClassDB::register_class<ScriptDecompDialog>();

#endif

	ClassDB::register_class<GDREWindow>();
	ClassDB::register_class<GDREAcceptDialogBase>();
	ClassDB::register_class<GDREConfirmationDialogBase>();
	ClassDB::register_class<GDREFileDialog>();

	ClassDB::register_class<SemVer>();
	ClassDB::register_class<GodotVer>();
	ClassDB::register_class<Glob>();
	init_ver_regex();

	ClassDB::register_abstract_class<GDScriptDecomp>();
	register_decomp_versions();

	ClassDB::register_class<FileAccessGDRE>();

	ClassDB::register_class<GodotREEditorStandalone>();
	ClassDB::register_class<PckDumper>();
	ClassDB::register_class<PckCreator>();
	ClassDB::register_class<ResourceImportMetadatav2>();
	ClassDB::register_abstract_class<ImportInfo>();
	ClassDB::register_class<ProjectConfigLoader>();
	ClassDB::register_class<AppVersionGetter>();

	ClassDB::register_class<Exporter>();
	ClassDB::register_class<ExportReport>();
	ClassDB::register_class<ResourceExporter>();
	ClassDB::register_class<AutoConvertedExporter>();
	ClassDB::register_class<FontFileExporter>();
	ClassDB::register_class<Mp3StrExporter>();
	ClassDB::register_class<OggStrExporter>();
	ClassDB::register_class<SampleExporter>();
	ClassDB::register_class<SceneExporter>();
	ClassDB::register_class<TextureExporter>();
	ClassDB::register_class<ShaderFileExporter>();
	ClassDB::register_class<TranslationExporter>();
	ClassDB::register_class<GDScriptExporter>();
	ClassDB::register_class<GDExtensionExporter>();
	ClassDB::register_class<ObjExporter>();
	ClassDB::register_class<CoreBind::ResourceCompatLoader>();
	ClassDB::register_class<CompatFormatLoader>();
	ClassDB::register_class<ResourceFormatLoaderCompatText>();
	ClassDB::register_class<ResourceFormatLoaderCompatBinary>();
	ClassDB::register_class<ResourceFormatLoaderCompatOBDB>();
	ClassDB::register_class<ResourceFormatLoaderCompatTexture2D>();
	ClassDB::register_class<ResourceFormatLoaderCompatTexture3D>();
	ClassDB::register_class<ResourceFormatLoaderCompatTextureLayered>();
	ClassDB::register_class<ResourceFormatLoaderImageTextureCompat>();
	ClassDB::register_class<ResourceFormatGDScriptLoader>();
	ClassDB::register_class<ResourceFormatLoaderCompatImage>();
	ClassDB::register_class<ResourceFormatLoaderCompatVideo>();
	ClassDB::register_class<LargeTextureConverterCompat>();
	// TODO: make ResourceCompatConverter non-abstract
	ClassDB::register_abstract_class<ResourceCompatConverter>();
	ClassDB::register_class<SampleConverterCompat>();
	ClassDB::register_class<ResourceConverterTexture2D>();
	ClassDB::register_class<ImageConverterCompat>();
	ClassDB::register_class<ImageTextureConverterCompat>();
	ClassDB::register_class<OggStreamConverterCompat>();
	ClassDB::register_class<FakeScriptConverterCompat>();
	ClassDB::register_class<TranslationConverterCompat>();
	ClassDB::register_class<InputEventConverterCompat>();
	ClassDB::register_class<OptimizedTranslationExtractor>();

	ClassDB::register_class<FakeScript>();
	ClassDB::register_class<FakeGDScript>();
	ClassDB::register_class<FakeCSharpScript>();
	ClassDB::register_class<FakeMesh>();
	ClassDB::register_class<ImportInfoModern>();
	ClassDB::register_class<ImportInfov2>();
	ClassDB::register_class<ImportInfoDummy>();
	ClassDB::register_class<ImportInfoRemap>();
	ClassDB::register_class<ImportInfoGDExt>();
	ClassDB::register_class<ImportExporter>();
	ClassDB::register_class<ImportExporterReport>();
	ClassDB::register_class<GDRESettings>();

	ClassDB::register_class<PackedFileInfo>();
	ClassDB::register_class<PackInfo>();

	ClassDB::register_class<GDREAudioStreamPreviewGeneratorNode>();
	ClassDB::register_class<GDREAudioStreamPreviewGenerator>();
	ClassDB::register_class<GDREAudioStreamPreview>();

	ClassDB::register_class<FileAccessPatchedGDRE>();

	// crypto classes
	ClassDB::register_class<CustomDecryptor>(true);
	ClassDB::register_class<FileAccessEncryptedCustom>();
	ClassDB::register_class<AESContextGDRE>();
	ClassDB::register_class<CamelliaContext>();
	ClassDB::register_class<AriaContext>();

	ClassDB::register_class<GDRECommon>();
	ClassDB::register_class<TextDiff>();
	ClassDB::register_class<TaskManager>();
	ClassDB::register_class<PluginManager>();
	ClassDB::register_class<PluginSource>();
	ClassDB::register_class<GitHubSource>();
	ClassDB::register_class<AssetLibrarySource>();
	ClassDB::register_class<GitLabSource>();
	ClassDB::register_class<ResourceInfo>();
	ClassDB::register_class<MeshPreviewer>();
	ClassDB::register_class<ScenePreviewer3D>();
	ClassDB::register_class<ScenePreviewer2D>();
	ClassDB::register_class<ScenePreviewer>();
	ClassDB::register_class<GDREColorChannelSelector>();
	ClassDB::register_class<TextureLayeredPreviewer>();
	ClassDB::register_class<TexturePreviewer>();
	ClassDB::register_class<GDREFindReplaceBar>();
	ClassDB::register_class<GDREXMLHighlighter>();
	ClassDB::register_class<GodotMonoDecompWrapper>();
	ClassDB::register_class<CoreBind::PackedFile>();
	ClassDB::register_class<PackSourceCustom>();

	ClassDB::register_class<GDREConfig>();
	ClassDB::register_class<GDREConfigSetting>();
	ClassDB::register_class<ImageSaver>();

	ClassDB::register_class<ConfigFileCompat>();
	ClassDB::register_class<GDRESceneTree>();
	ClassDB::register_class<GDREMainLoop>();

	ClassDB::register_class<GDREPackedData>();
	gdre_packeddata_singleton = memnew(GDREPackedData);
	Engine::get_singleton()->add_singleton(Engine::Singleton("GDREPackedData", GDREPackedData::get_singleton()));
	gui_icons = memnew(GDREGuiIcons);
	gdre_main_loop = memnew(GDREMainLoop);
	Engine::get_singleton()->add_singleton(Engine::Singleton("GDREMainLoop", GDREMainLoop::get_singleton()));

	init_plugin_manager_sources();
	gdre_singleton = memnew(GDRESettings);
	Engine::get_singleton()->add_singleton(Engine::Singleton("GDRESettings", GDRESettings::get_singleton()));
	gdre_config = memnew(GDREConfig);
	Engine::get_singleton()->add_singleton(Engine::Singleton("GDREConfig", GDREConfig::get_singleton()));
	audio_stream_preview_generator = memnew(GDREAudioStreamPreviewGenerator);
	Engine::get_singleton()->add_singleton(Engine::Singleton("GDREAudioStreamPreviewGenerator", GDREAudioStreamPreviewGenerator::get_singleton()));
	task_manager = memnew(TaskManager);
	Engine::get_singleton()->add_singleton(Engine::Singleton("TaskManager", TaskManager::get_singleton()));
#ifdef TOOLS_ENABLED
	EditorNode::add_init_callback(&gdsdecomp_init_callback);
#endif
	init_loaders();
	init_exporters();

	// Register ICO image loader
	ico_loader.instantiate();
	ImageLoader::add_image_format_loader(ico_loader);
	if (RenderingServer::get_singleton()) {
		TextureLayeredPreviewer::init_shaders();
		TexturePreviewer::init_shaders();
	}
}

void uninitialize_gdsdecomp_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	if (RenderingServer::get_singleton()) {
		TextureLayeredPreviewer::finish_shaders();
		TexturePreviewer::finish_shaders();
	}
	if (ico_loader.is_valid()) {
		ImageLoader::remove_image_format_loader(ico_loader);
		ico_loader.unref();
	}
	deinit_exporters();
	deinit_loaders();
	if (gdre_config) {
		memdelete(gdre_config);
		gdre_config = nullptr;
	}
	if (gdre_singleton) {
		memdelete(gdre_singleton);
		gdre_singleton = nullptr;
	}
	if (audio_stream_preview_generator) {
		memdelete(audio_stream_preview_generator);
		audio_stream_preview_generator = nullptr;
	}
	if (task_manager) {
		memdelete(task_manager);
		task_manager = nullptr;
	}
	deinit_plugin_manager_sources();
	free_ver_regex();
	if (gdre_main_loop) {
		memdelete(gdre_main_loop);
		gdre_main_loop = nullptr;
	}
	if (gui_icons) {
		memdelete(gui_icons);
		gui_icons = nullptr;
	}
	if (gdre_packeddata_singleton) {
		memdelete(gdre_packeddata_singleton);
		gdre_packeddata_singleton = nullptr;
	}
}
