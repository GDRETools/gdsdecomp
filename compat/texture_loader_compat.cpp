#include "compat/texture_loader_compat.h"
#include "compat/image_enum_compat.h"
#include "compat/resource_compat_binary.h"
#include "compat/resource_loader_compat.h"
#include "compat/webp_compat.h"
#include "core/io/resource_loader.h"
#include "utility/resource_info.h"

#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/missing_resource.h"
#include "core/variant/dictionary.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/texture.h"

enum FormatBits {
	FORMAT_MASK_IMAGE_FORMAT = (1 << 20) - 1,
	FORMAT_BIT_LOSSLESS = 1 << 20, // v2
	FORMAT_BIT_PNG = 1 << 20, // v3
	FORMAT_BIT_LOSSY = 1 << 21, // v2
	FORMAT_BIT_WEBP = 1 << 21, // v3
	FORMAT_BIT_STREAM = 1 << 22,
	FORMAT_BIT_HAS_MIPMAPS = 1 << 23,
	FORMAT_BIT_DETECT_3D = 1 << 24,
	FORMAT_BIT_DETECT_SRGB = 1 << 25,
	FORMAT_BIT_DETECT_NORMAL = 1 << 26,
	FORMAT_BIT_DETECT_ROUGNESS = 1 << 27,
};

bool is_real_or_gltf_load(ResourceInfo::LoadType p_type) {
	return p_type == ResourceInfo::LoadType::REAL_LOAD || p_type == ResourceInfo::LoadType::GLTF_LOAD;
}

void set_res_path(Ref<Resource> res, const String &path, ResourceInfo::LoadType p_type, ResourceFormatLoader::CacheMode cache_mode) {
	if (res.is_valid()) {
		if (p_type == ResourceInfo::LoadType::REAL_LOAD) {
			if (cache_mode != ResourceFormatLoader::CACHE_MODE_IGNORE && cache_mode != ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP) {
				res->set_path(path, cache_mode == ResourceFormatLoader::CACHE_MODE_REPLACE);
			} else {
				if (res->get_path().is_empty()) {
					RID rid = res->get_rid();
					if (rid.is_valid()) {
						RenderingServer::get_singleton()->texture_set_path(rid, path);
					}
				}
				res->set_path_cache(path);
			}
		} else if (p_type == ResourceInfo::LoadType::GLTF_LOAD) {
			// set_path() on textures calls RenderingServer::texture_set_path(),
			// and GLTF_LOADs do not load the image into the rendering server,
			// so we just set the path cache.
			res->set_path_cache(path);
		}
	}
}

Ref<ResourceInfo> merge_resource_info(Ref<ResourceInfo> &new_dict, Ref<ResourceInfo> &texture_dict, int int_flags) {
	new_dict->ver_major = texture_dict->ver_major;
	new_dict->type = texture_dict->type;
	new_dict->resource_format = texture_dict->resource_format;
	if (new_dict->original_path.is_empty()) {
		new_dict->original_path = texture_dict->original_path;
	}
	if (texture_dict->extra.has("texture_flags")) {
		new_dict->extra["texture_flags"] = texture_dict->extra["texture_flags"];
	} else {
		new_dict->extra["texture_flags"] = int_flags;
	}
	if (texture_dict->extra.has("data_format")) {
		new_dict->extra["data_format"] = texture_dict->extra["data_format"];
	}
	return new_dict;
}

void _set_resource_info(Ref<ResourceInfo> &info, const String &original_path, TextureLoaderCompat::TextureVersionType t) {
	if (!info.is_valid()) {
		info.instantiate();
	}
	info->ver_major = TextureLoaderCompat::get_ver_major_from_textype(t);
	info->type = TextureLoaderCompat::get_type_name_from_textype(t);
	info->resource_format = "Texture";
	info->original_path = original_path;
}

Ref<ResourceInfo> TextureLoaderCompat::_get_resource_info(const String &original_path, TextureLoaderCompat::TextureVersionType t) {
	Ref<ResourceInfo> info;
	info.instantiate();
	_set_resource_info(info, original_path, t);
	return info;
}

TextureLoaderCompat::TextureVersionType TextureLoaderCompat::recognize(const String &p_path, Error *r_err) {
	Error err;
	if (!r_err) {
		r_err = &err;
	}
	const String res_path = p_path;
	Ref<FileAccess> f = FileAccess::open(res_path, FileAccess::READ, r_err);

	ERR_FAIL_COND_V_MSG(*r_err != OK || f.is_null(), FORMAT_NOT_TEXTURE, "Can't open texture file " + p_path);

	uint8_t header[4];
	f->get_buffer(header, 4);
	//Only reading the header
	if (header[0] == 'G' && header[1] == 'D' && header[2] == 'S' && header[3] == 'T') {
		return TextureVersionType::FORMAT_V3_STREAM_TEXTURE2D;
	} else if (header[0] == 'G' && header[1] == 'D' && header[2] == '3' && header[3] == 'T') {
		return TextureVersionType::FORMAT_V3_STREAM_TEXTURE3D;
	} else if (header[0] == 'G' && header[1] == 'D' && header[2] == 'A' && header[3] == 'T') {
		return TextureVersionType::FORMAT_V3_STREAM_TEXTUREARRAY;
	} else if (header[0] == 'G' && header[1] == 'S' && header[2] == 'T' && header[3] == 'L') {
		String ext = p_path.get_extension();
		if (ext == "ctexarray" || ext == "ccube" || ext == "ccubearray") {
			return TextureVersionType::FORMAT_V4_COMPRESSED_TEXTURELAYERED;
		}
		return TextureVersionType::FORMAT_V4_COMPRESSED_TEXTURE3D;
	} else if (header[0] == 'G' && header[1] == 'S' && header[2] == 'T' && header[3] == '2') {
		return TextureVersionType::FORMAT_V4_COMPRESSED_TEXTURE2D;
	} else if ((header[0] == 'R' && header[1] == 'S' && header[2] == 'R' && header[3] == 'C') ||
			(header[0] == 'R' && header[1] == 'S' && header[2] == 'C' && header[3] == 'C')) {
		// check if this is a V2 texture
		ResourceFormatLoaderCompatBinary rlcb;
		Ref<ResourceInfo> i_info = rlcb.get_resource_info(p_path, r_err);

		if (*r_err == ERR_PRINTER_ON_FIRE) {
			// no import metadata
			*r_err = OK;
		} else if (*r_err) {
			ERR_FAIL_V_MSG(FORMAT_NOT_TEXTURE, "Can't open texture file " + p_path);
		}
		String type = i_info->type;
		if (type == "Texture") {
			return FORMAT_V2_TEXTURE;
		} else if (type == "ImageTexture") {
			if (i_info->ver_major <= 2) {
				return FORMAT_V2_IMAGE_TEXTURE;
			} else if (i_info->ver_major == 3) {
				return FORMAT_V3_IMAGE_TEXTURE;
			}
			return FORMAT_V4_IMAGE_TEXTURE;
		} else if (type == "AtlasTexture") {
			switch (i_info->ver_major) {
				case 1:
				case 2:
					return FORMAT_V2_ATLAS_TEXTURE;
				case 3:
					return FORMAT_V3_ATLAS_TEXTURE;
				default:
					return FORMAT_V4_ATLAS_TEXTURE;
			}
		} else if (type == "LargeTexture") {
			return FORMAT_V2_LARGE_TEXTURE;
		} else if (type == "CubeMap") {
			return FORMAT_V2_CUBEMAP;
		}
	}
	*r_err = ERR_FILE_UNRECOGNIZED;
	return FORMAT_NOT_TEXTURE;
}

int TextureLoaderCompat::get_ver_major_from_textype(TextureVersionType type) {
	switch (type) {
		case FORMAT_V2_TEXTURE:
		case FORMAT_V2_IMAGE_TEXTURE:
		case FORMAT_V2_ATLAS_TEXTURE:
		case FORMAT_V2_LARGE_TEXTURE:
		case FORMAT_V2_CUBEMAP:
			return 2;
		case FORMAT_V3_ATLAS_TEXTURE:
		case FORMAT_V3_IMAGE_TEXTURE:
		case FORMAT_V3_STREAM_TEXTURE2D:
		case FORMAT_V3_STREAM_TEXTURE3D:
		case FORMAT_V3_STREAM_TEXTUREARRAY:
			return 3;
		case FORMAT_V4_ATLAS_TEXTURE:
		case FORMAT_V4_IMAGE_TEXTURE:
		case FORMAT_V4_COMPRESSED_TEXTURE2D:
		case FORMAT_V4_COMPRESSED_TEXTURE3D:
		case FORMAT_V4_COMPRESSED_TEXTURELAYERED:
			return 4;
		default:
			return -1;
	}
}

TextureLoaderCompat::TextureType TextureLoaderCompat::get_type_enum_from_version_type(TextureVersionType type) {
	switch (type) {
		// layered
		case FORMAT_V2_LARGE_TEXTURE:
		case FORMAT_V2_CUBEMAP:
		case FORMAT_V3_STREAM_TEXTUREARRAY:
		case FORMAT_V4_COMPRESSED_TEXTURELAYERED:
			return TEXTURE_TYPE_LAYERED;
		// 3d
		case FORMAT_V3_STREAM_TEXTURE3D:
		case FORMAT_V4_COMPRESSED_TEXTURE3D:
			return TEXTURE_TYPE_3D;
		// 2d
		case FORMAT_V2_TEXTURE:
		case FORMAT_V2_IMAGE_TEXTURE:
		case FORMAT_V3_IMAGE_TEXTURE:
		case FORMAT_V3_STREAM_TEXTURE2D:
		case FORMAT_V4_COMPRESSED_TEXTURE2D:
		case FORMAT_V4_IMAGE_TEXTURE:
			return TEXTURE_TYPE_2D;
		case FORMAT_V2_ATLAS_TEXTURE:
		case FORMAT_V3_ATLAS_TEXTURE:
		case FORMAT_V4_ATLAS_TEXTURE:
			return TEXTURE_TYPE_ATLAS;
		default:
			return TEXTURE_TYPE_UNKNOWN;
	}
}

bool TextureLoaderCompat::is_binary_resource(TextureVersionType t) {
	switch (t) {
		case FORMAT_V2_TEXTURE:
		case FORMAT_V2_IMAGE_TEXTURE:
		case FORMAT_V2_ATLAS_TEXTURE:
		case FORMAT_V2_LARGE_TEXTURE:
		case FORMAT_V2_CUBEMAP:
		case FORMAT_V3_IMAGE_TEXTURE:
		case FORMAT_V4_IMAGE_TEXTURE:
		case FORMAT_V3_ATLAS_TEXTURE:
		case FORMAT_V4_ATLAS_TEXTURE:
			return true;
		default:
			return false;
	}
}

String TextureLoaderCompat::get_type_name_from_textype(TextureVersionType type) {
	switch (type) {
		case FORMAT_V2_TEXTURE:
			return "Texture";
		case FORMAT_V2_IMAGE_TEXTURE:
		case FORMAT_V3_IMAGE_TEXTURE:
		case FORMAT_V4_IMAGE_TEXTURE:
			return "ImageTexture";
		case FORMAT_V2_ATLAS_TEXTURE:
		case FORMAT_V3_ATLAS_TEXTURE:
		case FORMAT_V4_ATLAS_TEXTURE:
			return "AtlasTexture";
		case FORMAT_V2_LARGE_TEXTURE:
			return "LargeTexture";
		case FORMAT_V2_CUBEMAP:
			return "CubeMap";
		case FORMAT_V3_STREAM_TEXTURE2D:
			return "StreamTexture";
		case FORMAT_V3_STREAM_TEXTURE3D:
			return "StreamTexture3D";
		case FORMAT_V3_STREAM_TEXTUREARRAY:
			return "StreamTextureArray";
		case FORMAT_V4_COMPRESSED_TEXTURE2D:
			return "CompressedTexture2D";
		case FORMAT_V4_COMPRESSED_TEXTURE3D:
			return "CompressedTexture3D";
		case FORMAT_V4_COMPRESSED_TEXTURELAYERED:
			return "CompressedTextureLayered";
		default:
			return "Unknown";
	}
}

Error TextureLoaderCompat::load_image_from_fileV3(Ref<FileAccess> f, int tw, int th, int tw_custom, int th_custom, int flags, int p_size_limit, uint32_t df, Ref<Image> &image) {
	Image::Format format;
	if (!(df & FORMAT_BIT_STREAM)) {
		// do something??
	}
	if (df & FORMAT_BIT_PNG || df & FORMAT_BIT_WEBP) {
		//look for a PNG or WEBP file inside

		int sw = tw;
		int sh = th;

		uint32_t mipmaps = f->get_32();
		uint32_t size = f->get_32();

		//print_line("mipmaps: " + itos(mipmaps));

		while (mipmaps > 1 && p_size_limit > 0 && (sw > p_size_limit || sh > p_size_limit)) {
			f->seek(f->get_position() + size);
			mipmaps = f->get_32();
			size = f->get_32();

			sw = MAX(sw >> 1, 1);
			sh = MAX(sh >> 1, 1);
			mipmaps--;
		}

		// mipmaps need to be read independently, they will be later combined
		Vector<Ref<Image>> mipmap_images;
		int total_size = 0;

		for (uint32_t i = 0; i < mipmaps; i++) {
			if (i) {
				size = f->get_32();
			}
			if (size == 0) {
				ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, "Texture is empty");
			}
			Vector<uint8_t> pv;
			pv.resize(size);
			{
				uint8_t *wr = pv.ptrw();
				f->get_buffer(wr, size);
			}

			Ref<Image> img;
			if (df & FORMAT_BIT_PNG) {
				img = Image::png_unpacker(pv);
			} else {
				img = WebPCompat::webp_unpack_v2v3(pv);
			}
			ERR_FAIL_COND_V_MSG(img.is_null() || img->is_empty(), ERR_FILE_CORRUPT, "File is corrupt");

			if (i != 0) {
				img->convert(mipmap_images[0]->get_format()); // ensure the same format for all mipmaps
			}

			total_size += img->get_data().size();

			mipmap_images.push_back(img);
		}

		//print_line("mipmap read total: " + itos(mipmap_images.size()));
		format = mipmap_images[0]->get_format();

		if (mipmap_images.size() == 1) {
			image = mipmap_images[0];
		} else {
			Vector<uint8_t> img_data;
			img_data.resize(total_size);

			{
				uint8_t *wr = img_data.ptrw();

				int ofs = 0;
				for (int i = 0; i < mipmap_images.size(); i++) {
					Vector<uint8_t> id = mipmap_images[i]->get_data();
					int len = id.size();
					const uint8_t *r = id.ptr();
					memcpy(&wr[ofs], r, len);
					ofs += len;
				}
			}
			image->initialize_data(tw, th, true, format, img_data);
		}
	} else {
		//look for regular format
		uint32_t v3_fmt = df & FORMAT_MASK_IMAGE_FORMAT;
		format = ImageEnumCompat::convert_image_format_enum_v3_to_v4(V3Image::Format(v3_fmt));
		if (format == Image::FORMAT_MAX) {
			// deprecated format
			ERR_FAIL_COND_V_MSG(v3_fmt > 0 && v3_fmt < V3Image::FORMAT_MAX, ERR_UNAVAILABLE,
					"Support for deprecated texture format " + ImageEnumCompat::get_v3_format_name(V3Image::Format(v3_fmt)) + " is unimplemented.");
			// TODO: check if this is a custom Sonic Colors Unlimited format
			ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, "Texture is in an invalid format: " + itos(v3_fmt));
		}

		bool mipmaps = df & FORMAT_BIT_HAS_MIPMAPS;

		if (!mipmaps) {
			int size = Image::get_image_data_size(tw, th, format, false);

			Vector<uint8_t> img_data;
			img_data.resize(size);

			{
				uint8_t *wr = img_data.ptrw();
				f->get_buffer(wr, size);
			}

			image->initialize_data(tw, th, false, format, img_data);
		} else {
			int sw = tw;
			int sh = th;

			int mipmaps2 = Image::get_image_required_mipmaps(tw, th, format);
			int total_size = Image::get_image_data_size(tw, th, format, true);
			int idx = 0;

			while (mipmaps2 > 1 && p_size_limit > 0 && (sw > p_size_limit || sh > p_size_limit)) {
				sw = MAX(sw >> 1, 1);
				sh = MAX(sh >> 1, 1);
				mipmaps2--;
				idx++;
			}

			int ofs = Image::get_image_mipmap_offset(tw, th, format, idx);

			ERR_FAIL_COND_V_MSG(total_size - ofs <= 0, ERR_FILE_CORRUPT,
					"Failed to create image of format " + Image::get_format_name(format) + "from texture");

			f->seek(f->get_position() + ofs);

			Vector<uint8_t> img_data;
			img_data.resize(total_size - ofs);

			{
				uint8_t *wr = img_data.ptrw();
				int bytes = f->get_buffer(wr, total_size - ofs);
				//print_line("requested read: " + itos(total_size - ofs) + " but got: " + itos(bytes));

				int expected = total_size - ofs;
				if (bytes < expected) {
					// this is a compatibility workaround for older format, which saved less mipmaps2. It is still recommended the image is reimported.
					memset(wr + bytes, 0, (expected - bytes));
				}
				ERR_FAIL_COND_V(bytes != expected, ERR_FILE_CORRUPT);
			}
			image->initialize_data(sw, sh, true, format, img_data);
		}
	}
	ERR_FAIL_COND_V_MSG(image.is_null() || image->is_empty(), ERR_FILE_CORRUPT, "Failed to create image of format " + Image::get_format_name(format) + "from texture");
	return OK;
}

class OverrideTexture2D : public CompressedTexture2D {
public:
	Ref<Image> image;
	virtual Ref<Image> get_image() const override {
		// otherwise, call the parent
		return image;
	}
	virtual String get_save_class() const override {
		return "CompressedTexture2D";
	}
};
class faketex2D : Texture2D {
	GDCLASS(faketex2D, Texture2D);

public:
	String path_to_file;
	mutable RID texture;
	Image::Format format = Image::FORMAT_L8;
	int w = 0;
	int h = 0;
	mutable Ref<BitMap> alpha_cache;
};
static_assert(sizeof(faketex2D) == sizeof(CompressedTexture2D), "faketex2D must be the same size as CompressedTexture2D");

Error TextureLoaderCompat::_load_data_stex2d_v3(const String &p_path, int &tw, int &th, int &tw_custom, int &th_custom, int &flags, Ref<Image> &image, int p_size_limit) {
	Error err;
	// TODO: make this pass back the flags

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(f.is_null(), err, "Can't open image file for loading: " + p_path);
	uint8_t header[4];
	f->get_buffer(header, 4);
	// already checked

	tw = f->get_16();
	tw_custom = f->get_16();
	th = f->get_16();
	th_custom = f->get_16();

	flags = f->get_32(); // texture flags!
	uint32_t df = f->get_32(); // data format
	p_size_limit = 0;
	if (image.is_null()) {
		image.instantiate();
	}
	err = load_image_from_fileV3(f, tw, th, tw_custom, th_custom, flags, p_size_limit, df, image);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Failed to load image from texture file " + p_path);

	/*
	print_line("width: " + itos(tw));
	print_line("height: " + itos(th));
	print_line("flags: " + itos(flags));
	print_line("df: " + itos(df));
	*/

	return OK;
}

Error TextureLoaderCompat::_load_data_ctex2d_v4(const String &p_path, int &tw, int &th, int &tw_custom, int &th_custom, Ref<Image> &image, int &r_data_format, int &r_texture_flags, int p_size_limit) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	uint8_t header[4];
	// already checked header
	f->get_buffer(header, 4);

	uint32_t version = f->get_32();

	if (version > CompressedTexture2D::FORMAT_VERSION) {
		ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, "Compressed texture file is too new.");
	}
	tw_custom = f->get_32();
	th_custom = f->get_32();
	r_texture_flags = f->get_32(); //texture flags

	//skip reserved
	f->get_32(); // mipmap_limit, unused
	//reserved
	f->get_32();
	f->get_32();
	f->get_32();

	if (!(r_texture_flags & FORMAT_BIT_STREAM)) {
		p_size_limit = 0;
	}
	int64_t pos = f->get_position();
	r_data_format = f->get_32();
	f->seek(pos);

	image = CompressedTexture2D::load_image_from_file(f, p_size_limit);

	if (image.is_null() || image->is_empty()) {
		return ERR_CANT_OPEN;
	}
	if (!tw_custom) {
		tw = image->get_width();
	}
	if (!th_custom) {
		th = image->get_height();
	}
	return OK;
}

Error TextureLoaderCompat::_load_layered_texture_v3(const String &p_path, Vector<Ref<Image>> &r_data, Image::Format &r_format, int &r_width, int &r_height, int &r_depth, bool &r_mipmaps) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(f.is_null(), err, "Cannot open file '" + p_path + "'.");

	uint8_t header[5] = { 0, 0, 0, 0, 0 };
	f->get_buffer(header, 4);
	// already checked

	r_width = f->get_32();
	r_height = f->get_32();
	r_depth = f->get_32();
	int flags = f->get_32(); //texture flags!
	r_mipmaps = (flags & 1); // Texture::FLAG_MIPMAPS

	Image::Format format = ImageEnumCompat::convert_image_format_enum_v3_to_v4(V3Image::Format(f->get_32()));
	ERR_FAIL_COND_V_MSG(format == Image::FORMAT_MAX, ERR_FILE_CORRUPT, "Textured layer is in an invalid or deprecated format");

	uint32_t compression = f->get_32(); // 0 - lossless (PNG), 1 - vram, 2 - uncompressed

	for (int layer = 0; layer < r_depth; layer++) {
		Ref<Image> image;
		image.instantiate();

		if (compression == 0) { // COMPRESSION_LOSSLESS
			// look for a PNG file inside

			int mipmaps = f->get_32();
			if (mipmaps > 1) {
				r_mipmaps = true;
			}
			Vector<Ref<Image>> mipmap_images;

			for (int i = 0; i < mipmaps; i++) {
				uint32_t size = f->get_32();

				Vector<uint8_t> pv;
				pv.resize(size);
				{
					f->get_buffer(pv.ptrw(), size);
				}
				Ref<Image> img = Image::png_unpacker(pv);

				if (img.is_null() || img->is_empty() || format != img->get_format()) {
					ERR_FAIL_V(ERR_FILE_CORRUPT);
				}
				mipmap_images.push_back(img);
			}

			if (mipmap_images.size() == 1) {
				image = mipmap_images[0];

			} else {
				int total_size = Image::get_image_data_size(r_width, r_height, format, true);
				Vector<uint8_t> img_data;
				img_data.resize(total_size);

				{
					int ofs = 0;
					for (int i = 0; i < mipmap_images.size(); i++) {
						Vector<uint8_t> id = mipmap_images[i]->get_data();
						int len = id.size();
						memcpy(&img_data.ptrw()[ofs], id.ptr(), len);
						ofs += len;
					}
				}

				image->initialize_data(r_width, r_height, true, format, img_data);
				if (image->is_empty()) {
					ERR_FAIL_V(ERR_FILE_CORRUPT);
				}
			}

		} else {
			// look for regular format
			bool mipmaps = (flags & 1); // Texture::FLAG_MIPMAPS
			int total_size = Image::get_image_data_size(r_width, r_height, format, mipmaps);

			Vector<uint8_t> img_data;
			img_data.resize(total_size);

			{
				int bytes = f->get_buffer(img_data.ptrw(), total_size);
				if (bytes != total_size) {
					ERR_FAIL_V(ERR_FILE_CORRUPT);
				}
			}
			image->initialize_data(r_width, r_height, mipmaps, format, img_data);
		}
		r_data.push_back(image);
	}

	return OK;
}

Error TextureLoaderCompat::_load_data_ctexlayered_v4(const String &p_path, Vector<Ref<Image>> &r_data, Image::Format &r_format, int &r_width, int &r_height, int &r_depth, int &r_type, bool &r_mipmaps, int &r_data_format) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_OPEN, vformat("Unable to open file: %s.", p_path));

	uint8_t header[4];
	f->get_buffer(header, 4);
	ERR_FAIL_COND_V(header[0] != 'G' || header[1] != 'S' || header[2] != 'T' || header[3] != 'L', ERR_FILE_UNRECOGNIZED);

	// stored as stream textures (used for lossless and lossy compression)
	uint32_t version = f->get_32();

	if (version > 1) {
		ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, "Stream texture file is too new.");
	}

	r_depth = f->get_32(); //depth or layer count (CompressedTextureLayered)
	r_type = f->get_32(); //type
	f->get_32(); // Nothing
	f->get_32(); // Nothing
	int mipmaps = f->get_32();
	f->get_32(); // ignored
	f->get_32(); // ignored

	r_mipmaps = mipmaps != 0;

	r_data.clear();

	String ext = p_path.get_extension();
	bool is_layered = false;
	if (ext == "ctexarray" || ext == "ccube" || ext == "ccubearray") {
		is_layered = true;
	}

	int limit = is_layered ? r_depth : r_depth + mipmaps;
	for (int i = 0; i < limit; i++) {
		if (i == 0) {
			size_t pos = f->get_position();
			r_data_format = f->get_32();
			f->seek(pos);
		}
		Ref<Image> image = CompressedTexture2D::load_image_from_file(f, 0);
		ERR_FAIL_COND_V(image.is_null() || image->is_empty(), ERR_CANT_OPEN);
		if (i == 0) {
			r_format = image->get_format();
			r_width = image->get_width();
			r_height = image->get_height();
		}
		r_data.push_back(image);
	}

	return OK;
}

class OverrideTexture3D : public CompressedTexture3D {
public:
	Vector<Ref<Image>> data;
	virtual Vector<Ref<Image>> get_data() const override {
		return data;
	}
	virtual String get_save_class() const override {
		return "CompressedTexture3D";
	}
};
class faketex3D : Texture3D {
	GDCLASS(faketex3D, Texture3D);

public:
	String path_to_file;
	mutable RID texture;
	Image::Format format = Image::FORMAT_L8;
	int w = 0;
	int h = 0;
	int d = 0;
	bool mipmaps = false;
};
static_assert(sizeof(faketex3D) == sizeof(CompressedTexture3D), "faketex3D must be the same size as CompressedTexture3D");

template <class T>
class OverrideTextureLayered : public T {
	static_assert(std::is_base_of<TextureLayered, T>::value, "T must be a subclass of TextureLayered");

public:
	Vector<Ref<Image>> layer_data;
	virtual Ref<Image> get_layer_data(int layer) const override {
		return layer_data[layer];
	}
	virtual String get_save_class() const override {
		return T::get_save_class();
	}
};
class faketexlayered : TextureLayered {
	GDCLASS(faketexlayered, TextureLayered);

public:
	String path_to_file;
	mutable RID texture;
	Image::Format format = Image::FORMAT_L8;
	int w = 0;
	int h = 0;
	int layers = 0;
	bool mipmaps = false;
	LayeredType layered_type = LayeredType::LAYERED_TYPE_2D_ARRAY;
};
static_assert(sizeof(faketexlayered) == sizeof(CompressedTextureLayered), "faketexlayered must be the same size as CompressedTextureLayered");

// TODO: What to do with this?
Vector<Ref<Image>> TextureLoaderCompat::load_images_from_layered_tex(const String p_path, Error *r_err) {
	Error err;
	Vector<Ref<Image>> data;
	const String res_path = p_path;

	TextureLoaderCompat::TextureVersionType t = recognize(res_path, &err);
	if (t == FORMAT_NOT_TEXTURE) {
		if (r_err) {
			*r_err = err;
		}
		ERR_FAIL_COND_V_MSG(err == ERR_FILE_UNRECOGNIZED, data, "File " + res_path + " is not a texture.");
		ERR_FAIL_COND_V(err != OK, data);
	}
	auto textype = get_type_enum_from_version_type(t);
	switch (textype) {
		case TEXTURE_TYPE_3D: {
			ResourceFormatLoaderCompatTexture3D rlcb;
			Ref<Texture3D> res = rlcb.custom_load(res_path, {}, ResourceInfo::LoadType::NON_GLOBAL_LOAD, &err);
			data = res->get_data();
		} break;
		case TEXTURE_TYPE_LAYERED: {
			ResourceFormatLoaderCompatTextureLayered rlcb;
			Ref<TextureLayered> res = rlcb.custom_load(res_path, {}, ResourceInfo::LoadType::NON_GLOBAL_LOAD, &err);

			for (int i = 0; i < res->get_layers(); i++) {
				data.push_back(res->get_layer_data(i));
			}
		} break;
		default:
			if (r_err) {
				*r_err = ERR_INVALID_PARAMETER;
			}
			ERR_FAIL_V_MSG(data, "Not a 3d image texture: " + res_path);
			break;
	}

	if (r_err) {
		*r_err = err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, data, "Texture " + res_path + " could not be loaded");
	return data;
}

bool ResourceConverterTexture2D::handles_type(const String &p_type, int ver_major) const {
	return (p_type == "Texture" && ver_major <= 3) || (p_type == "Texture2D") || (p_type == "StreamTexture") || (p_type == "CompressedTexture2D");
}

Ref<Resource> ResourceConverterTexture2D::convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error) {
	String name;
	Ref<Resource> texture;

	if (p_type == ResourceInfo::LoadType::NON_GLOBAL_LOAD) {
		return res;
	}
	auto info = ResourceInfo::get_info_from_resource(res);
	ERR_FAIL_COND_V_MSG(!info.is_valid(), res, "Missing resource has no compat metadata??????????? This should have been set by the missing resource instance function(s)!!!!!!!!");
	String type = info->type;
	int flags = res->get("flags");
	String load_path = res->get("load_path");
	if (res->get("load_path").get_type() == Variant::NIL) {
		auto res = Ref<CompressedTexture2D>(memnew(CompressedTexture2D));
		info->set_on_resource(res);
		return res;
	}
	if (p_type == ResourceInfo::GLTF_LOAD || p_type == ResourceInfo::REAL_LOAD) {
		texture = ResourceCompatLoader::custom_load(load_path, type, p_type, r_error, false, ResourceFormatLoader::CACHE_MODE_IGNORE);
	}
	ERR_FAIL_COND_V_MSG(texture.is_null(), res, "Failed to load texture " + load_path);
	Ref<ResourceInfo> existing_dict = ResourceInfo::get_info_from_resource(texture);
	if (existing_dict.is_valid()) {
		existing_dict = merge_resource_info(existing_dict, info, flags);
		existing_dict->set_on_resource(texture);
	} else {
		WARN_PRINT("ResourceInfo is not valid for MissingResource???!1!!!!!1111!");
		info->set_on_resource(texture);
	}
	return texture;
}

// get recognized extensions
void ResourceFormatLoaderCompatTexture2D::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("stex");
	p_extensions->push_back("ctex");
}

// handles type
bool ResourceFormatLoaderCompatTexture2D::handles_type(const String &p_type) const {
	return p_type == "CompressedTexture2D" || p_type == "StreamTexture" || p_type == "Texture2D" || p_type == "Texture";
}

// get resource type
String ResourceFormatLoaderCompatTexture2D::get_resource_type(const String &p_path) const {
	Error err;
	String type = TextureLoaderCompat::get_type_name_from_textype(TextureLoaderCompat::recognize(p_path, &err));
	return type;
}

Ref<CompressedTexture2D> ResourceFormatLoaderCompatTexture2D::_set_tex(const String &p_path, ResourceInfo::LoadType p_type, int tw, int th, int tw_custom, int th_custom, int flags, Ref<Image> image) {
	Ref<CompressedTexture2D> texture;
	Ref<OverrideTexture2D> override_texture;
	if (p_type != ResourceInfo::LoadType::REAL_LOAD) {
		override_texture.instantiate();
		override_texture->image = image;
		texture = override_texture;
	} else {
		texture.instantiate();
	}
	faketex2D *fake = reinterpret_cast<faketex2D *>(texture.ptr());
	fake->w = tw_custom ? tw_custom : tw;
	fake->h = th_custom ? th_custom : th;
	fake->format = image->get_format();
	fake->path_to_file = p_path;
	bool size_override = tw_custom || th_custom;
	if (p_type == ResourceInfo::LoadType::REAL_LOAD) {
		RID texture_rid = RS::get_singleton()->texture_2d_create(image);
		fake->texture = texture_rid;
		if (size_override) {
			RS::get_singleton()->texture_set_size_override(texture_rid, fake->w, fake->h);
		}
	}
	return texture;
}

Ref<ResourceInfo> ResourceFormatLoaderCompatTexture2D::get_resource_info(const String &p_path, Error *r_error) const {
	return TextureLoaderCompat::get_resource_info(p_path, r_error);
}

Ref<ResourceInfo> ResourceFormatLoaderCompatTexture3D::get_resource_info(const String &p_path, Error *r_error) const {
	return TextureLoaderCompat::get_resource_info(p_path, r_error);
}

Ref<ResourceInfo> ResourceFormatLoaderCompatTextureLayered::get_resource_info(const String &p_path, Error *r_error) const {
	return TextureLoaderCompat::get_resource_info(p_path, r_error);
}

Ref<ResourceInfo> TextureLoaderCompat::get_resource_info(const String &p_path, Error *r_error) {
	Error err;
	TextureLoaderCompat::TextureVersionType t = TextureLoaderCompat::recognize(p_path, &err);
	if (t == TextureLoaderCompat::FORMAT_NOT_TEXTURE) {
		if (r_error) {
			*r_error = err;
		}
		return Ref<ResourceInfo>();
	}
	if (TextureLoaderCompat::is_binary_resource(t)) {
		ResourceFormatLoaderCompatBinary rlcb;
		return rlcb.get_resource_info(p_path, r_error);
	}
	return TextureLoaderCompat::_get_resource_info(p_path, t);
}

Ref<Resource> ResourceFormatLoaderCompatTexture2D::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, ResourceFormatLoader::CacheMode p_cache_mode) {
	return custom_load(p_path, p_original_path, get_default_real_load(), r_error);
}

Ref<Resource> ResourceFormatLoaderCompatTexture2D::custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
	Error err;
	Ref<Resource> res;
	TextureLoaderCompat::TextureVersionType t = TextureLoaderCompat::recognize(p_path, &err);
	if (t == TextureLoaderCompat::FORMAT_NOT_TEXTURE) {
		if (r_error) {
			*r_error = err;
		}
		return Ref<Resource>();
	}
	int lw, lh, lwc, lhc, lflags;
	int data_format;
	int texture_flags;
	Ref<Resource> texture;
	Ref<Image> image;
	if (t == TextureLoaderCompat::FORMAT_V3_STREAM_TEXTURE2D) {
		err = TextureLoaderCompat::_load_data_stex2d_v3(p_path, lw, lh, lwc, lhc, lflags, image);
	} else if (t == TextureLoaderCompat::FORMAT_V4_COMPRESSED_TEXTURE2D) {
		err = TextureLoaderCompat::_load_data_ctex2d_v4(p_path, lw, lh, lwc, lhc, image, data_format, texture_flags);
	} else {
		err = ERR_INVALID_PARAMETER;
	}
	if (r_error) {
		*r_error = err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, Ref<Resource>(), "Failed to load texture " + p_path);
	texture = _set_tex(p_path, p_type, lw, lh, lwc, lhc, lflags, image);
	set_res_path(texture, p_original_path.is_empty() ? p_path : p_original_path, p_type, p_cache_mode);
	auto info = TextureLoaderCompat::_get_resource_info(p_original_path.is_empty() ? p_path : p_original_path, t);
	info->cached_id = p_path;
	info->extra["data_format"] = data_format;
	info->extra["texture_flags"] = texture_flags;
	info->set_on_resource(texture);
	return texture;
}

Ref<Resource> ResourceFormatLoaderCompatTexture3D::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	return custom_load(p_path, p_original_path, get_default_real_load(), r_error);
}

void ResourceFormatLoaderCompatTexture3D::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("ctex3d");
	p_extensions->push_back("tex3d");
}

bool ResourceFormatLoaderCompatTexture3D::handles_type(const String &p_type) const {
	return p_type == "CompressedTexture3D" || p_type == "StreamTexture3D" || p_type == "Texture3D" || p_type == "Texture";
}

String ResourceFormatLoaderCompatTexture3D::get_resource_type(const String &p_path) const {
	Error err;
	String type = TextureLoaderCompat::get_type_name_from_textype(TextureLoaderCompat::recognize(p_path, &err));
	return type;
}

Ref<CompressedTexture3D> ResourceFormatLoaderCompatTexture3D::_set_tex(const String &p_path, ResourceInfo::LoadType p_type, int tw, int th, int td, bool mipmaps, const Vector<Ref<Image>> &images) {
	Ref<CompressedTexture3D> texture;
	Ref<OverrideTexture3D> override_texture;
	if (p_type != ResourceInfo::LoadType::REAL_LOAD) {
		override_texture.instantiate();
		override_texture->data = images;
		texture = override_texture;
	} else {
		texture.instantiate();
	}
	faketex3D *fake = reinterpret_cast<faketex3D *>(texture.ptr());
	fake->w = tw;
	fake->h = th;
	fake->d = td;
	fake->format = images[0]->get_format();
	fake->path_to_file = p_path;
	fake->mipmaps = mipmaps;
	if (p_type == ResourceInfo::LoadType::REAL_LOAD) {
		RID texture_rid = RS::get_singleton()->texture_3d_create(texture->get_format(), texture->get_width(), texture->get_height(), texture->get_depth(), texture->has_mipmaps(), images);
		fake->texture = texture_rid;
	}
	return texture;
}

Ref<Resource> ResourceFormatLoaderCompatTexture3D::custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
	Error err;
	TextureLoaderCompat::TextureVersionType t = TextureLoaderCompat::recognize(p_path, &err);
	if (t == TextureLoaderCompat::FORMAT_NOT_TEXTURE) {
		if (r_error) {
			*r_error = err;
		}
		return Ref<Resource>();
	}

	int lw, lh, ld, ltype;
	bool mipmaps;
	Ref<Resource> texture;
	Vector<Ref<Image>> images;
	Image::Format fmt;
	int data_format = 0;
	int texture_flags = 0;
	if (t == TextureLoaderCompat::FORMAT_V3_STREAM_TEXTURE3D) {
		err = TextureLoaderCompat::_load_layered_texture_v3(p_path, images, fmt, lw, lh, ld, mipmaps);
	} else if (t == TextureLoaderCompat::FORMAT_V4_COMPRESSED_TEXTURE3D) {
		err = TextureLoaderCompat::_load_data_ctexlayered_v4(p_path, images, fmt, lw, lh, ld, ltype, mipmaps, data_format);
	} else {
		err = ERR_INVALID_PARAMETER;
	}
	if (r_error) {
		*r_error = err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, Ref<Resource>(), "Failed to load texture " + p_path);
	texture = _set_tex(p_path, p_type, lw, lh, ld, mipmaps, images);
	set_res_path(texture, p_original_path.is_empty() ? p_path : p_original_path, p_type, p_cache_mode);
	auto info = TextureLoaderCompat::_get_resource_info(p_original_path.is_empty() ? p_path : p_original_path, t);
	info->extra["data_format"] = data_format;
	info->extra["texture_flags"] = texture_flags;
	info->set_on_resource(texture);
	return texture;
}

Ref<Resource> ResourceFormatLoaderCompatTextureLayered::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	return custom_load(p_path, p_original_path, get_default_real_load(), r_error);
}

void ResourceFormatLoaderCompatTextureLayered::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("ctexarray");
	p_extensions->push_back("ccube");
	p_extensions->push_back("ccubearray");
	p_extensions->push_back("texarr");
}

bool ResourceFormatLoaderCompatTextureLayered::handles_type(const String &p_type) const {
	return p_type == "StreamTextureArray" || p_type == "CompressedTexture2DArray" || p_type == "CompressedCubemap" || p_type == "CompressedCubemapArray" || p_type == "TextureArray" || p_type == "Texture";
}

String ResourceFormatLoaderCompatTextureLayered::get_resource_type(const String &p_path) const {
	Error err;
	String type = TextureLoaderCompat::get_type_name_from_textype(TextureLoaderCompat::recognize(p_path, &err));
	return type;
}

Ref<CompressedTextureLayered> ResourceFormatLoaderCompatTextureLayered::_set_tex(const String &p_path, ResourceInfo::LoadType p_type, int tw, int th, int td, int type, bool mipmaps, const Vector<Ref<Image>> &images) {
	Ref<CompressedTextureLayered> texture;
	if (p_type != ResourceInfo::LoadType::REAL_LOAD) {
		if (type == RS::TEXTURE_LAYERED_2D_ARRAY) {
			Ref<OverrideTextureLayered<CompressedTexture2DArray>> override_texture;
			override_texture.instantiate();
			override_texture->layer_data = images;
			texture = override_texture;
		} else if (type == RS::TEXTURE_LAYERED_CUBEMAP) {
			Ref<OverrideTextureLayered<CompressedCubemap>> override_texture;
			override_texture.instantiate();
			override_texture->layer_data = images;
			texture = override_texture;
		} else if (type == RS::TEXTURE_LAYERED_CUBEMAP_ARRAY) {
			Ref<OverrideTextureLayered<CompressedCubemapArray>> override_texture;
			override_texture.instantiate();
			override_texture->layer_data = images;
			texture = override_texture;
		}
	} else {
		if (type == RS::TEXTURE_LAYERED_2D_ARRAY) {
			texture = memnew(CompressedTexture2DArray);
		} else if (type == RS::TEXTURE_LAYERED_CUBEMAP) {
			texture = memnew(CompressedCubemap);
		} else if (type == RS::TEXTURE_LAYERED_CUBEMAP_ARRAY) {
			texture = memnew(CompressedCubemapArray);
		}
	}
	faketexlayered *fake = reinterpret_cast<faketexlayered *>(texture.ptr());
	fake->w = tw;
	fake->h = th;
	fake->layers = td;
	fake->format = images[0]->get_format();
	fake->path_to_file = p_path;
	fake->mipmaps = mipmaps;
	fake->layered_type = TextureLayered::LayeredType(type);
	if (p_type == ResourceInfo::LoadType::REAL_LOAD) {
		RID texture_rid = RS::get_singleton()->texture_2d_layered_create(images, RS::TextureLayeredType(type));
		fake->texture = texture_rid;
	}
	return texture;
}

Ref<Resource> ResourceFormatLoaderCompatTextureLayered::custom_load(const String &p_path, const String &p_original_path, ResourceInfo::LoadType p_type, Error *r_error, bool use_threads, ResourceFormatLoader::CacheMode p_cache_mode) {
	Error err;
	Ref<Resource> res;
	TextureLoaderCompat::TextureVersionType t = TextureLoaderCompat::recognize(p_path, &err);
	if (t == TextureLoaderCompat::FORMAT_NOT_TEXTURE) {
		if (r_error) {
			*r_error = err;
		}
		return Ref<Resource>();
	}

	int data_format;
	int texture_flags = 0;
	int lw, lh, ld, ltype;
	bool mipmaps;
	Ref<Resource> texture;
	Vector<Ref<Image>> images;
	Image::Format fmt;
	if (t == TextureLoaderCompat::FORMAT_V3_STREAM_TEXTUREARRAY) {
		err = TextureLoaderCompat::_load_layered_texture_v3(p_path, images, fmt, lw, lh, ld, mipmaps);
		ltype = RS::TEXTURE_LAYERED_2D_ARRAY;
	} else if (t == TextureLoaderCompat::FORMAT_V4_COMPRESSED_TEXTURELAYERED) {
		err = TextureLoaderCompat::_load_data_ctexlayered_v4(p_path, images, fmt, lw, lh, ld, ltype, mipmaps, data_format);
	} else {
		err = ERR_INVALID_PARAMETER;
	}
	if (r_error) {
		*r_error = err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, Ref<Resource>(), "Failed to load texture " + p_path);
	texture = _set_tex(p_path, p_type, lw, lh, ld, ltype, mipmaps, images);
	set_res_path(texture, p_original_path.is_empty() ? p_path : p_original_path, p_type, p_cache_mode);
	auto info = TextureLoaderCompat::_get_resource_info(p_original_path.is_empty() ? p_path : p_original_path, t);
	info->extra["data_format"] = data_format;
	info->extra["texture_flags"] = texture_flags;
	info->set_on_resource(texture);
	return texture;
}

bool ImageTextureConverterCompat::handles_type(const String &p_type, int ver_major) const {
	return p_type == "ImageTexture";
}

Ref<Resource> ImageTextureConverterCompat::convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error) {
	String name;
	Vector2 size;
	int tw = 0;
	int th = 0;
	int tw_custom = 0;
	int th_custom = 0;
	int flags = 0;
	Ref<Image> image;
	Ref<Resource> texture;
	auto info = ResourceInfo::get_info_from_resource(res);
	ERR_FAIL_COND_V_MSG(!info.is_valid(), res, "Missing resource has no compat metadata??????????? This should have been set by the missing resource instance function(s)!!!!!!!!");
	String type = info->type;

	auto convert_image = [&](const Ref<Resource> &image_res) -> Ref<Image> {
		Ref<Image> img = image_res;
		if (img.is_null() && image_res->get_class() == "MissingResource") {
			ImageConverterCompat ic;
			if (ic.handles_type("Image", ver_major)) {
				img = ic.convert(image_res, p_type, ver_major, r_error);
			}
		}
		return img;
	};
	ERR_FAIL_COND_V_MSG(type != "ImageTexture", res, "Unsupported type: " + type);
	name = get_resource_name(res, ver_major);
	image = convert_image(res->get("image"));
	ERR_FAIL_COND_V_MSG(image.is_null(), res, "Cannot load image from ImageTexture resource '" + name + "'.");

	size = res->get("size");
	flags = res->get("flags");
	bool mipmaps = flags & 1 || image->has_mipmaps();

	image->set_name(name);
	tw = image->get_width();
	th = image->get_height();
	if (size.width && tw != size.width) {
		tw_custom = size.width;
	}
	if (size.height && th != size.height) {
		th_custom = size.height;
	}
	texture = TextureLoaderCompat::create_image_texture(res->get_path(), p_type, tw, th, tw_custom, th_custom, mipmaps, image);
	TextureLoaderCompat::TextureVersionType t = (ver_major >= 4 ? TextureLoaderCompat::FORMAT_V4_COMPRESSED_TEXTURE2D : (ver_major == 3 ? TextureLoaderCompat::FORMAT_V3_IMAGE_TEXTURE : TextureLoaderCompat::FORMAT_V2_IMAGE_TEXTURE));
	auto new_info = TextureLoaderCompat::_get_resource_info(res->get_path(), t);
	if (info.is_valid()) {
		new_info = merge_resource_info(new_info, info, flags);
		new_info->set_on_resource(texture);
	} else {
		new_info->set_on_resource(texture);
	}

	return texture;
}

bool ImageConverterCompat::handles_type(const String &p_type, int ver_major) const {
	return (p_type == "Image") && ver_major == 3;
}

Ref<Resource> ImageConverterCompat::convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error) {
	String name;
	Ref<Image> image;
	auto info = ResourceInfo::get_info_from_resource(res);
	ERR_FAIL_COND_V_MSG(!info.is_valid(), res, "Missing resource has no compat metadata??????????? This should have been set by the missing resource instance function(s)!!!!!!!!");
	String type = info->type;
	if (type != "Image") {
		WARN_PRINT("ImageConverterCompat: Unsupported type: " + type);
		return res;
	}
	name = get_resource_name(res, ver_major);
	Dictionary data = res->get("data");
	int tw = data.get("width", 0);
	int th = data.get("height", 0);
	String format = data.get("format", "");
	auto fmt_enum = ImageEnumCompat::convert_image_format_enum_v3_to_v4(ImageEnumCompat::get_v3_format_enum_from_name(format));
	if (fmt_enum == Image::FORMAT_MAX) {
		*r_error = ERR_UNAVAILABLE;
		ERR_FAIL_V_MSG(res, "Deprecated v3 image format: " + format);
	}
	bool mipmaps = data.get("mipmaps", false);
	Vector<uint8_t> img_data = data.get("data", Vector<uint8_t>());
	image = Image::create_from_data(tw, th, mipmaps, fmt_enum, img_data);
	image->set_name(name);
	if (info.is_valid()) {
		info->set_on_resource(image);
	}
	return image;
}

class OverrideImageTexture : public ImageTexture {
public:
	Ref<Image> image;
	virtual Ref<Image> get_image() const override {
		// otherwise, call the parent
		return image;
	}
	virtual String get_save_class() const override {
		return "ImageTexture";
	}
};

class fakeimagetex : Texture2D {
	GDCLASS(fakeimagetex, Texture2D);

public:
	mutable RID texture;
	Image::Format format = Image::FORMAT_L8;
	bool mipmaps = false;
	int w = 0;
	int h = 0;
	Size2 size_override;
	mutable Ref<BitMap> alpha_cache;
	bool image_stored = false;
};
static_assert(sizeof(fakeimagetex) == sizeof(ImageTexture), "fakeimagetex must be the same size as ImageTexture");

Ref<ImageTexture> TextureLoaderCompat::create_image_texture(const String &p_path, ResourceInfo::LoadType p_type, int tw, int th, int tw_custom, int th_custom, bool mipmaps, Ref<Image> image) {
	Ref<ImageTexture> texture;
	Ref<OverrideImageTexture> override_texture;
	if (p_type != ResourceInfo::LoadType::REAL_LOAD) {
		override_texture.instantiate();
		override_texture->image = image;
		texture = override_texture;
	} else {
		texture.instantiate();
	}
	fakeimagetex *fake = reinterpret_cast<fakeimagetex *>(texture.ptr());
	fake->image_stored = true;
	fake->w = tw;
	fake->h = th;
	fake->format = image->get_format();
	if (tw_custom || th_custom) {
		fake->size_override = Size2(tw_custom, th_custom);
	}
	fake->mipmaps = mipmaps;
	bool size_override = tw_custom || th_custom;
	if (p_type == ResourceInfo::LoadType::REAL_LOAD) {
		RID texture_rid = RS::get_singleton()->texture_2d_create(image);
		fake->texture = texture_rid;
		if (size_override) {
			RS::get_singleton()->texture_set_size_override(texture_rid, fake->w, fake->h);
		}
	}
	return texture;
}
struct Piece {
	Point2 offset;
	Ref<Texture2D> texture;
};

struct CustomPieceSort {
	static bool compare(const Piece &a, const Piece &b) {
		// [0], [1], [2], [3]
		// [4], [5], [6], [7]
		if (a.offset.y != b.offset.y) {
			return a.offset.y < b.offset.y;
		}
		return a.offset.x < b.offset.x;
	}

	bool operator()(const Piece &a, const Piece &b) const {
		return compare(a, b);
	}
};

Ref<Resource> LargeTextureConverterCompat::convert(const Ref<MissingResource> &res, ResourceInfo::LoadType p_type, int ver_major, Error *r_error) {
	Ref<CompressedTexture2DArray> texture;
	Array data = res->get("_data");
	Ref<ResourceInfo> info = ResourceInfo::get_info_from_resource(res);
	Vector<Piece> pieces;
	Size2i max_piece_size = Size2i(0, 0);
	// last element is the whole size
	for (int i = 0; i < data.size() - 1; i += 2) {
		Point2 offset = data[i];
		Ref<Resource> texture_res = data[i + 1];
		Ref<MissingResource> missing_res = texture_res;
		Ref<Texture2D> image_texture;
		if (missing_res.is_valid() && missing_res->get_original_class() == "ImageTexture") {
			ImageTextureConverterCompat ic;
			image_texture = ic.convert(missing_res, p_type, ver_major, r_error);
			ERR_FAIL_COND_V_MSG(!image_texture.is_valid(), Ref<Resource>(), "LargeTextureConverterCompat: Failed to convert ImageTexture in array data of LargeTexture " + res->get_path());
		} else {
			image_texture = texture_res;
		}
		ERR_FAIL_COND_V_MSG(!image_texture.is_valid(), Ref<Resource>(), "LargeTextureConverterCompat: Failed to convert ImageTexture in array data of LargeTexture " + res->get_path());
		auto image_size = image_texture->get_image()->get_size();
		max_piece_size.x = MAX(max_piece_size.x, image_size.x);
		max_piece_size.y = MAX(max_piece_size.y, image_size.y);
		pieces.push_back({ offset, image_texture });
	}
	pieces.sort_custom<CustomPieceSort>();
	Vector<Ref<Image>> images;
	Vector2 whole_size = data[data.size() - 1];
	Vector<Vector2> offsets;
	// LargeTextures leave gaps where the whole texture is transparent, so we need to find the portions of the whole texture that we actually have pieces for
	int64_t expected_x = 0;
	int64_t expected_y = 0;
	auto pos = Point2(0, 0);

	for (int i = 0; i < pieces.size(); i++) {
		auto image = pieces[i].texture->get_image();
		pos = pieces[i].offset;
		while (pos.x != expected_x || pos.y != expected_y) {
			Size2i gap_size = max_piece_size;
			if (expected_x + gap_size.x > whole_size.x) {
				gap_size.x = whole_size.x - expected_x;
			}
			if (expected_y + gap_size.y > whole_size.y) {
				gap_size.y = whole_size.y - expected_y;
			}
			offsets.push_back(Vector2(expected_x, expected_y));
			// create a new image with the size of the gap
			Ref<Image> gap_image = Image::create_empty(gap_size.x, gap_size.y, image->has_mipmaps(), image->get_format());
			images.push_back(gap_image);
			expected_x += gap_size.x;
			if (expected_x >= whole_size.x) {
				expected_x = 0;
				expected_y += max_piece_size.y;
			}
		}
		expected_x = pos.x + image->get_width();
		if (expected_x >= whole_size.x) {
			expected_x = 0;
			expected_y += max_piece_size.y;
		}
		offsets.push_back(pos);
		images.push_back(image);
	}

	while (expected_y < whole_size.y) {
		Size2i gap_size = max_piece_size;
		if (expected_x + gap_size.x > whole_size.x) {
			gap_size.x = whole_size.x - expected_x;
		}
		if (expected_y + gap_size.y > whole_size.y) {
			gap_size.y = whole_size.y - expected_y;
		}
		offsets.push_back(Vector2(expected_x, expected_y));
		// create a new image with the size of the gap
		Ref<Image> gap_image = Image::create_empty(gap_size.x, gap_size.y, images[0]->has_mipmaps(), images[0]->get_format());
		images.push_back(gap_image);
		expected_x += gap_size.x;
		if (expected_x >= whole_size.x) {
			expected_x = 0;
			expected_y += max_piece_size.y;
		}
	}

	texture = ResourceFormatLoaderCompatTextureLayered::_set_tex(res->get_path(), p_type, whole_size.x, whole_size.y, images.size(), RS::TEXTURE_LAYERED_2D_ARRAY, false, images);
	auto new_info = TextureLoaderCompat::_get_resource_info(res->get_path(), TextureLoaderCompat::FORMAT_V2_LARGE_TEXTURE);
	new_info->extra["offsets"] = offsets;
	new_info->extra["whole_size"] = whole_size;
	if (info.is_valid()) {
		new_info = merge_resource_info(new_info, info, 0);
	}
	new_info->set_on_resource(texture);
	return texture;
}

bool LargeTextureConverterCompat::handles_type(const String &p_type, int ver_major) const {
	return p_type == "LargeTexture";
}