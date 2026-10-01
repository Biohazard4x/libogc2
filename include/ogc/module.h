/*-------------------------------------------------------------

module.h -- REL module loader interface

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
   not claim that you wrote the original software.
2. Altered source versions must be plainly marked as such.
3. This notice may not be removed or altered from any source distribution.

-------------------------------------------------------------*/

#ifndef __OGC_MODULE_H__
#define __OGC_MODULE_H__

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* REL file format revisions understood by this implementation. */
#define OS_MODULE_VERSION 3

typedef u32 OSModuleID;

typedef struct OSModuleInfo {
	OSModuleID id;
	struct OSModuleInfo *next;
	struct OSModuleInfo *prev;
	u32 numSections;
	u32 sectionInfoOffset;
	u32 nameOffset;
	u32 nameSize;
	u32 version;
} OSModuleInfo;

typedef struct OSModuleHeader {
	OSModuleInfo info;
	u32 bssSize;
	u32 relOffset;
	u32 impOffset;
	u32 impSize;
	u8 prologSection;
	u8 epilogSection;
	u8 unresolvedSection;
	u8 bssSection;
	u32 prolog;
	u32 epilog;
	u32 unresolved;
	u32 align;       /* version 2+ */
	u32 bssAlign;    /* version 2+ */
	u32 fixSize;     /* version 3+ */
} OSModuleHeader;

typedef struct OSSectionInfo {
	u32 offset;
	u32 size;
} OSSectionInfo;

typedef struct OSImportInfo {
	OSModuleID id;
	u32 offset;
} OSImportInfo;

typedef struct OSRel {
	u16 offset;
	u8 type;
	u8 section;
	u32 addend;
} OSRel;

#define OS_SECTIONINFO_EXEC       0x1
#define OS_SECTIONINFO_OFFSET(x)  ((x) & ~0x1u)

#define R_PPC_ADDR32              1
#define R_PPC_ADDR16_LO           4
#define R_PPC_ADDR16_HI           5
#define R_PPC_ADDR16_HA           6
#define R_PPC_REL24               10
#define R_PPC_REL14               11
#define R_DOLPHIN_NOP             201
#define R_DOLPHIN_SECTION         202
#define R_DOLPHIN_END             203

/*
 * Link an in-memory REL image. The caller owns the image and supplies a
 * suitably aligned BSS area of header->bssSize bytes. This routine does not
 * perform disc I/O and does not invoke the module's lifecycle callbacks.
 */
BOOL OSLink(OSModuleInfo *module, void *bss);
BOOL OSUnlink(OSModuleInfo *module);

/* Register the already-running main executable's section table as module 0.
 * Its section entries must contain loaded absolute addresses and sizes.
 */
BOOL OSRegisterModule(OSModuleInfo *module);
BOOL OSUnregisterModule(OSModuleInfo *module);
OSModuleInfo *OSGetModuleByID(OSModuleID id);

#ifdef __cplusplus
}
#endif

#endif /* __OGC_MODULE_H__ */
