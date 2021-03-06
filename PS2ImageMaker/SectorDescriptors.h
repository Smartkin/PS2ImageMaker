/*
PS2ImageMaker - Library for creating Playstation 2 (PS2)compatible images
Copyright(C) 2020 Vladislav Smyshlyaev(Smartkin)

This program is free software : you can redistribute it and /or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.If not, see < https://www.gnu.org/licenses/>.
*/

#pragma once
#include <stdint.h>
// Definition of all the descriptors used in a PS2 disc
// Laid out just like it would on a disc with supplimentary structs sprinkled in
#pragma pack(1) // We really need that 1 byte alignment :P
// Typedefs for UDF stuff

typedef uint16_t ushort;
typedef uint32_t uint;
typedef uint64_t ulong;
typedef unsigned char byte;

struct DirectoryRecord {
	char dir_rec_len;
	char ext_attr_rec_len = '\0';
	int loc_of_ext_lsb; // LBA, written in both endians
	int loc_of_ext_msb;
	int data_len_lsb; // Written in both endians
	int data_len_msb;
	char red_date_and_time[7]; // Recorded as yy/mm/dd hh:mm:ss GMT offset in 15 minutes interval
	char flags;
	char file_size_in_inter = '\0'; // Ugh, will probably just write 0 nobody cares about interleave mode anyway
	char interleave_gap = '\0'; // Same
	short vol_seq_num_lsb; // Written in both endians
	short vol_seq_num_msb;
	char file_ident_len;
	char file_ident; // Determined by file_ident_len, since we can't make pad an optional field, it will just be appended to this string, EZ
};

struct PrimaryVolumeDescriptor_ISO {
	char type = (char)1;
	char ident[5] = { 'C', 'D', '0', '0', '1' };
	char ver = (char)1;
	char unused = '\0';
	char sys_ident[32];
	char vol_ident[32];
	char unused_2[8];
	unsigned int vol_space_size_lsb;
	unsigned int vol_space_size_msb;
	char unused_3[32];
	short vol_set_size_lsb;
	short vol_set_size_msb;
	short vol_seq_num_lsb;
	short vol_seq_num_msb;
	short log_block_size_lsb;
	short log_block_size_msb;
	int path_table_size_lsb;
	int path_table_size_msb;
	int loc_type_l_path_tbl;
	int loc_opt_l_path_tbl;
	int loc_type_m_path_tbl;
	int loc_opt_m_path_tbl;
	DirectoryRecord root;
	char volume_set_desc[128];
	char publisher_ident[128];
	char data_preparer_ident[128];
	char app_ident[128];
	char cop_ident[38];
	char abstract_ident[36];
	char bibl_ident[37];
	char vol_create_date_time[17];
	char vol_mod_date_time[17];
	char vol_exp_date_time[17];
	char vol_effec_date_time[17];
	char file_str_ver = (char)1;
	// 883 onwards is 0x00 and are either unused or reserved
};

struct VolumeDescriptorSetTerminator {
	byte type = (char)1;
	char identifier[5] = { 'C', 'D', '0', '0', '1' };;
	byte version = (char)1;
	// rest of bytes are unused
};

struct BeginningExtendedAreaDescriptor {
	byte type = (char)0;
	char identifier[5] = { 'B', 'E', 'A', '0', '1' };
	byte version = (char)1;
	// rest of bytes are unused
};

struct NSRDescriptor {
	byte type = (char)0;
	char identifier[5] = { 'N', 'S', 'R', '0', '2' };
	byte version = (char)1;
	// rest of bytes are unused
};

struct TerminatingExtendedAreaDescriptor {
	byte type = (byte)0;
	char identifier[5] = { 'T', 'E', 'A', '0', '1' };
	byte version = (byte)1;
	// rest of bytes are unused
};

/*
*  After this are structs related to UDF standard
*/

struct DescriptorTag {
	ushort tag_ident;
	ushort desc_version;
	byte tag_checksum = 0;
	byte reserved = '\0';
	ushort tag_serial = 0; // Crash Twinsanity has this 0 for all its sectors so I assume all PS2 discs do as well :^)
	ushort desc_crc;
	ushort desc_crc_len; // = sizeof(Descriptor) - sizeof(DescriptorTag), apparently in PS2's case it's just the entire sector. The only exception is FileIdentifierDescriptor
	uint tag_location;
};

struct Charspec {
	byte char_set_type;
	char char_set_info[63];
};

struct Extent {
	uint length; // Data length
	uint location; // Sector index
};

struct Timestamp {
	ushort type_and_timezone;
	ushort year;
	byte month;
	byte day;
	byte hour;
	byte minute;
	byte second;
	byte centiseconds;
	byte milliseconds;
	byte microseconds;
};

struct EntityID {
	byte flags;
	char ident[23];
	char ident_suffix[8];
};

struct PrimaryVolumeDescriptor_UDF {
	DescriptorTag tag;
	uint vol_desc_seq_num;
	uint prim_vol_desc_num;
	char vol_ident[32];
	ushort vol_seq_num;
	ushort max_vol_seq_num;
	ushort interchange_level;
	ushort max_inter_level;
	uint character_set_list;
	uint max_char_set_list;
	char vol_set_ident[128];
	Charspec desc_char_set;
	Charspec expl_char_set;
	Extent volume_abstract;
	Extent volume_copy_notice;
	EntityID app_ident;
	Timestamp rec_date_and_time;
	EntityID impl_ident;
	byte impl_use[64];
	uint pred_vol_desc_seq_loc;
	byte reserved[24];
};

struct LVInformation {
	Charspec lvi_charset;
	char log_vol_ident[128];
	char lvinfo1[36];
	char lvinfo2[36];
	char lvinfo3[36];
	EntityID impl_id;
	byte impl_use[128];
};

struct ImplUseVolumeDescriptor {
	DescriptorTag tag;
	uint vol_desc_seq_num;
	EntityID impl_ident;
	LVInformation impl_use;
};

struct PartitionDescriptor {
	DescriptorTag tag;
	uint vol_desc_seq_num;
	ushort part_flags;
	ushort part_num;
	EntityID part_contents;
	char part_cont_use[128];
	uint access_type;
	uint part_start_loc;
	uint part_len;
	EntityID impl_ident;
	byte impl_use[128];
	byte reserved[156];
};

struct LogicalVolumeDescriptor {
	DescriptorTag tag;
	uint vol_desc_seq_num;
	Charspec desc_char_set;
	char log_vol_ident[128];
	uint log_block_size;
	EntityID domain_ident;
	char log_vol_cont_use[16];
	uint map_table_len;
	uint num_of_part_maps;
	EntityID impl_ident;
	byte impl_use[128];
	Extent int_seq_extent;
	byte part_maps[6];
};

struct UnallocatedSpaceDescriptor {
	DescriptorTag tag;
	uint vol_desc_seq_num;
	uint num_of_alloc_desc;
	Extent alloc_desc;
};

struct TerminatingDescriptor {
	DescriptorTag tag; // Tag ident 0x8
};

// Trailing logical sectors, consume 10 sectors because reserved by UDF? Who cares, basically just skip 10 sectors
// After that just write the same sectors again for UDF but this time as RSRV

struct LogVolIntImplUse {
	EntityID impl_id;
	uint num_of_files;
	uint num_of_dirs;
	ushort min_udf_read_rev;
	ushort min_udf_write_rev;
	ushort max_udf_write_rev;
	byte impl_use[2];
};

struct LogicalVolumeIntegrityDescriptor {
	DescriptorTag tag;
	Timestamp rec_date_and_time;
	uint integ_type;
	Extent next_integ_extent;
	char log_vol_cont_use[32];
	uint num_of_partitions;
	uint len_of_impl_use;
	uint free_space_table;
	uint size_table;
	LogVolIntImplUse impl_use;
};

// Then comes another terminating descriptor

// After this all sectors until 256 are reserved so they are skipped

struct AnchorVolumeDescriptorPointer {
	DescriptorTag tag;
	Extent main_vol_desc_seq_extent;
	Extent reserve_vol_desc_seq_extent;
	byte reserved[480];
};

struct PathTableEntry {
	byte ident_len;
	byte ext_rec_attrib_len;
	short loc_of_ext; // LBA
	byte par_dir_num;
	char dir_ident; // Length depends on ident_len field, but not to mess with memory management algo is gonna write this in a different way
	byte pad = '\0'; // Onle written if ident_len is odd
};

// Directory records happen next, look at the struct at the very top

struct LbPointer {
	uint log_block_num;
	ushort part_ref_num;
};

struct LongAllocDescriptor {
	uint extent_len;
	LbPointer extent_loc;
	byte impl_use[6];
};

struct FileSetDescriptor {
	DescriptorTag tag;
	Timestamp rec_date_and_time;
	ushort inter_level;
	ushort max_inter_level;
	uint char_set_list;
	uint max_char_set_list;
	uint file_set_num;
	uint file_set_desc_num;
	Charspec log_vol_ident_char_set;
	char log_vol_ident[128];
	Charspec file_set_char_set;
	char file_set_ident[32];
	char copy_file_ident[32];
	char abstr_file_ident[32];
	LongAllocDescriptor root_dir_icb;
	EntityID domain_ident;
	LongAllocDescriptor next_extent;
	byte reserved[48];
};

// Terminating descriptor


struct FileIdentifierDescriptor { // These are written from Root to the very bottom folder, Root is a parent of Root
	DescriptorTag tag; // Length 16
	ushort file_ver_num;
	byte file_chars;
	byte len_of_file_ident;
	LongAllocDescriptor icb; // Length 16
	ushort len_of_impl_use;
	byte impl_use;
	char file_ident; // Mostly here for root folder, for actual folders some math and buffers are involved to compute proper size
};

struct ICBTag {
	uint prior_rec_num_of_direct_entries;
	ushort strategy_type;
	byte strat_param[2];
	ushort num_of_entries;
	byte reserved = '\0';
	byte file_type;
	LbPointer parent_icb_loc;
	ushort flags;
};

struct EA_HeaderDescriptor {
	DescriptorTag tag;
	uint impl_attrib_loc = 0x18;
	uint app_attrib_loc = 0x84;
};

// When for once C++ metaprogramming is actually useful
template<uint attrib_len_t, uint impl_use_len_t>
struct ImplUseExtAttrib {
	uint attrib_type = 0x800;
	byte attrib_subtype = 0x1;
	byte reserved[3] = { '\0', '\0', '\0' };
	uint attrib_len = attrib_len_t;
	uint impl_use_len = impl_use_len_t;
	EntityID impl_ident;
	byte impl_use[impl_use_len_t];
};

struct AllocDescriptor {
	uint info_len;
	uint log_block_num;
};

struct FileEntry {
	DescriptorTag tag;
	ICBTag icb_tag;
	uint uid;
	uint gid;
	uint permissions;
	ushort file_link_cnt;
	byte record_format;
	byte record_disp_attrib;
	uint record_len;
	ulong info_len;
	ulong log_blocks_rec;
	Timestamp access_time;
	Timestamp mod_time;
	Timestamp attrib_time;
	uint checkpoint;
	LongAllocDescriptor ext_attrib_icb;
	EntityID impl_ident;
	ulong unique_id;
	uint len_of_ext_attrib = 132;
	uint len_of_alloc_desc = 8;
	EA_HeaderDescriptor ext_attrib_hd;
	ImplUseExtAttrib<52, 4> iuea_udf_free;
	ImplUseExtAttrib<56, 8> iuea_udf_cgms;
	AllocDescriptor alloc_desc;
};

// Then comes a pad sector

struct EndOfSessionDescriptor {
	DescriptorTag tag;
	AllocDescriptor alloc_desc1;
	AllocDescriptor alloc_desc2;
};

#pragma pack()