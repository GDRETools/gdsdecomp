#ifndef GDRE_CLI_MAIN_H
#define GDRE_CLI_MAIN_H

#include "core/object/reference.h"
#include "gdre_settings.h"
class GDRECLIMain : public Reference {
	GDCLASS(GDRECLIMain, Reference);

private:
	GDRESettings *gdres_singleton;

protected:
	static void _bind_methods();

public:
	Error open_log(const String &path);
	Error close_log();

	GDRECLIMain() {
		gdres_singleton = memnew(GDRESettings);
	}
	~GDRECLIMain() {
		close_log();
		memdelete(gdres_singleton);
	}
};

#endif