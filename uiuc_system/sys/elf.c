// elf.c - ELF file loader
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "elf.h"
#include "conf.h"
#include "io.h"
#include "string.h"
#include "memory.h"
#include "assert.h"
#include "error.h"

#include <stdint.h>

// Offsets into e_ident

#define EI_CLASS        4   
#define EI_DATA         5
#define EI_VERSION      6
#define EI_OSABI        7
#define EI_ABIVERSION   8   
#define EI_PAD          9  


// ELF header e_ident[EI_CLASS] values

#define ELFCLASSNONE 0
#define ELFCLASS32 1
#define ELFCLASS64 2

// ELF header e_ident[EI_DATA] values

#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

// ELF header e_ident[EI_VERSION] values

#define EV_NONE     0
#define EV_CURRENT  1

// ELF header e_type values

enum elf_et {
    ET_NONE = 0,
    ET_REL,
    ET_EXEC,
    ET_DYN,
    ET_CORE
};

struct elf64_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff; 
    uint32_t e_flags; 
    uint16_t e_ehsize; 
    uint16_t e_phentsize; 
    uint16_t e_phnum; 
    uint16_t e_shentsize; 
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

enum elf_pt {
	PT_NULL = 0, 
	PT_LOAD,
	PT_DYNAMIC,
	PT_INTERP,
	PT_NOTE,
	PT_SHLIB,
	PT_PHDR,
	PT_TLS
};

// Program header p_flags bits

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

// ELF header e_machine values (short list)

#define  EM_RISCV   243

int elf_load(struct io * elfio, void (**eptr)(void)) {
    struct elf64_ehdr ehdr;
    struct elf64_phdr phdr;
    long read_size;
    int i;

    kprintf("ELF: Starting to load ELF file\n");
    kprintf("ELF: File pointer: %p\n", elfio);

    // Read ELF header
    kprintf("ELF: Attempting to read header of size %zu bytes\n", sizeof(ehdr));
    read_size = ioreadat(elfio, 0, &ehdr, sizeof(ehdr));
    kprintf("ELF: ioreadat returned %ld\n", read_size);
    if (read_size != sizeof(ehdr)) {
        kprintf("ELF: Failed to read header, got %ld bytes\n", read_size);
        return -EBADFMT;
    }

    // Print ELF header values for debugging
    kprintf("ELF: Header values:\n");
    kprintf("  e_type: 0x%x\n", ehdr.e_type);
    kprintf("  e_machine: 0x%x\n", ehdr.e_machine);
    kprintf("  e_version: 0x%x\n", ehdr.e_version);
    kprintf("  e_entry: 0x%lx\n", ehdr.e_entry);
    kprintf("  e_phoff: 0x%lx\n", ehdr.e_phoff);
    kprintf("  e_shoff: 0x%lx\n", ehdr.e_shoff);
    kprintf("  e_flags: 0x%x\n", ehdr.e_flags);
    kprintf("  e_ehsize: 0x%x\n", ehdr.e_ehsize);
    kprintf("  e_phentsize: 0x%x\n", ehdr.e_phentsize);
    kprintf("  e_phnum: 0x%x\n", ehdr.e_phnum);
    kprintf("  e_shentsize: 0x%x\n", ehdr.e_shentsize);
    kprintf("  e_shnum: 0x%x\n", ehdr.e_shnum);
    kprintf("  e_shstrndx: 0x%x\n", ehdr.e_shstrndx);

    // Verify ELF magic number (first 4 bytes should be 0x7F, 'E', 'L', 'F')
    kprintf("ELF: Magic number bytes: 0x%02x, '%c', '%c', '%c'\n", 
        ehdr.e_ident[0], ehdr.e_ident[1], ehdr.e_ident[2], ehdr.e_ident[3]);
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        kprintf("ELF: Invalid magic number\n");
        return -EBADFMT;
    }

    // Check ELF class (must be 64-bit)
    kprintf("ELF: Checking class (must be 64-bit): %d\n", ehdr.e_ident[EI_CLASS]);
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        kprintf("ELF: Invalid class - not 64-bit\n");
        return -EBADFMT;
    }

    // Check endianness (must be little-endian or big-endian)
    kprintf("ELF: Checking endianness (must be little-endian or big-endian): %d\n", ehdr.e_ident[EI_DATA]);
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB && ehdr.e_ident[EI_DATA] != ELFDATA2MSB) {
        kprintf("ELF: Invalid endianness - not little-endian or big-endian\n");
        return -EBADFMT;
    }

    // Check ELF version
    kprintf("ELF: Checking version (must be current): %d\n", ehdr.e_ident[EI_VERSION]);
    if (ehdr.e_ident[EI_VERSION] != EV_CURRENT) {
        kprintf("ELF: Invalid version - not current\n");
        return -EBADFMT;
    }

    // Check architecture (must be RISC-V)
    kprintf("ELF: Checking machine (must be RISC-V): %d\n", ehdr.e_machine);
    if (ehdr.e_machine != EM_RISCV) {
        kprintf("ELF: Invalid machine - not RISC-V\n");
        return -EBADFMT;
    }

    // Check file type (must be executable)
    kprintf("ELF: Checking type (must be executable): %d\n", ehdr.e_type);
    if (ehdr.e_type != ET_EXEC) {
        kprintf("ELF: Invalid type - not executable\n");
        return -EBADFMT;
    }

    // Set entry point
    kprintf("ELF: Entry point: 0x%llx\n", ehdr.e_entry);
    *eptr = (void (*)(void))ehdr.e_entry;

    // Process all program headers
    kprintf("ELF: Program header table at offset 0x%llx\n", ehdr.e_phoff);
    kprintf("ELF: Program header size: %u\n", ehdr.e_phentsize);
    kprintf("ELF: Number of program headers: %u\n", ehdr.e_phnum);

    for (i = 0; i < ehdr.e_phnum; i++) {
        kprintf("ELF: Processing program header %d\n", i);
        // Read program header directly using ioreadat
        kprintf("ELF: Reading program header at offset 0x%llx\n", ehdr.e_phoff + i * ehdr.e_phentsize);
        
        // Read program header in smaller chunks
        uint32_t type_flags[2];
        uint64_t offset_vaddr[2];
        uint64_t paddr_filesz[2];
        uint64_t memsz_align[2];

        read_size = ioreadat(elfio, ehdr.e_phoff + i * ehdr.e_phentsize, type_flags, sizeof(type_flags));
        if (read_size != sizeof(type_flags)) {
            kprintf("ELF: Failed to read type and flags, got %ld bytes\n", read_size);
            return -EIO;
        }

        read_size = ioreadat(elfio, ehdr.e_phoff + i * ehdr.e_phentsize + 8, offset_vaddr, sizeof(offset_vaddr));
        if (read_size != sizeof(offset_vaddr)) {
            kprintf("ELF: Failed to read offset and vaddr, got %ld bytes\n", read_size);
            return -EIO;
        }

        read_size = ioreadat(elfio, ehdr.e_phoff + i * ehdr.e_phentsize + 24, paddr_filesz, sizeof(paddr_filesz));
        if (read_size != sizeof(paddr_filesz)) {
            kprintf("ELF: Failed to read paddr and filesz, got %ld bytes\n", read_size);
            return -EIO;
        }

        read_size = ioreadat(elfio, ehdr.e_phoff + i * ehdr.e_phentsize + 40, memsz_align, sizeof(memsz_align));
        if (read_size != sizeof(memsz_align)) {
            kprintf("ELF: Failed to read memsz and align, got %ld bytes\n", read_size);
            return -EIO;
        }

        phdr.p_type = type_flags[0];
        phdr.p_flags = type_flags[1];
        phdr.p_offset = offset_vaddr[0];
        phdr.p_vaddr = offset_vaddr[1];
        phdr.p_paddr = paddr_filesz[0];
        phdr.p_filesz = paddr_filesz[1];
        phdr.p_memsz = memsz_align[0];
        phdr.p_align = memsz_align[1];

        kprintf("ELF: Program header %d values:\n", i);
        kprintf("  p_type: %u (0x%x)\n", phdr.p_type, phdr.p_type);
        kprintf("  p_flags: 0x%x\n", phdr.p_flags);
        kprintf("  p_offset: 0x%llx\n", phdr.p_offset);
        kprintf("  p_vaddr: 0x%llx\n", phdr.p_vaddr);
        kprintf("  p_paddr: 0x%llx\n", phdr.p_paddr);
        kprintf("  p_filesz: %llu (0x%llx)\n", phdr.p_filesz, phdr.p_filesz);
        kprintf("  p_memsz: %llu (0x%llx)\n", phdr.p_memsz, phdr.p_memsz);
        kprintf("  p_align: 0x%llx\n", phdr.p_align);

        // Validate program header values
        // Only validate filesz <= memsz for LOAD type segments
        if (phdr.p_type == PT_LOAD && phdr.p_filesz > phdr.p_memsz) {
            kprintf("ELF: Invalid program header %d: filesz (%llu) > memsz (%llu)\n", 
                    i, phdr.p_filesz, phdr.p_memsz);
            return -EBADFMT;
        }

        // Only process loadable segments
        if (phdr.p_type == PT_LOAD) {
            void *dest = (void *)phdr.p_vaddr;
            kprintf("ELF: Loading segment at vaddr 0x%p, file offset 0x%llx\n", dest, phdr.p_offset);

            // Read segment data directly using ioreadat at the correct file offset
            kprintf("ELF: Reading %llu bytes from offset 0x%llx\n", phdr.p_filesz, phdr.p_offset);
            read_size = ioreadat(elfio, phdr.p_offset, dest, phdr.p_filesz);
            if (read_size != phdr.p_filesz) {
                kprintf("ELF: Failed to read segment data, got %ld bytes\n", read_size);
                return -EIO;
            }

            // If memory size is larger than file size, zero out the remainder (BSS section)
            if (phdr.p_memsz > phdr.p_filesz) {
                void *bss_start = (void *)(phdr.p_vaddr + phdr.p_filesz);
                size_t bss_size = phdr.p_memsz - phdr.p_filesz;
                kprintf("ELF: Zeroing BSS section at 0x%p, size %zu\n", bss_start, bss_size);
                memset(bss_start, 0, bss_size);
            }
        }
    }

    return 0;
}
