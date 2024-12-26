/*************************************************************************/
/*  register_types.cpp                                                   */
/*************************************************************************/

#include "register_types.h"
#include "compat/fake_script.h"
#include "core/object/class_db.h"
#include "modules/regex/regex.h"
#include "utility/file_access_gdre.h"
#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#endif

#include "bytecode/bytecode_versions.h"
#include "compat/oggstr_loader_compat.h"
#include "compat/resource_compat_binary.h"
#include "compat/resource_compat_text.h"
#include "compat/resource_loader_compat.h"
#include "compat/sample_loader_compat.h"
#include "compat/texture_loader_compat.h"
#include "editor/gdre_editor.h"
#include "exporters/autoconverted_exporter.h"
#include "exporters/export_report.h"
#include "exporters/fontfile_exporter.h"
#include "exporters/gdextension_exporter.h"
#include "exporters/mp3str_exporter.h"
#include "exporters/oggstr_exporter.h"
#include "exporters/resource_exporter.h"
#include "exporters/sample_exporter.h"
#include "exporters/scene_exporter.h"
#include "exporters/texture_exporter.h"
#include "exporters/translation_exporter.h"
#include "utility/gdre_settings.h"
#include "utility/glob.h"
#include "utility/godotver.h"
#include "utility/import_exporter.h"
#include "utility/packed_file_info.h"
#include "utility/pck_dumper.h"

#include "module_etc_decompress/register_types.h"

#ifdef TOOLS_ENABLED
void gdsdecomp_init_callback() {
	EditorNode *editor = EditorNode::get_singleton();
	editor->add_child(memnew(GodotREEditor(editor)));
};
#endif

static GDRESettings *gdre_singleton = nullptr;
// TODO: move this to its own thing
static Ref<ResourceFormatLoaderCompatText> text_loader = nullptr;
static Ref<ResourceFormatLoaderCompatBinary> binary_loader = nullptr;
static Ref<ResourceFormatLoaderCompatTexture2D> texture_loader = nullptr;
static Ref<ResourceFormatLoaderCompatTexture3D> texture3d_loader = nullptr;
static Ref<ResourceFormatLoaderCompatTextureLayered> texture_layered_loader = nullptr;
//converters
static Ref<SampleConverterCompat> sample_converter = nullptr;
static Ref<ResourceConverterTexture2D> texture_converter = nullptr;
static Ref<ImageConverterCompat> image_converter = nullptr;
static Ref<ImageTextureConverterCompat> image_texture_converter = nullptr;
static Ref<OggStreamConverterCompat> ogg_converter = nullptr;

//exporters
static Ref<AutoConvertedExporter> auto_converted_exporter = nullptr;
static Ref<FontFileExporter> fontfile_exporter = nullptr;
static Ref<GDExtensionExporter> gdextension_exporter = nullptr;
static Ref<Mp3StrExporter> mp3str_exporter = nullptr;
static Ref<OggStrExporter> oggstr_exporter = nullptr;
static Ref<SampleExporter> sample_exporter = nullptr;
static Ref<SceneExporter> scene_exporter = nullptr;
static Ref<TextureExporter> texture_exporter = nullptr;
static Ref<TranslationExporter> translation_exporter = nullptr;

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
	texture_loader = memnew(ResourceFormatLoaderCompatTexture2D);
	texture3d_loader = memnew(ResourceFormatLoaderCompatTexture3D);
	texture_layered_loader = memnew(ResourceFormatLoaderCompatTextureLayered);
	sample_converter = memnew(SampleConverterCompat);
	texture_converter = memnew(ResourceConverterTexture2D);
	image_converter = memnew(ImageConverterCompat);
	image_texture_converter = memnew(ImageTextureConverterCompat);
	ogg_converter = memnew(OggStreamConverterCompat);
	ResourceCompatLoader::add_resource_format_loader(binary_loader, true);
	ResourceCompatLoader::add_resource_format_loader(text_loader, true);
	ResourceCompatLoader::add_resource_format_loader(texture_loader, true);
	ResourceCompatLoader::add_resource_format_loader(texture3d_loader, true);
	ResourceCompatLoader::add_resource_format_loader(texture_layered_loader, true);
	ResourceCompatLoader::add_resource_object_converter(sample_converter, true);
	ResourceCompatLoader::add_resource_object_converter(texture_converter, true);
	ResourceCompatLoader::add_resource_object_converter(image_converter, true);
	ResourceCompatLoader::add_resource_object_converter(image_texture_converter, true);
	ResourceCompatLoader::add_resource_object_converter(ogg_converter, true);
}

void init_exporters() {
	auto_converted_exporter = memnew(AutoConvertedExporter);
	fontfile_exporter = memnew(FontFileExporter);
	gdextension_exporter = memnew(GDExtensionExporter);
	mp3str_exporter = memnew(Mp3StrExporter);
	oggstr_exporter = memnew(OggStrExporter);
	sample_exporter = memnew(SampleExporter);
	scene_exporter = memnew(SceneExporter);
	texture_exporter = memnew(TextureExporter);
	translation_exporter = memnew(TranslationExporter);
	Exporter::add_exporter(auto_converted_exporter);
	Exporter::add_exporter(fontfile_exporter);
	Exporter::add_exporter(gdextension_exporter);
	Exporter::add_exporter(mp3str_exporter);
	Exporter::add_exporter(oggstr_exporter);
	Exporter::add_exporter(sample_exporter);
	Exporter::add_exporter(scene_exporter);
	Exporter::add_exporter(texture_exporter);
	Exporter::add_exporter(translation_exporter);
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
	auto_converted_exporter = nullptr;
	fontfile_exporter = nullptr;
	gdextension_exporter = nullptr;
	mp3str_exporter = nullptr;
	oggstr_exporter = nullptr;
	sample_exporter = nullptr;
	scene_exporter = nullptr;
	texture_exporter = nullptr;
	translation_exporter = nullptr;
}

void deinit_loaders() {
	if (text_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(text_loader);
	}
	if (binary_loader.is_valid()) {
		ResourceCompatLoader::remove_resource_format_loader(binary_loader);
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
	text_loader = nullptr;
	binary_loader = nullptr;
	texture_loader = nullptr;
	texture3d_loader = nullptr;
	texture_layered_loader = nullptr;
	sample_converter = nullptr;
	texture_converter = nullptr;
	image_converter = nullptr;
	image_texture_converter = nullptr;
	ogg_converter = nullptr;
}

void initialize_gdsdecomp_module(ModuleInitializationLevel p_level) {
	ResourceLoader::set_create_missing_resources_if_class_unavailable(true);
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	ClassDB::register_class<SemVer>();
	ClassDB::register_class<GodotVer>();
	ClassDB::register_class<Glob>();
	init_ver_regex();

	ClassDB::register_abstract_class<GDScriptDecomp>();
	register_decomp_versions();

	ClassDB::register_class<FileAccessGDRE>();

	ClassDB::register_class<GodotREEditorStandalone>();
	ClassDB::register_class<PckDumper>();
	ClassDB::register_class<ResourceImportMetadatav2>();
	ClassDB::register_abstract_class<ImportInfo>();

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
	ClassDB::register_class<TranslationExporter>();

	ClassDB::register_class<ResourceCompatLoader>();
	ClassDB::register_class<CompatFormatLoader>();
	ClassDB::register_class<ResourceFormatLoaderCompatText>();
	ClassDB::register_class<ResourceFormatLoaderCompatBinary>();
	ClassDB::register_class<ResourceFormatLoaderCompatTexture2D>();
	ClassDB::register_class<ResourceFormatLoaderCompatTexture3D>();
	ClassDB::register_class<ResourceFormatLoaderCompatTextureLayered>();
	// TODO: make ResourceCompatConverter non-abstract
	ClassDB::register_abstract_class<ResourceCompatConverter>();
	ClassDB::register_class<SampleConverterCompat>();
	ClassDB::register_class<ResourceConverterTexture2D>();
	ClassDB::register_class<ImageConverterCompat>();
	ClassDB::register_class<ImageTextureConverterCompat>();
	ClassDB::register_class<OggStreamConverterCompat>();
	ClassDB::register_class<FakeEmbeddedScript>();
	ClassDB::register_class<FakeGDScript>();

	ClassDB::register_class<ImportInfoModern>();
	ClassDB::register_class<ImportInfov2>();
	ClassDB::register_class<ImportInfoDummy>();
	ClassDB::register_class<ImportInfoRemap>();
	ClassDB::register_class<ImportInfoGDExt>();
	ClassDB::register_class<ImportExporter>();
	ClassDB::register_class<ImportExporterReport>();
	ClassDB::register_class<GDRESettings>();

	ClassDB::register_class<PackedFileInfo>();

	ClassDB::register_class<PackDialog>();
	ClassDB::register_class<NewPackDialog>();
	ClassDB::register_class<ScriptCompDialog>();
	ClassDB::register_class<ScriptDecompDialog>();
	gdre_singleton = memnew(GDRESettings);
	Engine::get_singleton()->add_singleton(Engine::Singleton("GDRESettings", GDRESettings::get_singleton()));

#ifdef TOOLS_ENABLED
	EditorNode::add_init_callback(&gdsdecomp_init_callback);
#endif
	init_loaders();
	init_exporters();
	initialize_etcpak_decompress_module(p_level);
}

void uninitialize_gdsdecomp_module(ModuleInitializationLevel p_level) {
	uninitialize_etcpak_decompress_module(p_level);
	deinit_exporters();
	deinit_loaders();
	if (gdre_singleton) {
		memdelete(gdre_singleton);
		gdre_singleton = nullptr;
	}
	free_ver_regex();
}
