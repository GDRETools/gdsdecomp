#ifndef TEST_RESOURCE_LOADING_H
#define TEST_RESOURCE_LOADING_H

#include <compat/resource_compat_text.h>
#include <compat/resource_loader_compat.h>
#include <modules/gdscript/gdscript_tokenizer_buffer.h>
#include <utility/common.h>
#include <utility/glob.h>

#include "test_common.h"
#include "tests/test_macros.h"
#include "utility/file_access_gdre.h"

namespace TestResourceLoading {

TEST_CASE("[GDSDecomp][ResourceLoading] Basic resource loading") {
	REQUIRE(GDRESettings::get_singleton());
	// Get available test versions
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	// Test each version
	for (const String &version : versions) {
		String test_dir = get_test_resources_path().path_join(version);
		String previous_resource_path = GDRESettings::get_singleton()->get_project_path();
		GDRESettings::get_singleton()->set_project_path(test_dir);
		GDREPackedData::get_singleton()->set_default_file_access();

		// Test loading a simple resource
		SUBCASE((String("Load simple resource: ") + version).utf8().get_data()) {
			String resource_path = test_dir.path_join("simple_resource.tres");
			Error error;
			Ref<Resource> resource = ResourceCompatLoader::real_load(resource_path, "", &error);

			CHECK(error == OK);
			CHECK(resource.is_valid());

			// Verify resource properties
			if (resource.is_valid()) {
				CHECK(resource->get_class() == "Resource");
			}
		}

		// Test loading a resource with dependencies
		SUBCASE((String("Load resource with dependencies: ") + version).utf8().get_data()) {
			String resource_path = test_dir.path_join("resource_with_deps.tres");
			Error error;
			Ref<Resource> resource = ResourceCompatLoader::real_load(resource_path, "", &error);

			CHECK(error == OK);
			CHECK(resource.is_valid());

			// Verify dependencies are loaded
			if (resource.is_valid()) {
				// Check for expected dependencies
				// This will depend on the specific test resource structure
				//metadata/test
				//metadata/test2
				Ref<Resource> dependency = resource->get("metadata/test");
				CHECK(dependency.is_valid());
				dependency = resource->get("metadata/test2");
				CHECK(dependency.is_valid());
			}
		}

		// Test error handling for non-existent resource
		SUBCASE((String("Handle non-existent resource: ") + version).utf8().get_data()) {
			String non_existent_path = test_dir.path_join("non_existent_resource.tres");
			Error error;
			Ref<Resource> resource = ResourceCompatLoader::real_load(non_existent_path, "", &error);

			CHECK(error != OK);
			CHECK(resource.is_null());
		}
		GDRESettings::get_singleton()->set_project_path(previous_resource_path);
		GDREPackedData::get_singleton()->reset_default_file_access();
	}
}

TEST_CASE("[GDSDecomp][ResourceLoading] Resource format compatibility") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	for (const String &version : versions) {
		// SUBCASE(version.utf8().get_data()) {
		String test_dir = get_test_resources_path().path_join(version);

		// Test format version detection
		SUBCASE("Format version detection") {
			String resource_path = test_dir.path_join("simple_resource.tres");
			Error error;
			ResourceFormatLoaderCompatText loader;
			Ref<ResourceInfo> info = loader.get_resource_info(resource_path, &error);

			CHECK(error == OK);
			CHECK(info.is_valid());
			CHECK(info->resource_format == "text");
			CHECK(info->ver_format > 0);
		}

		// Test resource type detection
		SUBCASE("Resource type detection") {
			String resource_path = test_dir.path_join("simple_resource.tres");
			Error error;
			ResourceFormatLoaderCompatText loader;
			Ref<ResourceInfo> info = loader.get_resource_info(resource_path, &error);

			CHECK(error == OK);
			CHECK(info.is_valid());
			CHECK(info->type == "Resource");
		}
		// }
	}
}

TEST_CASE("[GDSDecomp][ResourceLoading] Resource loading modes") {
	Vector<String> versions = get_test_versions();
	CHECK(versions.size() > 0);

	for (const String &version : versions) {
		// SUBCASE(version.utf8().get_data()) {
		String test_dir = get_test_resources_path().path_join(version);
		String resource_path = test_dir.path_join("simple_resource.tres");

		// Test different loading modes
		SUBCASE("Different loading modes") {
			Error error;

			// Test REAL_LOAD mode
			Ref<Resource> real_resource = ResourceCompatLoader::real_load(resource_path, "", &error);
			CHECK(error == OK);
			CHECK(real_resource.is_valid());

			// Test FAKE_LOAD mode
			Ref<Resource> fake_resource = ResourceCompatLoader::fake_load(resource_path, "", &error);
			CHECK(error == OK);
			CHECK(fake_resource.is_valid());

			// Test NON_GLOBAL_LOAD mode
			Ref<Resource> non_global_resource = ResourceCompatLoader::non_global_load(resource_path, "", &error);
			CHECK(error == OK);
			CHECK(non_global_resource.is_valid());
		}

		// Test different cache modes
		SUBCASE("Different cache modes") {
			Error error;

			// Test CACHE_MODE_IGNORE
			Ref<Resource> ignore_resource = ResourceCompatLoader::real_load(
					resource_path, "", &error, ResourceFormatLoader::CACHE_MODE_IGNORE);
			CHECK(error == OK);

			// Test CACHE_MODE_REUSE
			Ref<Resource> reuse_resource = ResourceCompatLoader::real_load(
					resource_path, "", &error, ResourceFormatLoader::CACHE_MODE_REUSE);
			CHECK(error == OK);

			// Test CACHE_MODE_REPLACE
			Ref<Resource> replace_resource = ResourceCompatLoader::real_load(
					resource_path, "", &error, ResourceFormatLoader::CACHE_MODE_REPLACE);
			CHECK(error == OK);
		}
		// }
	}
}

} //namespace TestResourceLoading

#endif
