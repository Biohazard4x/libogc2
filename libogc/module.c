/*-------------------------------------------------------------

module.c -- REL module linker

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:
1. The origin of this software must not be misrepresented.
2. Altered source versions must be plainly marked as such.
3. This notice may not be removed or altered from any source distribution.

-------------------------------------------------------------*/

#include <ogc/module.h>
#include <ogc/cache.h>
#include <ogc/machine/processor.h>

static OSModuleInfo *__moduleList;

static u32 ModuleHeaderSize(u32 version)
{
	return 0x40 + (version >= 2 ? 8 : 0) + (version >= 3 ? 4 : 0);
}

static OSSectionInfo *SectionTable(OSModuleInfo *module)
{
	return (OSSectionInfo *)((u32)module + module->sectionInfoOffset);
}

static OSModuleInfo *FindModule(OSModuleID id)
{
	OSModuleInfo *module;
	for (module = __moduleList; module; module = module->next)
		if (module->id == id) return module;
	return NULL;
}

static void InsertModule(OSModuleInfo *module)
{
	module->prev = NULL;
	module->next = __moduleList;
	if (__moduleList) __moduleList->prev = module;
	__moduleList = module;
}

static void RemoveModule(OSModuleInfo *module)
{
	if (module->prev) module->prev->next = module->next;
	else __moduleList = module->next;
	if (module->next) module->next->prev = module->prev;
	module->next = module->prev = NULL;
}

static BOOL FirstImport(OSImportInfo *imports, u32 index)
{
	u32 previous;
	for (previous = 0; previous < index; ++previous)
		if (imports[previous].id == imports[index].id) return FALSE;
	return TRUE;
}

static BOOL RelocTableValid(OSModuleHeader *header, OSImportInfo *imports,
							OSModuleInfo *target)
{
	OSRel *rel;
	OSSectionInfo *sourceSections = SectionTable(&header->info);
	OSSectionInfo *targetSections = SectionTable(target);
	u32 importIndex;

	for (importIndex = 0; importIndex < header->impSize / sizeof(OSImportInfo); ++importIndex) {
		u32 sourceSection = 0, sourceOffset = 0;
		if (imports[importIndex].id != target->id) continue;
		rel = (OSRel *)((u32)header + imports[importIndex].offset);
		for (;;) {
			u32 width;
			if (rel->type == R_DOLPHIN_END) break;
			if (rel->type == R_DOLPHIN_SECTION) {
				sourceSection = rel->section;
				sourceOffset = 0;
				if (sourceSection >= header->info.numSections) return FALSE;
				++rel;
				continue;
			}
			if (rel->type == R_DOLPHIN_NOP) {
				sourceOffset += rel->offset;
				++rel;
				continue;
			}
			sourceOffset += rel->offset;
			switch (rel->type) {
			case R_PPC_ADDR32: width = 4; break;
			case R_PPC_ADDR16_LO: case R_PPC_ADDR16_HI: case R_PPC_ADDR16_HA:
			case R_PPC_REL14: width = 4; break;
			case R_PPC_REL24: width = 4; break;
			default: return FALSE;
			}
			if (sourceSection >= header->info.numSections || rel->section >= target->numSections ||
				sourceOffset > sourceSections[sourceSection].size ||
				width > sourceSections[sourceSection].size - sourceOffset ||
				!targetSections[rel->section].size)
				return FALSE;
			++rel;
		}
	}
	return TRUE;
}

static BOOL ApplyRelocs(OSModuleHeader *header, OSImportInfo *imports,
						OSModuleInfo *target, s32 direction)
{
	OSRel *rel;
	OSSectionInfo *sourceSections = SectionTable(&header->info);
	OSSectionInfo *targetSections = SectionTable(target);
	u32 importIndex;
	for (importIndex = 0; importIndex < header->impSize / sizeof(OSImportInfo); ++importIndex) {
		u32 sourceSection = 0, sourceOffset = 0;
		if (imports[importIndex].id != target->id) continue;
		rel = (OSRel *)((u32)header + imports[importIndex].offset);
		for (;;) {
			u8 *place;
			u32 value, instruction;
			if (rel->type == R_DOLPHIN_END) break;
			if (rel->type == R_DOLPHIN_SECTION) {
				sourceSection = rel->section;
				sourceOffset = 0;
				++rel;
				continue;
			}
			sourceOffset += rel->offset;
			if (rel->type == R_DOLPHIN_NOP) {
				++rel;
				continue;
			}
			place = (u8 *)OS_SECTIONINFO_OFFSET(sourceSections[sourceSection].offset) + sourceOffset;
			value = target->id == 0 ? rel->addend :
				OS_SECTIONINFO_OFFSET(targetSections[rel->section].offset) + rel->addend;
			switch (rel->type) {
			case R_PPC_ADDR32:
				*(u32 *)place = direction > 0 ? value : rel->addend;
				break;
			case R_PPC_ADDR16_LO:
				*(u16 *)place = direction > 0 ? (u16)value : (u16)rel->addend;
				break;
			case R_PPC_ADDR16_HI:
				*(u16 *)place = direction > 0 ? (u16)(value >> 16) : (u16)(rel->addend >> 16);
				break;
			case R_PPC_ADDR16_HA:
				*(u16 *)place = direction > 0 ? (u16)((value + 0x8000) >> 16) :
					(u16)((rel->addend + 0x8000) >> 16);
				break;
			case R_PPC_REL24:
				instruction = *(u32 *)place;
				value = direction > 0 ? (value - (u32)place) : rel->addend;
				*(u32 *)place = (instruction & ~0x03fffffcu) | (value & 0x03fffffcu);
				break;
			case R_PPC_REL14:
				instruction = *(u32 *)place;
				value = direction > 0 ? (value - (u32)place) : rel->addend;
				*(u32 *)place = (instruction & ~0x0000fffcu) | (value & 0x0000fffcu);
				break;
			}
			++rel;
		}
	}
	return TRUE;
}

BOOL OSRegisterModule(OSModuleInfo *module)
{
	u32 level;
	if (!module || module->version > OS_MODULE_VERSION || !module->numSections ||
		!module->sectionInfoOffset) return FALSE;
	_CPU_ISR_Disable(level);
	if (FindModule(module->id)) {
		_CPU_ISR_Restore(level);
		return FALSE;
	}
	InsertModule(module);
	_CPU_ISR_Restore(level);
	return TRUE;
}

BOOL OSUnregisterModule(OSModuleInfo *module)
{
	u32 level;
	if (!module) return FALSE;
	_CPU_ISR_Disable(level);
	if (FindModule(module->id) != module) {
		_CPU_ISR_Restore(level);
		return FALSE;
	}
	RemoveModule(module);
	_CPU_ISR_Restore(level);
	return TRUE;
}

OSModuleInfo *OSGetModuleByID(OSModuleID id)
{
	return FindModule(id);
}

BOOL OSLink(OSModuleInfo *module, void *bss)
{
	OSModuleHeader *header = (OSModuleHeader *)module;
	OSSectionInfo *sections;
	OSImportInfo *imports;
	u32 i, level;
	if (!module || (!bss && header->bssSize) || module->id == 0 || module->version < 1 ||
		module->version > OS_MODULE_VERSION || module->numSections == 0 ||
		module->sectionInfoOffset != ModuleHeaderSize(module->version) ||
		header->impSize % sizeof(OSImportInfo) != 0 ||
		header->impOffset > header->relOffset ||
		header->impSize > header->relOffset - header->impOffset ||
		(module->version >= 2 && ((header->align && (u32)module % header->align) ||
		 (header->bssAlign && (u32)bss % header->bssAlign)))) return FALSE;

	sections = SectionTable(module);
	imports = (OSImportInfo *)((u32)module + header->impOffset);
	for (i = 0; i < module->numSections; ++i) {
		if (sections[i].offset && (OS_SECTIONINFO_OFFSET(sections[i].offset) >= header->impOffset ||
			sections[i].size > header->impOffset - OS_SECTIONINFO_OFFSET(sections[i].offset)))
			return FALSE;
	}
	for (i = 0; i < header->impSize / sizeof(OSImportInfo); ++i) {
		OSModuleInfo *target = imports[i].id == module->id ? module : FindModule(imports[i].id);
		if (!target || !RelocTableValid(header, imports, target)) return FALSE;
	}

	_CPU_ISR_Disable(level);
	if (FindModule(module->id)) {
		_CPU_ISR_Restore(level);
		return FALSE;
	}
	for (i = 1; i < module->numSections; ++i) {
		if (sections[i].offset) {
			u32 exec = sections[i].offset & OS_SECTIONINFO_EXEC;
			sections[i].offset = (u32)module + OS_SECTIONINFO_OFFSET(sections[i].offset) + exec;
		} else if (sections[i].size) {
			sections[i].offset = (u32)bss;
			header->bssSection = (u8)i;
			bss = (u8 *)bss + sections[i].size;
		}
	}
	for (i = 0; i < header->impSize / sizeof(OSImportInfo); ++i) {
		if (!FirstImport(imports, i)) continue;
		OSModuleInfo *target = imports[i].id == module->id ? module : FindModule(imports[i].id);
		ApplyRelocs(header, imports, target, 1);
	}
	if (header->prologSection != 0xff)
		header->prolog += OS_SECTIONINFO_OFFSET(sections[header->prologSection].offset);
	if (header->epilogSection != 0xff)
		header->epilog += OS_SECTIONINFO_OFFSET(sections[header->epilogSection].offset);
	if (header->unresolvedSection != 0xff)
		header->unresolved += OS_SECTIONINFO_OFFSET(sections[header->unresolvedSection].offset);
	InsertModule(module);
	for (i = 1; i < module->numSections; ++i) {
		if (sections[i].offset && (sections[i].offset & OS_SECTIONINFO_EXEC)) {
			u32 address = OS_SECTIONINFO_OFFSET(sections[i].offset);
			DCFlushRange((void *)address, sections[i].size);
			ICInvalidateRange((void *)address, sections[i].size);
		}
	}
	_CPU_ISR_Restore(level);
	return TRUE;
}

BOOL OSUnlink(OSModuleInfo *module)
{
	OSModuleHeader *header = (OSModuleHeader *)module;
	OSImportInfo *imports;
	OSModuleInfo *other;
	u32 i, level;
	if (!module || module->id == 0) return FALSE;
	_CPU_ISR_Disable(level);
	if (FindModule(module->id) != module) {
		_CPU_ISR_Restore(level);
		return FALSE;
	}
	/* Never detach a provider while another live module still points into it. */
	for (other = __moduleList; other; other = other->next) {
		OSModuleHeader *otherHeader;
		OSImportInfo *otherImports;
		if (other == module) continue;
		otherHeader = (OSModuleHeader *)other;
		otherImports = (OSImportInfo *)((u32)other + otherHeader->impOffset);
		for (i = 0; i < otherHeader->impSize / sizeof(OSImportInfo); ++i) {
			if (otherImports[i].id == module->id) {
				_CPU_ISR_Restore(level);
				return FALSE;
			}
		}
	}
	imports = (OSImportInfo *)((u32)module + header->impOffset);
	for (i = 0; i < header->impSize / sizeof(OSImportInfo); ++i) {
		if (!FirstImport(imports, i)) continue;
		OSModuleInfo *target = FindModule(imports[i].id);
		if (target) ApplyRelocs(header, imports, target, -1);
	}
	if (header->prologSection != 0xff)
		header->prolog -= OS_SECTIONINFO_OFFSET(SectionTable(module)[header->prologSection].offset);
	if (header->epilogSection != 0xff)
		header->epilog -= OS_SECTIONINFO_OFFSET(SectionTable(module)[header->epilogSection].offset);
	if (header->unresolvedSection != 0xff)
		header->unresolved -= OS_SECTIONINFO_OFFSET(SectionTable(module)[header->unresolvedSection].offset);
	{
		OSSectionInfo *sections = SectionTable(module);
		for (i = 1; i < module->numSections; ++i) {
			if (i == header->bssSection) sections[i].offset = 0;
			else if (sections[i].offset)
				sections[i].offset = ((sections[i].offset & ~OS_SECTIONINFO_EXEC) - (u32)module) |
					(sections[i].offset & OS_SECTIONINFO_EXEC);
		}
		header->bssSection = 0;
	}
	RemoveModule(module);
	_CPU_ISR_Restore(level);
	return TRUE;
}
