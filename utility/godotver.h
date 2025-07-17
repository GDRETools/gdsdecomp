/*************************************************************************/
/*  godotver.h                                                           */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef GODOTVER_H
#define GODOTVER_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_REGEX_ENABLED
#include "modules/regex/regex.h"
#endif
// <sys/sysmacros.h> is included somewhere, which defines major(dev) to gnu_dev_major(dev)
#if defined(major)
#undef major
#endif
#if defined(minor)
#undef minor
#endif

class SemVer : public RefCounted {
	GDCLASS(SemVer, RefCounted);

protected:
	friend void init_ver_regex();
	friend void free_ver_regex();
	int major = 0;
	int minor = 0;
	int patch = 0;
	String prerelease;
	String build_metadata;
	bool valid = false;
	bool is_strict = true;

	enum _TYPE {
		STRICT,
		WINDOWS,
		GODOT
	};
	virtual _TYPE get_type() const { return STRICT; }
	static bool parse_digit_only_field(const String &p_field, uint64_t &r_result);
	static void _bind_methods();

	// this will match: "1.2.3" "1.1.3-pre.1" "4.2.0-rc.1+sha.md5.f9300dc53"
	// we strip "v" in front of the string in case it's there
	static constexpr const char *strict_regex_str = "^[vV]?(?P<major>0|[1-9]\\d*)\\.(?P<minor>0|[1-9]\\d*)\\.(?P<patch>0|[1-9]\\d*)(?:-(?P<prerelease>(?:0|[1-9]\\d*|\\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\\.(?:0|[1-9]\\d*|\\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\\.[0-9a-zA-Z-]+)*))?$";
	static Ref<RegEx> strict_regex;

public:
	virtual String as_text() const;
	virtual int cmp(const Ref<SemVer> &p_b) const;
	// strict comparison; if the build data is different, the versions are considered different
	bool eq(const Ref<SemVer> &b) const {
		return cmp(b) == 0;
	}

	bool neq(const Ref<SemVer> &b) const {
		return !eq(b);
	}

	bool lt(const Ref<SemVer> &b) const {
		return cmp(b) < 0;
	}

	bool lte(const Ref<SemVer> &b) const {
		return cmp(b) <= 0;
	}

	bool gt(const Ref<SemVer> &b) const {
		return cmp(b) > 0;
	}

	bool gte(const Ref<SemVer> &b) const {
		return cmp(b) >= 0;
	}

	bool is_exact_match(const Ref<SemVer> &b) const {
		return b.is_valid() && as_text() == b->as_text();
	}

	bool is_not_exact_match(const Ref<SemVer> &b) const {
		return !is_exact_match(b);
	}

	bool operator==(const Ref<SemVer> &b) const {
		return eq(b);
	}

	bool operator!=(const Ref<SemVer> &b) const {
		return neq(b);
	}

	bool operator<(const Ref<SemVer> &b) const {
		return lt(b);
	}

	bool operator<=(const Ref<SemVer> &b) const {
		return lte(b);
	}

	bool operator>(const Ref<SemVer> &b) const {
		return gt(b);
	}

	bool operator>=(const Ref<SemVer> &b) const {
		return gte(b);
	}

	operator String() const { return as_text(); };
	virtual String to_string() override;
	virtual String as_tag() const { return as_text(); }

	bool is_prerelease() { return prerelease != ""; }

	int get_major() const { return major; }
	int get_minor() const { return minor; }
	int get_patch() const { return patch; }
	String get_prerelease() const { return prerelease; }
	String get_build_metadata() const { return build_metadata; }

	void set_major(int p_par) {
		major = p_par;
		valid = true;
	}
	void set_minor(int p_par) {
		minor = p_par;
		valid = true;
	}
	void set_patch(int p_par) {
		patch = p_par;
		valid = true;
	}
	void set_prerelease(String p_par) {
		prerelease = p_par;
		valid = true;
	}
	void set_build_metadata(String p_par) {
		build_metadata = p_par;
		valid = true;
	}

	bool is_valid_semver() const { return valid; }

	bool patch_compatible(const Ref<SemVer> &p_b) const {
		return major == p_b->major && minor == p_b->minor;
	}

	static bool parse_valid(const String &p_ver_text, Ref<SemVer> &r_semver);
	static Ref<SemVer> parse(const String &p_ver_text);
	static Ref<SemVer> create(int p_major, int p_minor, int p_patch,
			const String &p_prerelease = "", const String &p_build_metadata = "");
	SemVer() {}
	SemVer(const String &p_ver_text) {
		auto val = SemVer::parse(p_ver_text);
		if (val.is_valid() && val->is_valid_semver()) {
			major = val->major;
			minor = val->minor;
			patch = val->patch;
			prerelease = val->prerelease;
			build_metadata = val->build_metadata;
			valid = true;
		}
	}
	SemVer(int p_major, int p_minor, int p_patch,
			const String &p_prerelease = "", const String &p_build_metadata = "") :
			major(p_major),
			minor(p_minor),
			patch(p_patch),
			prerelease(p_prerelease),
			build_metadata(p_build_metadata) {
		valid = true;
	}
};

class GodotVer : public SemVer {
	GDCLASS(GodotVer, SemVer);

protected:
	friend void init_ver_regex();
	friend void free_ver_regex();
	virtual int cmp(const Ref<SemVer> &p_b) const override;
	static void _bind_methods();

	// this will match: "v1" "2" "1.1" "2.4.stable" "1.4.5.6" "3.4.0.stable" "3.4.5.stable.official.f9ac000d5"
	// if this is not a Windows-type version number ("1.4.5.6"), then everything after the patch number will be build metadata as we can't use it for precedence.
	static constexpr const char *non_strict_regex_str = R"(^[vV]?(?P<major>0|[1-9]\d*)(?:\.(?P<minor>0|[1-9]\d*))?(?:\.(?P<patch>0|[1-9]\d*))?(?:[\.-](?P<prerelease>(?:dev|alpha|beta|rc)\d*))?(?:[\.+-](?P<buildmetadata>(?:[\w\-_\+\.]*)))?$)";

	static Ref<RegEx> non_strict_regex;

public:
	static Ref<GodotVer> copy_from(const Ref<GodotVer> &p_ver);
	virtual String as_text() const override;
	// outputs a string that can be used as a tag to look up the release on github
	virtual String as_tag() const override;
	bool is_not_custom_build();
	static bool parse_valid(const String &p_ver_text, Ref<GodotVer> &r_semver);
	static Ref<GodotVer> parse(const String &p_ver_text);
	static Ref<GodotVer> create(int p_major, int p_minor, int p_patch,
			const String &p_prerelease = "", const String &p_build_metadata = "");
	GodotVer() {
		is_strict = false;
	}
	GodotVer(const String &p_ver_text) {
		auto val = GodotVer::parse(p_ver_text);
		if (val.is_valid() && val->is_valid_semver()) {
			major = val->major;
			minor = val->minor;
			patch = val->patch;
			prerelease = val->prerelease;
			build_metadata = val->build_metadata;
			valid = true;
		}
	}
	GodotVer(int p_major, int p_minor, int p_patch,
			const String &p_prerelease = "", const String &p_build_metadata = "") :
			SemVer(p_major, p_minor, p_patch, p_prerelease, p_build_metadata) {
		is_strict = false;
	}
};

#endif // GODOTVER_H
