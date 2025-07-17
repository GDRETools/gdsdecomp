/*************************************************************************/
/*  godotver.cpp                                                         */
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

#include "godotver.h"
#include "modules/regex/regex.h"

// initialized by register_types.cpp
Ref<RegEx> SemVer::strict_regex = nullptr;
Ref<RegEx> GodotVer::non_strict_regex = nullptr;

bool SemVer::parse_digit_only_field(const String &p_field, uint64_t &r_result) {
	if (p_field.is_empty()) {
		return false;
	}

	int64_t integer = 0;

	for (int i = 0; i < p_field.length(); i++) {
		char32_t c = p_field[i];
		if (is_digit(c)) {
			bool overflow = ((uint64_t)integer > UINT64_MAX / 10) || ((uint64_t)integer == UINT64_MAX / 10 && c > '5');
			ERR_FAIL_COND_V_MSG(overflow, false, "Cannot represent '" + p_field + "' as a 64-bit unsigned integer, since the value is too large.");
			integer *= 10;
			integer += c - '0';
		} else {
			return false;
		}
	}

	r_result = (uint64_t)integer;
	return true;
}

int SemVer::cmp(const Ref<SemVer> &p_b) const {
	if (p_b.is_null()) {
		return 1;
	}
	if (major != p_b->major) {
		return major > p_b->major ? 1 : -1;
	}

	if (minor != p_b->minor) {
		return minor > p_b->minor ? 1 : -1;
	}

	if (patch != p_b->patch) {
		return patch > p_b->patch ? 1 : -1;
	}

	if (prerelease.is_empty() && p_b->prerelease.is_empty()) {
		return 0;
	}

	if (prerelease.is_empty() || p_b->prerelease.is_empty()) {
		return prerelease.is_empty() ? 1 : -1;
	}

	if (prerelease != p_b->prerelease) {
		// This could be optimized, but I'm too lazy

		Vector<String> a_field_set = prerelease.split(".");
		Vector<String> b_field_set = p_b->prerelease.split(".");

		int a_field_count = a_field_set.size();
		int b_field_count = b_field_set.size();

		int min_field_count = MIN(a_field_count, b_field_count);

		for (int i = 0; i < min_field_count; i++) {
			const String &a_field = a_field_set[i];
			const String &b_field = b_field_set[i];

			if (a_field == b_field) {
				continue;
			}

			uint64_t a_num;
			bool a_is_digit_only = parse_digit_only_field(a_field, a_num);

			uint64_t b_num;
			bool b_is_digit_only = parse_digit_only_field(b_field, b_num);

			if (a_is_digit_only && b_is_digit_only) {
				// Identifiers consisting of only digits are compared numerically.

				if (a_num == b_num) {
					continue;
				}

				return a_num > b_num ? 1 : -1;
			}

			if (a_is_digit_only || b_is_digit_only) {
				// Numeric identifiers always have lower precedence than non-numeric identifiers.
				return b_is_digit_only ? 1 : -1;
			}

			// Identifiers with letters or hyphens are compared lexically in ASCII sort order.
			return a_field > b_field ? 1 : -1;
		}

		if (a_field_count != b_field_count) {
			// A larger set of pre-release fields has a higher precedence than a smaller set, if all of the preceding identifiers are equal.
			return a_field_count > b_field_count ? 1 : -1;
		}
	}

	return 0;
}

void SemVer::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("parse_semver", "ver_text"), &SemVer::parse);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("create_semver", "major", "minor", "patch", "prerelease", "build_metadata"), &SemVer::create, DEFVAL(""), DEFVAL(""));
	ClassDB::bind_method(D_METHOD("is_valid_semver"), &SemVer::is_valid_semver);
	ClassDB::bind_method(D_METHOD("eq", "other"), &SemVer::eq);
	ClassDB::bind_method(D_METHOD("neq", "other"), &SemVer::neq);
	ClassDB::bind_method(D_METHOD("gt", "other"), &SemVer::gt);
	ClassDB::bind_method(D_METHOD("gte", "other"), &SemVer::gte);
	ClassDB::bind_method(D_METHOD("lt", "other"), &SemVer::lt);
	ClassDB::bind_method(D_METHOD("lte", "other"), &SemVer::lte);

	ClassDB::bind_method(D_METHOD("get_major"), &SemVer::get_major);
	ClassDB::bind_method(D_METHOD("get_minor"), &SemVer::get_minor);
	ClassDB::bind_method(D_METHOD("get_patch"), &SemVer::get_patch);
	ClassDB::bind_method(D_METHOD("get_prerelease"), &SemVer::get_prerelease);
	ClassDB::bind_method(D_METHOD("get_build_metadata"), &SemVer::get_build_metadata);

	ClassDB::bind_method(D_METHOD("set_major", "major"), &SemVer::set_major);
	ClassDB::bind_method(D_METHOD("set_minor", "minor"), &SemVer::set_minor);
	ClassDB::bind_method(D_METHOD("set_patch", "patch"), &SemVer::set_patch);
	ClassDB::bind_method(D_METHOD("set_prerelease", "prerelease"), &SemVer::set_prerelease);
	ClassDB::bind_method(D_METHOD("set_build_metadata", "build_metadata"), &SemVer::set_build_metadata);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "major"), "set_major", "get_major");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "minor"), "set_minor", "get_minor");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "patch"), "set_patch", "get_patch");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "prerelease"), "set_prerelease", "get_prerelease");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "build_metadata"), "set_build_metadata", "get_build_metadata");

	ClassDB::bind_method(D_METHOD("is_prerelease"), &SemVer::is_prerelease);

	ClassDB::bind_method(D_METHOD("as_text"), &SemVer::as_text);
	ClassDB::bind_method(D_METHOD("as_tag"), &SemVer::as_tag);
}

bool SemVer::parse_valid(const String &p_ver_text, Ref<SemVer> &r_semver) {
#ifdef MODULE_REGEX_ENABLED
	Ref<RegExMatch> match = SemVer::strict_regex->search(p_ver_text);

	if (match.is_valid()) {
		r_semver = SemVer::create(
				match->get_string("major").to_int(),
				match->get_string("minor").to_int(),
				match->get_string("patch").to_int(),
				match->get_string("prerelease"),
				match->get_string("buildmetadata"));
		return true;
	}
	return false;
#else
	String ver_text = p_ver_text;
	// strip leading "v" or "V"
	if (p_ver_text.begins_with("v") || p_ver_text.begins_with("V")) {
		ver_text = p_ver_text.substr(1);
	}
	Vector<String> ver_numbers = ver_text.split(".", false, 3);
	int major = 0;
	int minor = 0;
	int patch = 0;
	// We should have exactly 3 splits, and the first two should be numbers
	if ((ver_numbers.size() == 3 && ver_numbers[0].is_valid_int() && ver_numbers[1].is_valid_int())) {
		major = ver_numbers[0].to_int();
		minor = ver_numbers[1].to_int();
	} else {
		return false;
	}
	String prerelease;
	String buildmeta;
	bool is_windows_style = false;
	// we may have pre-release or patch numbers
	String leftover = ver_numbers.size() == 3 ? ver_numbers[2] : "";
	bool is_non_strict = false;
	if (!leftover.is_empty() && !leftover.is_valid_int()) {
		Vector<String> pre_comp;
		// build meta always comes after prerelease tag
		if (leftover.contains("+")) {
			pre_comp = leftover.split("+", true, 2);
			if (pre_comp.size() != 2) {
				return false;
			}
			leftover = pre_comp[0];
			buildmeta = pre_comp[1];
		}
		if (leftover.contains("-")) {
			// we can have as many dashes as we want in the prerelease string, so limit to 2 splits
			Vector<String> pre_comp = leftover.split("-", 2);
			leftover = pre_comp[0];
			prerelease = pre_comp[1];
		}
	}
	if (leftover.is_valid_int()) {
		patch = leftover.to_int();
	} else { //failed validation, what's leftover should be a number
		return false;
	}
	r_semver = SemVer::create(major, minor, patch, prerelease, buildmeta);
	return true;
#endif
}

Ref<SemVer> SemVer::parse(const String &p_ver_text) {
	Ref<SemVer> r_val;
	ERR_FAIL_COND_V_MSG(!SemVer::parse_valid(p_ver_text, r_val), Ref<SemVer>(), "Not a valid semantic version string!");
	return r_val;
}

Ref<SemVer> SemVer::create(int p_major, int p_minor, int p_patch,
		const String &p_prerelease, const String &p_build_metadata) {
	Ref<SemVer> ref = memnew(SemVer(p_major, p_minor, p_patch, p_prerelease, p_build_metadata));
	return ref;
}

String SemVer::as_text() const {
	if (!is_valid_semver()) {
		return "";
	}
	String ver_text = itos(major) + "." + itos(minor) + "." + itos(patch);
	if (!prerelease.is_empty()) {
		ver_text += "-" + prerelease;
	}
	if (!build_metadata.is_empty()) {
		ver_text += "+" + build_metadata;
	}
	return ver_text;
}

String SemVer::to_string() {
	return as_text();
}

bool parse_digit_from_godot_prerelease_field(const String &p_field, uint64_t &r_result) {
	// pre-release fields for godot go like "beta1", "rc1", "alpha20", etc.
	// we just parse the field until we find the first digit character, and then parse the rest of the field as a number until we hit another non-digit character.
	if (p_field.is_empty()) {
		return false;
	}

	int64_t integer = 0;
	bool found_int = false;
	for (int i = 0; i < p_field.length(); i++) {
		char32_t c = p_field[i];
		if (is_digit(c)) {
			found_int = true;
			bool overflow = ((uint64_t)integer > UINT64_MAX / 10) || ((uint64_t)integer == UINT64_MAX / 10 && c > '5');
			ERR_FAIL_COND_V_MSG(overflow, false, "Cannot represent '" + p_field + "' as a 64-bit unsigned integer, since the value is too large.");
			integer *= 10;
			integer += c - '0';
		} else if (found_int) {
			return false;
		}
	}

	r_result = (uint64_t)integer;
	return true;
}

int GodotVer::cmp(const Ref<SemVer> &p_b) const {
	if (!p_b.is_valid() || !p_b->is_valid_semver()) {
		return is_valid_semver() ? 1 : 0;
	} else if (!is_valid_semver()) {
		return -1;
	}

	if (major != p_b->get_major()) {
		return major > p_b->get_major() ? 1 : -1;
	}

	if (minor != p_b->get_minor()) {
		return minor > p_b->get_minor() ? 1 : -1;
	}

	if (patch != p_b->get_patch()) {
		return patch > p_b->get_patch() ? 1 : -1;
	}

	String b_prerelease = p_b->get_prerelease();
	if (prerelease.is_empty() && b_prerelease.is_empty()) {
		return 0;
	}

	if (prerelease.is_empty() || b_prerelease.is_empty()) {
		return prerelease.is_empty() ? 1 : -1;
	}

	// pre-release fields for godot go like "beta1", "rc1", "alpha20", etc.
	if (prerelease != b_prerelease) {
		bool a_is_dev = prerelease.contains("dev");
		bool b_is_dev = b_prerelease.contains("dev");
		bool a_is_alpha = prerelease.contains("alpha");
		bool b_is_alpha = b_prerelease.contains("alpha");
		bool a_is_beta = prerelease.contains("beta");
		bool b_is_beta = b_prerelease.contains("beta");
		bool a_is_rc = prerelease.contains("rc");
		bool b_is_rc = b_prerelease.contains("rc");
		// bool a_is_stable = prerelease.contains("stable");
		// bool b_is_stable = b_prerelease.contains("stable");

		bool a_valid_pfield = a_is_dev || a_is_alpha || a_is_beta || a_is_rc;
		bool b_valid_pfield = b_is_dev || b_is_alpha || b_is_beta || b_is_rc;
		// dev < alpha < beta < rc
		if (a_valid_pfield && !b_valid_pfield) {
			return -1;
		}
		if (!a_valid_pfield && b_valid_pfield) {
			return 1;
		}
		if (a_is_dev && (b_is_alpha || b_is_beta || b_is_rc)) {
			return -1;
		}
		if (a_is_alpha && (b_is_beta || b_is_rc)) {
			return -1;
		}
		if (a_is_alpha && b_is_dev) {
			return 1;
		}
		if (a_is_beta && b_is_rc) {
			return -1;
		}
		if (a_is_beta && (b_is_dev || b_is_alpha)) {
			return 1;
		}
		if (a_is_rc && (b_is_dev || b_is_alpha || b_is_beta)) {
			return 1;
		}

		// If the precedence is equal, the version with the lower version field has lower precedence.
		uint64_t a_num;
		uint64_t b_num;
		bool a_is_digit_only = parse_digit_from_godot_prerelease_field(prerelease, a_num);
		bool b_is_digit_only = parse_digit_from_godot_prerelease_field(b_prerelease, b_num);

		if (a_is_digit_only && b_is_digit_only) {
			// Identifiers consisting of only digits are compared numerically.
			if (a_num == b_num) {
				return 0;
			}
			return a_num > b_num ? 1 : -1;
		}
		if (a_is_digit_only || b_is_digit_only) {
			// Numeric identifiers always have lower precedence than non-numeric identifiers.
			return b_is_digit_only ? 1 : -1;
		}
	}

	return 0;
}

// We can only say for certain that this is an official build if the build metadata contains "official".
bool GodotVer::is_not_custom_build() {
	return !build_metadata.is_empty() && !build_metadata.contains("official");
}

bool GodotVer::parse_valid(const String &p_ver_text, Ref<GodotVer> &r_semver) {
#ifdef MODULE_REGEX_ENABLED
	Ref<RegExMatch> match = GodotVer::non_strict_regex->search(p_ver_text);
	if (match.is_valid()) {
		// Godot version specific hacks
		String prerelease = match->get_string("prerelease");
		String build = match->get_string("buildmetadata");
		r_semver = GodotVer::create(
				match->get_string("major").to_int(),
				!match->get_string("minor").is_empty() ? match->get_string("minor").to_int() : 0,
				!match->get_string("patch").is_empty() ? match->get_string("patch").to_int() : 0,
				prerelease,
				build);
		return true;
	}

	return false;
#else
	ERR_FAIL_V_MSG(false, "CAN'T PARSE VERSION WITHOUT REGEX MODULE");

#endif
}

Ref<GodotVer> GodotVer::parse(const String &p_ver_text) {
	Ref<GodotVer> r_val;
	ERR_FAIL_COND_V_MSG(!GodotVer::parse_valid(p_ver_text, r_val), Ref<GodotVer>(), "Not a valid semantic version string!");
	return r_val;
}

Ref<GodotVer> GodotVer::create(int p_major, int p_minor, int p_patch,
		const String &p_prerelease, const String &p_build_metadata) {
	Ref<GodotVer> ref = memnew(GodotVer(p_major, p_minor, p_patch, p_prerelease, p_build_metadata));
	return ref;
}

String GodotVer::as_text() const {
	if (!valid) {
		return "";
	}
	String ver_text = itos(major) + "." + itos(minor) + "." + itos(patch);
	if (!prerelease.is_empty()) {
		ver_text += "." + prerelease;
	}
	if (!build_metadata.is_empty()) {
		ver_text += "." + build_metadata;
	}
	return ver_text;
}

String GodotVer::as_tag() const {
	if (!valid) {
		return "";
	}
	String ver_text = itos(major) + "." + itos(minor);
	if (patch >= 0) {
		ver_text += "." + itos(patch);
	}
	if (!prerelease.is_empty()) {
		ver_text += "-" + prerelease;
	} else {
		ver_text += "-stable";
	}
	return ver_text;
}

void GodotVer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_not_custom_build"), &GodotVer::is_not_custom_build);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("parse_godotver", "ver_text"), &GodotVer::parse);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("create_godotver", "major", "minor", "patch", "prerelease", "build_metadata"), &GodotVer::create, DEFVAL(""), DEFVAL(""));
}

Ref<GodotVer> GodotVer::copy_from(const Ref<GodotVer> &p_ver) {
	Ref<GodotVer> ref;
	ref.instantiate();
	ref->major = p_ver->major;
	ref->minor = p_ver->minor;
	ref->patch = p_ver->patch;
	ref->prerelease = p_ver->prerelease;
	ref->build_metadata = p_ver->build_metadata;
	ref->valid = p_ver->valid;
	ref->is_strict = p_ver->is_strict;
	return ref;
}
