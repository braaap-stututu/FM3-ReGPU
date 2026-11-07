// The table of recompiled images (recomp_modules.h). FM3 is a single image: default.xex (Disc 1
// has no other XEX), so this is one entry taken straight from XenonRecomp's ppc_config.h instead
// of FM4's generated multi-image table.
#include "recomp_modules.h"

#include <ppc_config.h>
#include <ppc_context.h>

extern PPCFuncMapping PPCFuncMappings[];

const RecompiledModule g_recompiledModules[] = {
    { "game:\\default.xex", uint32_t(PPC_IMAGE_BASE), uint32_t(PPC_IMAGE_SIZE), uint32_t(PPC_CODE_BASE), uint32_t(PPC_CODE_SIZE), PPCFuncMappings },
};
const size_t g_recompiledModuleCount = 1;
