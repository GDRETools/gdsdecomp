#ifndef TEXTURE_LOADER_COMPAT_H
#define TEXTURE_LOADER_COMPAT_H

#include "compat/resource_loader_compat.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/image_texture.h"
class TextureLoaderCompat {
public:
	enum TextureVersionType {
		FORMAT_NOT_TEXTURE = -1,
		FORMAT_V2_TEXTURE = 0, //tex
		FORMAT_V2_IMAGE_TEXTURE, //tex
		FORMAT_V2_ATLAS_TEXTURE, //atex
		FORMAT_V2_LARGE_TEXTURE, //ltex
		FORMAT_V2_CUBEMAP, //cbm
		FORMAT_V3_ATLAS_TEXTURE, //res
		FORMAT_V3_IMAGE_TEXTURE, //tex
		FORMAT_V3_STREAM_TEXTURE2D, //stex
		FORMAT_V3_STREAM_TEXTURE3D, //tex3d
		FORMAT_V3_STREAM_TEXTUREARRAY, //texarr
		FORMAT_V4_ATLAS_TEXTURE, //res
		FORMAT_V4_IMAGE_TEXTURE, //tex
		FORMAT_V4_COMPRESSED_TEXTURE2D, //ctex
		FORMAT_V4_COMPRESSED_TEXTURE3D, //ctex3d
		FORMAT_V4_COMPRESSED_TEXTURELAYERED //ctexarray, ccube, ccubearray
	};
	enum TextureType {
		TEXTURE_TYPE_UNKNOWN = -1,
		TEXTURE_TYPE_2D,
		TEXTURE_TYPE_3D,
		TEXTURE_TYPE_LAYERED,
		TEXTURE_TYPE_ATLAS
	};
	static Error _load_data_ctexlayered_v4(const String &p_path, Vector<Ref<Image>> &r_data, Image::Format &r_format, int &r_width, int &r_height, int &r_depth, int &r_type, bool &r_mipmaps, int &r_data_format);
	static Error _load_layered_texture_v3(const String &p_path, Vector<Ref<Image>> &r_data, Image::Format &r_format, int &r_width, int &r_height, int &r_depth, bool &r_mipmaps);
	static Ref<ResourceInfo> _get_resource_info(const String &original_path, TextureLoaderCompat::TextureVersionType t);
	static Ref<ResourceInfo> get_resource_info(const String &p_path, Error *r_error);
	static Error load_image_from_fileV3(Ref<FileAccess> f, int tw, int th, int tw_custom, int th_custom, int flags, int p_size_limit, uint32_t df, Ref<Image> &image);
	static Error _load_data_ctex2d_v4(const String &p_path, int &tw, int &th, int &tw_custom, int &th_custom, Ref<Image> &image, int &r_data_format, int &r_texture_flags, int p_size_limit = 0);
	static Error _load_data_stex2d_v3(const String &p_path, int &tw, int &th, int &tw_custom, int &th_custom, int &flags, Ref<Image> &image, int p_size_limit = 0);

	static Ref<ImageTexture> create_image_texture(const String &p_path, ResourceInfo::LoadType p_type, int tw, int th, int tw_custom, int th_custom, bool mipmaps, Ref<Image> image);
	static bool is_binary_resource(TextureVersionType t);
	static TextureVersionType recognize(const String &p_path, Error *r_err);
	static int get_ver_major_from_textype(TextureVersionType type);
	static TextureType get_type_enum_from_version_type(TextureVersionType type);
	static String get_type_name_from_textype(TextureVersionType type);
	static Vector<Ref<Image>> load_images_from_layered_tex(const String p_path, Error *r_err);
};

class ResourceConverterTexture2D : public ResourceCompatConverter {
	GDCLASS(ResourceConverterTexture2D, ResourceCompatConverter);

public:
	virtual Ref<Resource> convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error = nullptr) override;
	virtual bool handles_type(const String &p_type, int ver_major) const override;
};

class ImageConverterCompat : public ResourceCompatConverter {
	GDCLASS(ImageConverterCompat, ResourceCompatConverter);

public:
	virtual Ref<Resource> convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error = nullptr) override;
	virtual bool handles_type(const String &p_type, int ver_major) const override;
};

class ImageTextureConverterCompat : public ResourceCompatConverter {
	GDCLASS(ImageTextureConverterCompat, ResourceCompatConverter);

public:
	virtual Ref<Resource> convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error = nullptr) override;
	virtual bool handles_type(const String &p_type, int ver_major) const override;
};

class LargeTextureConverterCompat : public ResourceCompatConverter {
	GDCLASS(LargeTextureConverterCompat, ResourceCompatConverter);

public:
	virtual Ref<Resource> convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error = nullptr) override;
	virtual bool handles_type(const String &p_type, int ver_major) const override;
};

class ResourceFormatLoaderCompatTexture2D : public CompatFormatLoader {
	GDCLASS(ResourceFormatLoaderCompatTexture2D, CompatFormatLoader);

public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;

	static Ref<CompressedTexture2D> _set_tex(const String &p_path, ResourceInfo::LoadType p_type, int tw, int th, int tw_custom, int th_custom, int flags, Ref<Image> image);
	virtual Ref<Resource> custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error = nullptr, bool use_threads = true, ResourceFormatLoader::CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual Ref<ResourceInfo> get_resource_info(const String &p_path, Error *r_error) const override;
	virtual bool handles_fake_load() const override { return false; }
};

class ResourceFormatLoaderCompatTexture3D : public CompatFormatLoader {
	GDCLASS(ResourceFormatLoaderCompatTexture3D, CompatFormatLoader);

public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;

	static Ref<CompressedTexture3D> _set_tex(const String &p_path, ResourceInfo::LoadType p_type, int tw, int th, int td, bool mipmaps, const Vector<Ref<Image>> &images);
	virtual Ref<Resource> custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error = nullptr, bool use_threads = true, ResourceFormatLoader::CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual Ref<ResourceInfo> get_resource_info(const String &p_path, Error *r_error) const override;
	virtual bool handles_fake_load() const override { return false; }
};

class ResourceFormatLoaderCompatTextureLayered : public CompatFormatLoader {
	GDCLASS(ResourceFormatLoaderCompatTextureLayered, CompatFormatLoader);

public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;

	static Ref<CompressedTextureLayered> _set_tex(const String &p_path, ResourceInfo::LoadType p_type, int tw, int th, int td, int type, bool mipmaps, const Vector<Ref<Image>> &images);
	virtual Ref<Resource> custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error = nullptr, bool use_threads = true, ResourceFormatLoader::CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual Ref<ResourceInfo> get_resource_info(const String &p_path, Error *r_error) const override;
	virtual bool handles_fake_load() const override { return false; }
};

class ResourceFormatLoaderCompatImage : public CompatFormatLoader {
	GDCLASS(ResourceFormatLoaderCompatImage, CompatFormatLoader);

public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;

	virtual Ref<Resource> custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error = nullptr, bool use_threads = true, ResourceFormatLoader::CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual Ref<ResourceInfo> get_resource_info(const String &p_path, Error *r_error) const override;
	virtual bool handles_fake_load() const override { return false; }
};

#endif // TEXTURE_LOADER_COMPAT_H
