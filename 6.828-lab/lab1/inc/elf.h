#ifndef JOS_INC_ELF_H
#define JOS_INC_ELF_H

#define ELF_MAGIC 0x464C457FU	/* "\x7FELF" in little endian */

/*
 Each ELF file is made up of one ELF header,followed by file data。
 quote以下mit6.828的说明，我们不必深究elf文件的具体格式只需要知道
 an ELF binary starts with a fixed-length elf header,followed by a variable-length program header
  listing each of program sections to be loaded.也就是说在program header当中说明了我们需要的段，我们只需要
  根据Program header来加载文件即可
*/
struct Elf {
	uint32_t e_magic;	// must equal ELF_MAGIC
	uint8_t e_elf[12];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint32_t e_entry; // entry point from where process start executing
	uint32_t e_phoff; //start of program header,0x34 for 32-bit elf
	uint32_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize; // size of this header,
	uint16_t e_phentsize; //size of program header table entry
	uint16_t e_phnum; // number of entries in program header table,
	uint16_t e_shentsize; // size of a section header table entry
	uint16_t e_shnum; // number of entries in section header table
	uint16_t e_shstrndx;
};

struct Proghdr {
	uint32_t p_type;
	uint32_t p_offset; // offset of the segment in the file image,segment = .text,.data,.bss and etc.
	uint32_t p_va; // virtual address of segment in memory
	uint32_t p_pa; // 需要加载到的物理地址
	uint32_t p_filesz;
	uint32_t p_memsz; // size in bytes of segment in memory
	uint32_t p_flags;
	uint32_t p_align;
};

struct Secthdr {
	uint32_t sh_name;
	uint32_t sh_type;
	uint32_t sh_flags;
	uint32_t sh_addr;
	uint32_t sh_offset;
	uint32_t sh_size;
	uint32_t sh_link;
	uint32_t sh_info;
	uint32_t sh_addralign;
	uint32_t sh_entsize;
};

// Values for Proghdr::p_type
#define ELF_PROG_LOAD		1

// Flag bits for Proghdr::p_flags
#define ELF_PROG_FLAG_EXEC	1
#define ELF_PROG_FLAG_WRITE	2
#define ELF_PROG_FLAG_READ	4

// Values for Secthdr::sh_type
#define ELF_SHT_NULL		0
#define ELF_SHT_PROGBITS	1
#define ELF_SHT_SYMTAB		2
#define ELF_SHT_STRTAB		3

// Values for Secthdr::sh_name
#define ELF_SHN_UNDEF		0

#endif /* !JOS_INC_ELF_H */
