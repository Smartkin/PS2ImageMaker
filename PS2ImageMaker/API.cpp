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

#include "pch.h"
#include "API.h"
#include "Directory.h"
#include "File.h"
#include "SectorManager.h"
#include "SectorDescriptors.h"
#include "Util.h"
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <algorithm>
#include <map>
#include <cassert>

constexpr auto LOG_BLOCK_SIZE = 0x800U;
std::mutex progress_mut;
Progress program_progress;
Progress progress_copy;
bool progress_dirty;
char game_path[1024];
char dest_path[1024];

void pack(const char* game_path, const char* dest_path);
void write_sectors(std::ofstream& f, FileTree* ft);
void write_file_tree(SectorManager& sm, std::ofstream& f);
unsigned int get_path_table_size(FileTree* ft);
void pad_string(char* str, int offset, int size, const char pad = ' ');
void fill_path_table(char* buffer, FileTree* ft, bool msb = false);
unsigned int fill_fid(SectorManager& sm, FileIdentifierDescriptor& fi, FileTreeNode* node, unsigned int cur_spec_lba, std::vector<std::pair<char*, unsigned int>>& buffers);
template<typename T>
void fill_tag_checksum(DescriptorTag& tag, T* buffer, unsigned int size = sizeof(T));

// Helper struct to pass to the fill file entry function
struct ImageContext {
	Timestamp twins_creation_time;
	char dvd_gen[18] = "DVD-ROM GENERATOR";
	char udf_free_ea[17] = "*UDF FreeEASpace";
	char udf_cgms_info[19] = "*UDF DVD CGMS Info";
	char id_suff[3] = { 0x2, 0x1, 0x3 };
	char iuea_free_impl[4] = { 0x61, 0x5, 0x0, 0x0 };
	char iuea_cgms_impl[8] = { 0x49, 0x5, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
};
void fill_file_fe(std::ofstream& f, SectorManager& sm, ulong unique_id, ushort cur_spec_lba, ImageContext& context);

// Launch the thread to pack
extern "C" Progress* start_packing(const char* game_path, const char* dest_path) {
	// Copy over the received strings
	strncpy(::game_path, game_path, strlen(game_path));
	::game_path[strlen(game_path)] = '\0';
	strncpy(::dest_path, dest_path, strlen(dest_path));
	::dest_path[strlen(dest_path)] = '\0';
	std::thread* thr = new std::thread(pack, ::game_path, ::dest_path);
	return &progress_copy;
}

extern "C" Progress* poll_progress() {
	std::lock_guard<std::mutex> guard(progress_mut);
	if (progress_dirty) {
		progress_copy = program_progress;
		progress_dirty = false;
	}
	return &progress_copy;
}

// Start the packing
void pack(const char* game_path, const char* dest_path) {
	Directory dir(game_path);
	update_progress(ProgressState::ENUM_FILES, 0);
	FileTree* ft = dir.get_files();
	if (ft == nullptr) { // No file tree was built
		update_progress(ProgressState::FAILED, 1.0, "", true);
		return;
	}
	update_progress(ProgressState::WRITE_SECTORS, 0.1);
	std::ofstream image;
	image.open(dest_path, std::ios_base::binary | std::ios_base::out);
	write_sectors(image, ft);
	update_progress(ProgressState::FINISHED, 1.0, "", true);
}

// All the writing for each sector is packed into this single function(for the most part) instead of having each sector to be in its separate function
// biggest reason is because all sectors need a very strict ordering so instead of creating a seperate function for each
// they are just divided into regions, additionally certain sector's data can depend on others
void write_sectors(std::ofstream& f, FileTree* ft) {
	SectorManager sm(ft);
	const char pad = ' '; // For padding with spaces
	auto sys_ident = "PLAYSTATION";
	auto vol_ident = "CRASH";
	auto publisher_ident = "VUG";
	auto data_prep = "P.GENDREAU";
	auto app_ident = "PLAYSTATION";
	auto cop_ident = "VUG";
	// Size of the identifier is written at the end of some fields in UDF descriptors, no clue why nothing is mentioned about this in UDF standard
	// must have been some Sony shenanigans
	char vol_ident_len = strlen(vol_ident) + 1;
	// Fill first 16 sectors with nothing as they are system stuff, Sony's CDVDGEN fills these with some bytes, potentially for CRC calculations?
	for (int i = 0; i < 16; ++i) {
		auto pad = '\0';
		sm.write_sector<char>(f, &pad, 1);
	}
	// Write primary descriptor ISO
#pragma region PrimaryVolumeDescriptor_ISO writing
	PrimaryVolumeDescriptor_ISO pvd;
	
	strncpy(pvd.sys_ident, sys_ident, strlen(sys_ident));
	pad_string(pvd.sys_ident, strlen(sys_ident), 32);
	
	strncpy(pvd.vol_ident, vol_ident, strlen(vol_ident));
	pad_string(pvd.vol_ident, strlen(vol_ident), 32);
	// Write unused field
	pad_string(pvd.unused_2, 0, 8, '\0');
	pvd.vol_space_size_lsb = sm.get_total_sectors();
	pvd.vol_space_size_msb = changeEndianness32(sm.get_total_sectors());
	// Write unused field
	pad_string(pvd.unused_3, 0, 32, '\0');
	// There is only 1 volume and 1 partition
	pvd.vol_set_size_lsb = 1;
	pvd.vol_set_size_msb = changeEndianness16(1);
	pvd.vol_seq_num_lsb = 1;
	pvd.vol_seq_num_msb = changeEndianness16(1);
	// Block size is always 2048(0x800)
	pvd.log_block_size_lsb = LOG_BLOCK_SIZE;
	pvd.log_block_size_msb = changeEndianness16(LOG_BLOCK_SIZE);
	auto path_table_size = get_path_table_size(ft);
	pvd.path_table_size_lsb = path_table_size;
	pvd.path_table_size_msb = changeEndianness32(path_table_size);
	// These values are hardcoded because nobody in their right mind would manage to fill more than one sector with folders, right?
	pvd.loc_type_l_path_tbl = 257;
	pvd.loc_opt_l_path_tbl = 258;
	pvd.loc_type_m_path_tbl = changeEndianness32(259);
	pvd.loc_opt_m_path_tbl = changeEndianness32(260);
	// Write root directory record
	DirectoryRecord& root_rec = pvd.root;
	root_rec.dir_rec_len = 34;
	root_rec.ext_attr_rec_len = '\0';
	root_rec.loc_of_ext_lsb = 261;
	root_rec.loc_of_ext_msb = changeEndianness32(261);
	// These are actually supposed to be calculated but I couldn't find out the exact algo but it works out if it's just set to the entire logical block size
	auto root_len = LOG_BLOCK_SIZE;
	root_rec.data_len_lsb = root_len;
	root_rec.data_len_msb = changeEndianness32(root_len);
	// Hardcoding to some random date because who cares lmao
	root_rec.red_date_and_time[0] = 120;
	root_rec.red_date_and_time[1] = 8;
	root_rec.red_date_and_time[2] = 25;
	root_rec.red_date_and_time[3] = 11;
	root_rec.red_date_and_time[4] = 30;
	root_rec.red_date_and_time[5] = '\0';
	root_rec.red_date_and_time[6] = '\0';
	root_rec.flags = 0x2; // Indicates that root should be treated as a folder
	root_rec.file_size_in_inter = '\0';
	root_rec.interleave_gap = '\0';
	// Again there is only 1 volume
	root_rec.vol_seq_num_lsb = 1;
	root_rec.vol_seq_num_msb = changeEndianness16(1);
	// Root's file identifier is byte 00
	root_rec.file_ident_len = 1;
	root_rec.file_ident = '\0';
	// In ISO pvd for string padding spaces(0x20) are used, however in UDF \0(0x00) bytes are used
	pad_string(pvd.volume_set_desc, 0, 128);
	
	strncpy(pvd.publisher_ident, publisher_ident, strlen(publisher_ident));
	pad_string(pvd.publisher_ident, strlen(publisher_ident), 128);
	
	strncpy(pvd.data_preparer_ident, data_prep, strlen(data_prep));
	pad_string(pvd.data_preparer_ident, strlen(data_prep), 128);
	
	strncpy(pvd.app_ident, app_ident, strlen(app_ident));
	pad_string(pvd.app_ident, strlen(app_ident), 128);
	
	strncpy(pvd.cop_ident, cop_ident, strlen(cop_ident));
	pad_string(pvd.cop_ident, strlen(cop_ident), 38);
	// These are not really useful but can be added as options to include in the future
	pad_string(pvd.abstract_ident, 0, 36);
	pad_string(pvd.bibl_ident, 0, 37);
	// Create date time :^)
	auto create_year = "2009";
	auto create_month = "09";
	auto create_day = "03";
	auto create_hour = "07";
	auto create_minutes = "33";
	auto create_seconds = "47";
	auto create_centiseconds = "00";
	char create_green_offset = 0x24;
	strncpy(pvd.vol_create_date_time, create_year, 4);
	strncpy(pvd.vol_create_date_time + 4, create_month, 2);
	strncpy(pvd.vol_create_date_time + 6, create_day, 2);
	strncpy(pvd.vol_create_date_time + 8, create_hour, 2);
	strncpy(pvd.vol_create_date_time + 10, create_minutes, 2);
	strncpy(pvd.vol_create_date_time + 12, create_seconds, 2);
	strncpy(pvd.vol_create_date_time + 14, create_centiseconds, 2);
	strncpy(pvd.vol_create_date_time + 16, &create_green_offset, 1);
	auto no_date = "0000000000000000";
	strncpy(pvd.vol_mod_date_time, no_date, strlen(no_date) + 1);
	strncpy(pvd.vol_exp_date_time, no_date, strlen(no_date) + 1);
	strncpy(pvd.vol_effec_date_time, no_date, strlen(no_date) + 1);
	sm.write_sector<PrimaryVolumeDescriptor_ISO>(f, &pvd, sizeof(PrimaryVolumeDescriptor_ISO));
#pragma endregion
	// Write volume descriptor set terminator ISO
	VolumeDescriptorSetTerminator set_terminator;
	set_terminator.type = 0xFF;
	sm.write_sector<VolumeDescriptorSetTerminator>(f, &set_terminator, sizeof(VolumeDescriptorSetTerminator));

	// Write BEA1
	BeginningExtendedAreaDescriptor beg_ext_area;
	sm.write_sector<BeginningExtendedAreaDescriptor>(f, &beg_ext_area, sizeof(BeginningExtendedAreaDescriptor));

	// Write NSR2
	NSRDescriptor nsr2;
	sm.write_sector<NSRDescriptor>(f, &nsr2, sizeof(NSRDescriptor));

	// Write TEA1
	TerminatingExtendedAreaDescriptor tea;
	sm.write_sector<TerminatingExtendedAreaDescriptor>(f, &tea, sizeof(TerminatingExtendedAreaDescriptor));

	// Skip reserved sectors
	for (int i = 0; i < 11; ++i) {
		auto pad = '\0';
		sm.write_sector<char>(f, &pad, 1);
	}
	// Predetermined strings by UDF and PS2 standard
	auto osta = "OSTA Compressed Unicode";
	auto dvd_gen = "DVD-ROM GENERATOR";
	auto udf_lv_info = "*UDF LV Info";
	auto osta_complient = "*OSTA UDF Compliant";
	auto udf_free_ea = "*UDF FreeEASpace";
	auto udf_cgms_info = "*UDF DVD CGMS Info";
	char compID = 0x8; // Compression ID of 8
	auto encoded_date = "=0<58115SCEI"; // This is an encoded date of 11:35AM 24/08/2020 with SCEI appended, I haven't managed to figure out the exact algorithm to calculate this so currently it's just hardcoded to a date
	char id_suff[3] = { 0x2, 0x1, 0x3 }; // This is special identifier suffix bytes that are written for certain identifiers
	Timestamp twins_creation_time; // Just use date and time whenever Twinsanity PAL was released
	twins_creation_time.microseconds = 0;
	twins_creation_time.milliseconds = 0;
	twins_creation_time.centiseconds = 0;
	twins_creation_time.second = 47;
	twins_creation_time.minute = 33;
	twins_creation_time.hour = 7;
	twins_creation_time.day = 3;
	twins_creation_time.month = 9;
	twins_creation_time.year = 2004;
	twins_creation_time.type_and_timezone = 0x121C;
	// Write twice the same descriptors for Main and RSRV
	for (int i = 0; i < 2; ++i) {
		ushort cur_tag_ident = 1;
		ushort cur_tag_desc_ver = 2;
		uint cur_vol_desc_seq_num = 0;
		// Write primary volume descriptor UDF	
#pragma region PrimaryVolumeDescriptor_UDF writing
		PrimaryVolumeDescriptor_UDF pvd;
		DescriptorTag& pvd_tag = pvd.tag;
		pvd_tag.tag_ident = cur_tag_ident;
		pvd_tag.desc_version = cur_tag_desc_ver;
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		pvd_tag.desc_crc_len = sizeof(PrimaryVolumeDescriptor_UDF) - sizeof(DescriptorTag);
		pvd_tag.tag_location = sm.get_current_sector();
		pvd.vol_desc_seq_num = cur_vol_desc_seq_num++;
		pvd.prim_vol_desc_num = 0;
		strncpy(pvd.vol_ident, &compID, 1);
		strncpy(pvd.vol_ident + 1, vol_ident, strlen(vol_ident));
		pad_string(pvd.vol_ident, strlen(vol_ident) + 1, 32, '\0');
		strncpy(pvd.vol_ident + 31, &vol_ident_len, 1);
		pvd.vol_seq_num = 1;
		pvd.max_vol_seq_num = 1;
		pvd.interchange_level = 2;
		pvd.max_inter_level = 2;
		pvd.character_set_list = 1;
		pvd.max_char_set_list = 1;
		strncpy(pvd.vol_set_ident, &compID, 1);
		strncpy(pvd.vol_set_ident + 1, encoded_date, strlen(encoded_date));
		pad_string(pvd.vol_set_ident, strlen(encoded_date) + 1, 128 - 36);
		pad_string(pvd.vol_set_ident, strlen(encoded_date) + 1 + 36, 128, '\0');
		char size = 0x31;
		strncpy(pvd.vol_set_ident + 127, &size, 1);
		pvd.desc_char_set.char_set_type = 0;
		strncpy(pvd.desc_char_set.char_set_info, osta, strlen(osta));
		pad_string(pvd.desc_char_set.char_set_info, strlen(osta), 63, '\0');
		pvd.expl_char_set.char_set_type = 0;
		strncpy(pvd.expl_char_set.char_set_info, osta, strlen(osta));
		pad_string(pvd.expl_char_set.char_set_info, strlen(osta), 63, '\0');
		pvd.volume_abstract.length = 0;
		pvd.volume_abstract.location = 0;
		pvd.volume_copy_notice.length = 0;
		pvd.volume_copy_notice.location = 0;
		pvd.app_ident.flags = 0;
		strncpy(pvd.app_ident.ident, sys_ident, strlen(sys_ident));
		pad_string(pvd.app_ident.ident, strlen(sys_ident), 23);
		pad_string(pvd.app_ident.ident_suffix, 0, 8, '\0');
		pvd.rec_date_and_time = twins_creation_time;
		pvd.impl_ident.flags = 0;
		strncpy(pvd.impl_ident.ident, dvd_gen, strlen(dvd_gen));
		pad_string(pvd.impl_ident.ident, strlen(dvd_gen), 23, '\0');
		pad_string(pvd.impl_ident.ident_suffix, 0, 8, '\0');
		pad_string((char*)pvd.impl_use, 0, 64, '\0');
		pvd.pred_vol_desc_seq_loc = 0;
		pad_string((char*)pvd.reserved, 0, 24, '\0');
		fill_tag_checksum(pvd_tag, &pvd);
		sm.write_sector<PrimaryVolumeDescriptor_UDF>(f, &pvd, sizeof(PrimaryVolumeDescriptor_UDF));
#pragma endregion
		cur_tag_ident = 4; // Other identifiers just start couting up from here

		// Write implementation use volume descriptor
#pragma region ImplUseVolumeDescriptor writing
		ImplUseVolumeDescriptor iuv;
		DescriptorTag& iuv_tag = iuv.tag;
		iuv_tag.tag_ident = cur_tag_ident++;
		iuv_tag.desc_crc_len = sizeof(ImplUseVolumeDescriptor) - sizeof(DescriptorTag);
		iuv_tag.desc_version = cur_tag_desc_ver;
		iuv_tag.tag_location = sm.get_current_sector();
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		iuv.vol_desc_seq_num = cur_vol_desc_seq_num++;
		iuv.impl_ident.flags = 0;
		strncpy(iuv.impl_ident.ident, udf_lv_info, strlen(udf_lv_info));
		pad_string(iuv.impl_ident.ident, strlen(udf_lv_info), 23, '\0');
		strncpy(iuv.impl_ident.ident_suffix, id_suff, 2);
		pad_string(iuv.impl_ident.ident_suffix, 2, 8, '\0');
		iuv.impl_use.lvi_charset.char_set_type = 0;
		strncpy(iuv.impl_use.lvi_charset.char_set_info, osta, strlen(osta));
		pad_string(iuv.impl_use.lvi_charset.char_set_info, strlen(osta), 63, '\0');
		strncpy(iuv.impl_use.log_vol_ident, &compID, 1);
		strncpy(iuv.impl_use.log_vol_ident + 1, vol_ident, strlen(vol_ident));
		pad_string(iuv.impl_use.log_vol_ident, strlen(vol_ident) + 1, 128, '\0');
		// Honestly what's wrong with these standard designers?
		strncpy(iuv.impl_use.log_vol_ident + 127, &vol_ident_len, 1);
		strncpy(iuv.impl_use.lvinfo1, &compID, 1);
		strncpy(iuv.impl_use.lvinfo2, &compID, 1);
		strncpy(iuv.impl_use.lvinfo3, &compID, 1);
		pad_string(iuv.impl_use.lvinfo1, 1, 36, '\0');
		pad_string(iuv.impl_use.lvinfo2, 1, 36, '\0');
		pad_string(iuv.impl_use.lvinfo3, 1, 36, '\0');
		char c = 1;
		strncpy(iuv.impl_use.lvinfo1 + 35, &c, 1);
		strncpy(iuv.impl_use.lvinfo2 + 35, &c, 1);
		strncpy(iuv.impl_use.lvinfo3 + 35, &c, 1);
		iuv.impl_use.impl_id.flags = 0;
		strncpy(iuv.impl_use.impl_id.ident, dvd_gen, strlen(dvd_gen));
		pad_string(iuv.impl_use.impl_id.ident, strlen(dvd_gen), 23, '\0');
		pad_string(iuv.impl_use.impl_id.ident_suffix, 0, 8, '\0');
		pad_string((char*)iuv.impl_use.impl_use, 0, 128, '\0');
		fill_tag_checksum(iuv_tag, &iuv);
		sm.write_sector<ImplUseVolumeDescriptor>(f, &iuv, sizeof(ImplUseVolumeDescriptor));
#pragma endregion

		// Write partition descriptor
#pragma region PartitionDescriptor writing
		PartitionDescriptor pd;
		DescriptorTag& pd_tag = pd.tag;
		pd_tag.tag_ident = cur_tag_ident++;
		pd_tag.desc_version = cur_tag_desc_ver;
		pd_tag.desc_crc_len = sizeof(PartitionDescriptor) - sizeof(DescriptorTag);
		pd_tag.tag_location = sm.get_current_sector();
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		pd.vol_desc_seq_num = cur_vol_desc_seq_num++;
		pd.part_flags = 1;
		pd.part_num = 0;
		pd.part_contents.flags = 2;
		auto nsr = "+NSR02";
		strncpy(pd.part_contents.ident, nsr, strlen(nsr));
		pad_string(pd.part_contents.ident, strlen(nsr), 23, '\0');
		pad_string(pd.part_contents.ident_suffix, 0, 8, '\0');
		pad_string(pd.part_cont_use, 0, 128, '\0');
		pd.access_type = 1;
		pd.part_start_loc = sm.get_partition_start_sector();
		pd.part_len = sm.get_total_sectors() - sm.get_partition_start_sector() - 1;
		pd.impl_ident.flags = 0;
		strncpy(pd.impl_ident.ident, dvd_gen, strlen(dvd_gen));
		pad_string(pd.impl_ident.ident, strlen(dvd_gen), 23, '\0');
		pad_string(pd.impl_ident.ident_suffix, 0, 8, '\0');
		pad_string((char*)pd.impl_use, 0, 128, '\0');
		pad_string((char*)pd.reserved, 0, 156, '\0');
		fill_tag_checksum(pd_tag, &pd);
		sm.write_sector<PartitionDescriptor>(f, &pd, sizeof(PartitionDescriptor));
#pragma endregion

		// Write logical volume descriptor
#pragma region LogicalVolumeDescriptor writing
		LogicalVolumeDescriptor lv;
		DescriptorTag& lv_tag = lv.tag;
		lv_tag.tag_ident = cur_tag_ident++;
		lv_tag.desc_version = cur_tag_desc_ver;
		lv_tag.desc_crc_len = sizeof(LogicalVolumeDescriptor) - sizeof(DescriptorTag);
		lv_tag.tag_location = sm.get_current_sector();
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		lv.vol_desc_seq_num = cur_vol_desc_seq_num++;
		lv.desc_char_set.char_set_type = 0;
		strncpy(lv.desc_char_set.char_set_info, osta, strlen(osta));
		pad_string(lv.desc_char_set.char_set_info, strlen(osta), 63, '\0');
		strncpy(lv.log_vol_ident, &compID, 1);
		strncpy(lv.log_vol_ident + 1, vol_ident, strlen(vol_ident));
		pad_string(lv.log_vol_ident, strlen(vol_ident) + 1, 128, '\0');
		strncpy(lv.log_vol_ident + 127, &vol_ident_len, 1);
		lv.log_block_size = 2048;
		lv.domain_ident.flags = 0;
		strncpy(lv.domain_ident.ident, osta_complient, strlen(osta_complient));
		pad_string(lv.domain_ident.ident, strlen(osta_complient), 23, '\0');
		strncpy(lv.domain_ident.ident_suffix, id_suff, 3);
		pad_string(lv.domain_ident.ident_suffix, 3, 8, '\0');
		// This unknown number is set in the log_vol_cont_use[1], according to the UDF standard this is not the way this field should be used :P
		char unkNum2 = 0x10; // Usually this indicates that the string is unicode compressed meaning one character takes 2 bytes, but no string is written to the upcoming field what so ever
		lv.log_vol_cont_use[0] = '\0';
		strncpy(lv.log_vol_cont_use + 1, &unkNum2, 1);
		pad_string(lv.log_vol_cont_use, 2, 16, '\0');
		// Table maps related stuff, not sure what for but these values are all hardcoded due to them being constant for all PS2 games
		lv.map_table_len = 6;
		lv.num_of_part_maps = 1;
		lv.impl_ident.flags = 0;
		strncpy(lv.impl_ident.ident, dvd_gen, strlen(dvd_gen));
		pad_string(lv.impl_ident.ident, strlen(dvd_gen), 23, '\0');
		pad_string(lv.impl_ident.ident_suffix, 0, 8, '\0');
		pad_string((char*)lv.impl_use, 0, 128, '\0');
		lv.int_seq_extent.length = 4096;
		lv.int_seq_extent.location = 64;
		lv.part_maps[0] = 1;
		lv.part_maps[1] = 6;
		lv.part_maps[2] = 1;
		pad_string((char*)lv.part_maps, 3, 6, '\0');
		fill_tag_checksum(lv_tag, &lv);
		sm.write_sector<LogicalVolumeDescriptor>(f, &lv, sizeof(LogicalVolumeDescriptor));
#pragma endregion

		// Write unallocated space descriptor
#pragma region UnallocatedSpaceDescriptor
		UnallocatedSpaceDescriptor usd;
		DescriptorTag& usd_tag = usd.tag;
		usd_tag.tag_ident = cur_tag_ident++;
		usd_tag.desc_version = cur_tag_desc_ver;
		usd_tag.desc_crc_len = sizeof(UnallocatedSpaceDescriptor) - sizeof(DescriptorTag);
		usd_tag.tag_location = sm.get_current_sector();
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		usd.vol_desc_seq_num = cur_vol_desc_seq_num++;
		usd.alloc_desc.length = 0;
		usd.alloc_desc.location = 0;
		usd.num_of_alloc_desc = 0;
		fill_tag_checksum(usd_tag, &usd);
		sm.write_sector<UnallocatedSpaceDescriptor>(f, &usd, sizeof(UnallocatedSpaceDescriptor));
#pragma endregion

		// Write terminating descriptor
		TerminatingDescriptor td;
		DescriptorTag& td_tag = td.tag;
		td_tag.tag_ident = cur_tag_ident++;
		td_tag.desc_version = cur_tag_desc_ver;
		td_tag.desc_crc_len = sizeof(TerminatingDescriptor) - sizeof(DescriptorTag);
		td_tag.tag_location = sm.get_current_sector();
		fill_tag_checksum(td_tag, &td);
		sm.write_sector<TerminatingDescriptor>(f, &td, sizeof(TerminatingDescriptor));

		// Write trailing logical sectors
		for (int j = 0; j < 10; ++j) {
			auto pad = '\0';
			sm.write_sector<char>(f, &pad, 1);
		}
	}

	// Write logical volume integrity descriptor
#pragma region LogicalVolumeIntegrityDescriptor writing
	LogicalVolumeIntegrityDescriptor lvi;
	DescriptorTag& lvi_tag = lvi.tag;
	lvi_tag.tag_ident = 9;
	lvi_tag.desc_version = 2;
	lvi_tag.desc_crc_len = sizeof(LogicalVolumeIntegrityDescriptor) - sizeof(DescriptorTag);
	lvi_tag.tag_location = sm.get_current_sector();
	// Tag checksum and its desc crc fields are written after descriptor has been filled
	lvi.rec_date_and_time = twins_creation_time;
	lvi.integ_type = 1;
	lvi.next_integ_extent.length = 0;
	lvi.next_integ_extent.location = 0;
	pad_string(lvi.log_vol_cont_use, 0, 4, (char)0xFF);
	pad_string(lvi.log_vol_cont_use, 4, 32, '\0');
	lvi.num_of_partitions = 1;
	lvi.len_of_impl_use = 48; // Constant for PS2 discs
	lvi.free_space_table = 0;
	lvi.size_table = sm.get_total_sectors() - sm.get_partition_start_sector() - 1;
	lvi.impl_use.impl_id.flags = 0;
	strncpy(lvi.impl_use.impl_id.ident, dvd_gen, strlen(dvd_gen));
	pad_string(lvi.impl_use.impl_id.ident, strlen(dvd_gen), 23, '\0');
	pad_string(lvi.impl_use.impl_id.ident_suffix, 0, 8, '\0');
	lvi.impl_use.num_of_files = sm.get_total_files();
	lvi.impl_use.num_of_dirs = sm.get_total_directories();
	uint min_udf_ver = 258; // Indicates version 2.58? of UDF standard for some reason this is the version PS2 games set when OSTA only provides version 2.50
	lvi.impl_use.min_udf_read_rev = min_udf_ver;
	lvi.impl_use.min_udf_write_rev = min_udf_ver;
	lvi.impl_use.max_udf_write_rev = min_udf_ver;
	lvi.impl_use.impl_use[0] = '\0';
	lvi.impl_use.impl_use[1] = '\0';
	fill_tag_checksum(lvi_tag, &lvi);
	sm.write_sector<LogicalVolumeIntegrityDescriptor>(f, &lvi, sizeof(LogicalVolumeIntegrityDescriptor));
#pragma endregion


	// Write terminating descriptor
	TerminatingDescriptor td;
	DescriptorTag& td_tag = td.tag;
	td_tag.tag_ident = 8;
	td_tag.desc_version = 2;
	td_tag.desc_crc_len = sizeof(TerminatingDescriptor) - sizeof(DescriptorTag);
	td_tag.tag_location = sm.get_current_sector();
	fill_tag_checksum(td_tag, &td);
	sm.write_sector<TerminatingDescriptor>(f, &td, sizeof(TerminatingDescriptor));

	// Skip reserved sectors until sector 256
	for (int i = 0; i < 190; ++i) {
		auto pad = '\0';
		sm.write_sector<char>(f, &pad, 1);
	}

	// Write anchor volume descriptor pointer
#pragma region AnchorVolumeDescriptorPointer writing
	AnchorVolumeDescriptorPointer avd;
	DescriptorTag& avd_tag = avd.tag;
	avd_tag.tag_ident = 2;
	avd_tag.desc_version = 2;
	avd_tag.desc_crc_len = sizeof(AnchorVolumeDescriptorPointer) - sizeof(DescriptorTag);
	avd_tag.tag_location = sm.get_current_sector();
	// Tag checksum and its desc crc fields are written after descriptor has been filled
	avd.main_vol_desc_seq_extent.length = 0x8000;
	avd.main_vol_desc_seq_extent.location = 32;
	avd.reserve_vol_desc_seq_extent.length = 0x8000;
	avd.reserve_vol_desc_seq_extent.location = 48;
	pad_string((char*)avd.reserved, 0, 480, '\0');
	fill_tag_checksum(avd_tag, &avd);
	sm.write_sector<AnchorVolumeDescriptorPointer>(f, &avd, sizeof(AnchorVolumeDescriptorPointer));
#pragma endregion

#pragma region Path table L/M writing
	// Hell yeah, I love my pure C malloc
	uint ptz = get_path_table_size(ft);
	char* path_table_buffer = (char*)malloc(ptz); // Buffer where all the path stuff is written to
	memset(path_table_buffer, 0, ptz);

	// Write path table L and option L
	fill_path_table(path_table_buffer, ft);
	sm.write_sector<char>(f, path_table_buffer, ptz);
	sm.write_sector<char>(f, path_table_buffer, ptz);

	// Write path table M and option M
	fill_path_table(path_table_buffer, ft, true);
	sm.write_sector<char>(f, path_table_buffer, ptz);
	sm.write_sector<char>(f, path_table_buffer, ptz);
	free(path_table_buffer);
#pragma endregion


	// Write directory records
#pragma region DirectoryRecord sectors writing
	auto directories = sm.get_total_directories();
	// At all times present . and .. navigation folders
	DirectoryRecord nav_this;
	nav_this.dir_rec_len = 48;
	nav_this.loc_of_ext_lsb = 261;
	nav_this.loc_of_ext_msb = changeEndianness32(261);
	nav_this.data_len_lsb = root_len;
	nav_this.data_len_msb = changeEndianness32(root_len);
	nav_this.red_date_and_time[0] = 120;
	nav_this.red_date_and_time[1] = 8;
	nav_this.red_date_and_time[2] = 25;
	nav_this.red_date_and_time[3] = 11;
	nav_this.red_date_and_time[4] = 30;
	nav_this.red_date_and_time[5] = '\0';
	nav_this.red_date_and_time[6] = '\0';
	nav_this.flags = 2;
	nav_this.vol_seq_num_lsb = 1;
	nav_this.vol_seq_num_msb = changeEndianness16(1);
	nav_this.file_ident_len = 1;
	nav_this.file_ident = '\0';
	DirectoryRecord nav_prev;
	nav_prev.dir_rec_len = 48;
	nav_prev.loc_of_ext_lsb = 261;
	nav_prev.loc_of_ext_msb = changeEndianness32(261);
	nav_prev.data_len_lsb = root_len;
	nav_prev.data_len_msb = changeEndianness32(root_len);
	nav_prev.red_date_and_time[0] = 120;
	nav_prev.red_date_and_time[1] = 8;
	nav_prev.red_date_and_time[2] = 25;
	nav_prev.red_date_and_time[3] = 11;
	nav_prev.red_date_and_time[4] = 30;
	nav_prev.red_date_and_time[5] = '\0';
	nav_prev.red_date_and_time[6] = '\0';
	nav_prev.flags = 2;
	nav_prev.vol_seq_num_lsb = 1;
	nav_prev.vol_seq_num_msb = changeEndianness16(1);
	nav_prev.file_ident_len = 1;
	nav_prev.file_ident = 1;
	auto cur_tree = ft;
	FileTreeNode* cur_dir = nullptr;
	auto prev_dir_len = root_len;
	for (int i = 0; i < directories; ++i) {
		auto needed_memory = 96; // Amount of needed memory for a sector
		auto index = 0;
		auto dir_len = 0x800;// 0x30 * 2U;
		// Recalculate the data len of the current directory tree for nav dirs as well as set their new LBA
		if (cur_dir != nullptr) {
			/*for (auto node : cur_tree->tree) {
				dir_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32);
			}*/
			nav_this.data_len_lsb = dir_len;
			nav_this.data_len_msb = changeEndianness32(dir_len);
			nav_this.loc_of_ext_lsb = sm.get_file_sector(cur_dir);
			nav_this.loc_of_ext_msb = changeEndianness32(sm.get_file_sector(cur_dir));
			if (cur_dir->parent != nullptr) {
				auto par_dir_len = 0x800;/*0x30 * 2U;
				for (auto node : cur_dir->parent->next->tree) {
					par_dir_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32);
				}*/
				nav_prev.data_len_lsb = par_dir_len;
				nav_prev.data_len_msb = changeEndianness32(par_dir_len);
				nav_prev.loc_of_ext_lsb = sm.get_file_sector(cur_dir->parent);
				nav_prev.loc_of_ext_msb = changeEndianness32(sm.get_file_sector(cur_dir->parent));
			}
			else {
				nav_prev.loc_of_ext_lsb = 261;
				nav_prev.loc_of_ext_msb = changeEndianness32(261);
				nav_prev.data_len_lsb = root_len;
				nav_prev.data_len_msb = changeEndianness32(root_len);
			}
		}

		// Fill in the directory
		DirectoryRecord* dir_rec = new DirectoryRecord[cur_tree->tree.size()];
		std::vector<std::string> file_names_buf;
		std::vector<FileTreeNode*> file_buf;
		std::vector<FileTreeNode*> dir_buf;
		for (auto node : cur_tree->tree) {
			// Need to process directories in root first, kick files in a buffer for now
			if (node->file->IsDirectory() && cur_dir == nullptr) {
				auto file = node->file;
				DirectoryRecord& rec = dir_rec[index++];
				rec.dir_rec_len = 48 + file->GetName().size() + file->GetName().size() % 2;
				needed_memory += rec.dir_rec_len;
				rec.loc_of_ext_lsb = sm.get_file_sector(node);
				rec.loc_of_ext_msb = changeEndianness32(sm.get_file_sector(node));
				auto rec_len = 0x800;/*0x30 * 2U;
				for (auto node : node->next->tree) {
					rec_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32);
				}*/
				rec.data_len_lsb = rec_len;
				rec.data_len_msb = changeEndianness32(rec_len);
				rec.red_date_and_time[0] = 120;
				rec.red_date_and_time[1] = 8;
				rec.red_date_and_time[2] = 25;
				rec.red_date_and_time[3] = 11;
				rec.red_date_and_time[4] = 30;
				rec.red_date_and_time[5] = '\0';
				rec.red_date_and_time[6] = '\0';
				rec.flags = 2;
				rec.vol_seq_num_lsb = 1;
				rec.vol_seq_num_msb = changeEndianness16(1);
				rec.file_ident_len = file->GetName().size();
				rec.file_ident = '\0'; // These are set in memory directly
				auto f_name = file->GetName();
				auto _pad = '\0';
				if (rec.file_ident_len % 2 != 0) f_name.append(&_pad);
				std::transform(f_name.begin(), f_name.end(), f_name.begin(), ::toupper);
				file_names_buf.push_back(f_name);
			}
			else if (node->file->IsDirectory()) {
				dir_buf.push_back(node);
			}
			else {
				file_buf.push_back(node);
			}
		}
		auto a = 0;
		a++;
		// Fill in the files
		for (auto node : file_buf) {
			auto file = node->file;
			DirectoryRecord& rec = dir_rec[index++];
			rec.dir_rec_len = 50 + file->GetName().size() + file->GetName().size() % 2;
			needed_memory += rec.dir_rec_len;
			rec.loc_of_ext_lsb = sm.get_file_sector(node);
			rec.loc_of_ext_msb = changeEndianness32(sm.get_file_sector(node));
			rec.data_len_lsb = file->GetSize();
			rec.data_len_msb = changeEndianness32(file->GetSize());
			rec.red_date_and_time[0] = 120;
			rec.red_date_and_time[1] = 8;
			rec.red_date_and_time[2] = 25;
			rec.red_date_and_time[3] = 11;
			rec.red_date_and_time[4] = 30;
			rec.red_date_and_time[5] = '\0';
			rec.red_date_and_time[6] = '\0';
			rec.flags = 0;
			rec.vol_seq_num_lsb = 1;
			rec.vol_seq_num_msb = changeEndianness16(1);
			rec.file_ident_len = file->GetName().size() + 2;
			rec.file_ident = '\0'; // These are set in memory directly
			auto f_name = file->GetName().append(";1");
			auto _pad = '\0';
			if (rec.file_ident_len % 2 != 0) f_name.append(&_pad);
			std::transform(f_name.begin(), f_name.end(), f_name.begin(), ::toupper);
			file_names_buf.push_back(f_name);
		}
		// In every other folder than root fill directories last
		for (auto node : dir_buf) {
			auto file = node->file;
			DirectoryRecord& rec = dir_rec[index++];
			rec.dir_rec_len = 48 + file->GetName().size() + file->GetName().size() % 2;
			needed_memory += rec.dir_rec_len;
			rec.loc_of_ext_lsb = sm.get_file_sector(node);
			rec.loc_of_ext_msb = changeEndianness32(sm.get_file_sector(node));
			auto rec_len = 0x800;/*0x30 * 2U;
			for (auto node : node->next->tree) {
				rec_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32);
			}*/
			rec.data_len_lsb = rec_len;
			rec.data_len_msb = changeEndianness32(rec_len);
			rec.red_date_and_time[0] = 120;
			rec.red_date_and_time[1] = 8;
			rec.red_date_and_time[2] = 25;
			rec.red_date_and_time[3] = 11;
			rec.red_date_and_time[4] = 30;
			rec.red_date_and_time[5] = '\0';
			rec.red_date_and_time[6] = '\0';
			rec.flags = 2;
			rec.vol_seq_num_lsb = 1;
			rec.vol_seq_num_msb = changeEndianness16(1);
			rec.file_ident_len = file->GetName().size();
			rec.file_ident = '\0'; // These are set in memory directly
			auto f_name = file->GetName();
			auto _pad = '\0';
			if (rec.file_ident_len % 2 != 0) f_name.append(&_pad);
			std::transform(f_name.begin(), f_name.end(), f_name.begin(), ::toupper);
			file_names_buf.push_back(f_name);
		}

		// If a single directory needs more than one sector for elements then I am sorry but you kinda gonna have to create a child directory :^)
		assert(needed_memory < 2048);

		// Write the records to memory
		char* buffer = (char*)malloc(needed_memory);
		int offset = 0;
		// Write nav dir this
		memcpy(buffer, &nav_this, sizeof(DirectoryRecord));
		offset += sizeof(DirectoryRecord);
		memset(buffer + offset, 0, nav_this.dir_rec_len - sizeof(DirectoryRecord));
		offset += nav_this.dir_rec_len - sizeof(DirectoryRecord);
		// Write nav dir prev
		memcpy(buffer + offset, &nav_prev, sizeof(DirectoryRecord));
		offset += sizeof(DirectoryRecord);
		memset(buffer + offset, 0, nav_prev.dir_rec_len - sizeof(DirectoryRecord));
		offset += nav_prev.dir_rec_len - sizeof(DirectoryRecord);
		for (int j = 0; j < cur_tree->tree.size(); ++j) {
			DirectoryRecord& rec = dir_rec[j];
			// Write the header without the string
			memcpy(buffer + offset, &rec, sizeof(DirectoryRecord) - 1);
			offset += sizeof(DirectoryRecord) - 1;
			// Write the string
			strncpy(buffer + offset, file_names_buf[j].c_str(), rec.file_ident_len);
			offset += rec.file_ident_len;
			memset(buffer + offset, 0, rec.dir_rec_len - sizeof(DirectoryRecord) + 1 - rec.file_ident_len);
			offset += rec.dir_rec_len - sizeof(DirectoryRecord) + 1 - rec.file_ident_len;
		}

		if (needed_memory - offset > 0) {
			memset(buffer + offset, 0, needed_memory - offset);
		}

		// Write the sector and free the memory
		sm.write_sector<char>(f, buffer, needed_memory);
		free(buffer);
		delete[] dir_rec;

		// Update current tree
		if (i != directories - 1) {
			cur_dir = sm.get_directories()[i];
			cur_tree = cur_dir->next;
		}
	}
#pragma endregion

	// Write file set descriptor, after this descriptor and its terminator starts special LBA addressing for tags and other stuff, this descriptor is at LBA 0
#pragma region FileSetDescriptor writing
	FileSetDescriptor fs;
	DescriptorTag& fs_tag = fs.tag;
	fs_tag.tag_ident = 0x100;
	fs_tag.desc_version = 2;
	fs_tag.desc_crc_len = sizeof(FileSetDescriptor) - sizeof(DescriptorTag);
	fs_tag.tag_location = 0;
	// Tag checksum and its desc crc fields are written after descriptor has been filled
	fs.rec_date_and_time = twins_creation_time;
	fs.inter_level = 3;
	fs.max_inter_level = 3;
	fs.char_set_list = 1;
	fs.max_char_set_list = 1;
	fs.file_set_num = 0;
	fs.file_set_desc_num = 0;
	fs.log_vol_ident_char_set.char_set_type = 0;
	strncpy(fs.log_vol_ident_char_set.char_set_info, osta, strlen(osta));
	pad_string(fs.log_vol_ident_char_set.char_set_info, strlen(osta), 63, '\0');
	strncpy(fs.log_vol_ident, &compID, 1);
	strncpy(fs.log_vol_ident + 1, vol_ident, strlen(vol_ident));
	pad_string(fs.log_vol_ident, strlen(vol_ident) + 1, 128, '\0');
	strncpy(fs.log_vol_ident + 127, &vol_ident_len, 1);
	fs.file_set_char_set.char_set_type = 0;
	strncpy(fs.file_set_char_set.char_set_info, osta, strlen(osta));
	pad_string(fs.file_set_char_set.char_set_info, strlen(osta), 63, '\0');
	auto ps2_set = "PLAYSTATION2 DVD-ROM FILE SET";
	strncpy(fs.file_set_ident, &compID, 1);
	strncpy(fs.file_set_ident + 1, ps2_set, strlen(ps2_set));
	char ps2_set_len = strlen(ps2_set) + 1;
	pad_string(fs.file_set_ident, strlen(ps2_set) + 1, 32, '\0');
	strncpy(fs.file_set_ident + 31, &ps2_set_len, 1);
	pad_string(fs.copy_file_ident, 0, 32, '\0');
	pad_string(fs.abstr_file_ident, 0, 32, '\0');
	fs.root_dir_icb.extent_len = 0x13C;
	fs.root_dir_icb.extent_loc.part_ref_num = 0;
	fs.root_dir_icb.extent_loc.log_block_num = 2 + sm.get_total_directories(); // This is the amount of sectors past this descriptor
	pad_string((char*)fs.root_dir_icb.impl_use, 0, 6, '\0');
	fs.domain_ident.flags = 0;
	strncpy(fs.domain_ident.ident, osta_complient, strlen(osta_complient));
	pad_string(fs.domain_ident.ident, strlen(osta_complient), 23, '\0');
	strncpy(fs.domain_ident.ident_suffix, id_suff, 3);
	pad_string(fs.domain_ident.ident_suffix, 3, 8, '\0');
	fs.next_extent.extent_len = 0;
	fs.next_extent.extent_loc.log_block_num = 0;
	fs.next_extent.extent_loc.part_ref_num = 0;
	pad_string((char*)fs.next_extent.impl_use, 0, 6, '\0');
	pad_string((char*)fs.reserved, 0, 48, '\0');
	fill_tag_checksum(fs_tag, &fs);
	sm.write_sector<FileSetDescriptor>(f, &fs, sizeof(FileSetDescriptor));
#pragma endregion


	// Write terminating descriptor
	td_tag.tag_location = sm.get_current_sector();
	sm.write_sector<TerminatingDescriptor>(f, &td, sizeof(TerminatingDescriptor));
	// Start special LBA counter
	ushort cur_spec_lba = 2;

	// Write file identifier descriptor
	std::map<FileTree*, unsigned int> dir_file_ident_size_map; // Used for directory entries
#pragma region FileIdentifierDescriptors writing
	FileIdentifierDescriptor fi_root;
	DescriptorTag& fi_root_tag = fi_root.tag;
	fi_root_tag.tag_ident = 0x101;
	fi_root_tag.desc_version = 2;
	fi_root_tag.desc_crc_len = 0x18;
	// Tag checksum etc. etc. you know the drill
	fi_root.file_ver_num = 1;
	fi_root.file_chars = 0xA;
	fi_root.len_of_file_ident = 0;
	fi_root.icb.extent_len = 0x13C;
	fi_root.icb.extent_loc.log_block_num = cur_spec_lba + directories; // The only part that changes for root FID in other directories, also its tag CRC
	fi_root.icb.extent_loc.part_ref_num = 0;
	pad_string((char*)fi_root.icb.impl_use, 0, 6, '\0');
	fi_root.len_of_impl_use = 0;
	fi_root.impl_use = '\0';
	fi_root.file_ident = '\0';
	cur_tree = ft;
	cur_dir = nullptr;
	for (int i = 0; i < directories; ++i) {
		// Update root's lba and its checksum
		fi_root_tag.tag_location = cur_spec_lba;
		fill_tag_checksum(fi_root_tag, &fi_root);
		if (cur_dir != nullptr) {
			fi_root.icb.extent_loc.log_block_num = sm.get_file_lba(cur_dir);
		}

		int needed_memory = sizeof(FileIdentifierDescriptor); // Root descriptor is always included
		int index = 0;
		FileIdentifierDescriptor* file_idents = new FileIdentifierDescriptor[cur_tree->tree.size()];
		std::vector<FileTreeNode*> files;
		std::vector<FileTreeNode*> dir_buf;
		std::vector<std::string> file_names;
		std::vector<std::pair<char*, unsigned int>> buffers;
		// Write folders first and files later
		for (auto node : cur_tree->tree) {
			if (node->file->IsDirectory() && cur_dir == nullptr) {
				needed_memory += fill_fid(sm, file_idents[index++], node, cur_spec_lba, buffers);
			}
			else if (node->file->IsDirectory()) {
				dir_buf.push_back(node);
			}
			else {
				files.push_back(node);
			}
		}

		// Fill in the files
		for (auto file : files) {
			needed_memory += fill_fid(sm, file_idents[index++], file, cur_spec_lba, buffers);
		}

		for (auto node : dir_buf) {
			needed_memory += fill_fid(sm, file_idents[index++], node, cur_spec_lba, buffers);
		}

		// Write the data to the sector
		char* buffer = (char*)malloc(needed_memory);
		int offset = 0;
		memcpy(buffer, &fi_root, sizeof(FileIdentifierDescriptor));
		offset += sizeof(FileIdentifierDescriptor);
		for (auto p : buffers) {
			memcpy(buffer + offset, p.first, p.second);
			offset += p.second;
			free(p.first);
		}
		if (needed_memory - offset > 0) {
			memset(buffer + offset, 0, needed_memory - offset);
		}
		dir_file_ident_size_map.emplace(std::pair<FileTree*, unsigned int>(cur_tree, needed_memory));
		sm.write_sector<char>(f, buffer, needed_memory);
		cur_spec_lba++;
		free(buffer);

		// Update current tree
		if (i != directories - 1) {
			cur_dir = sm.get_directories()[i];
			cur_tree = cur_dir->next;
		}
	}
#pragma endregion

	char iuea_free_impl[4] = { 0x61, 0x5, 0x0, 0x0 };
	char iuea_cgms_impl[8] = { 0x49, 0x5, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
	// Write file entries for directories
#pragma region FileEntries(Directories) writing
	FileEntry root_fe;
	DescriptorTag& fe_tag = root_fe.tag;
	fe_tag.tag_ident = 0x105;
	fe_tag.desc_version = 2;
	fe_tag.desc_crc_len = sizeof(FileEntry) - sizeof(DescriptorTag);
	fe_tag.tag_location = cur_spec_lba;
	ICBTag& fe_icb = root_fe.icb_tag;
	fe_icb.prior_rec_num_of_direct_entries = 0;
	fe_icb.strategy_type = 4;
	pad_string((char*)fe_icb.strat_param, 0, 2, '\0');
	fe_icb.num_of_entries = 1;
	fe_icb.file_type = 4;
	fe_icb.parent_icb_loc.log_block_num = 0;
	fe_icb.parent_icb_loc.part_ref_num = 0;
	fe_icb.flags = 0x630;
	root_fe.uid = -1;
	root_fe.gid = -1;
	root_fe.permissions = 0x14A5;
	root_fe.file_link_cnt = ft->get_dir_links();
	root_fe.record_format = 0;
	root_fe.record_disp_attrib = 0;
	root_fe.record_len = 0;
	root_fe.info_len = dir_file_ident_size_map.at(ft);
	root_fe.log_blocks_rec = 1;
	root_fe.access_time = twins_creation_time;
	root_fe.mod_time = twins_creation_time;
	root_fe.attrib_time = twins_creation_time;
	root_fe.checkpoint = 1;
	root_fe.ext_attrib_icb.extent_len = 0;
	root_fe.ext_attrib_icb.extent_loc.log_block_num = 0;
	root_fe.ext_attrib_icb.extent_loc.part_ref_num = 0;
	pad_string((char*)root_fe.ext_attrib_icb.impl_use, 0, 6, '\0');
	root_fe.impl_ident.flags = 0;
	strncpy(root_fe.impl_ident.ident, dvd_gen, strlen(dvd_gen));
	pad_string(root_fe.impl_ident.ident, strlen(dvd_gen), 23, '\0');
	pad_string(root_fe.impl_ident.ident_suffix, 0, 8, '\0');
	root_fe.unique_id = 0; // Only for root, rest start from 0x10
	EA_HeaderDescriptor& ea = root_fe.ext_attrib_hd;
	ea.tag.tag_ident = 0x106;
	ea.tag.desc_version = 2;
	ea.tag.desc_crc_len = 8;
	ea.tag.tag_location = cur_spec_lba;
	fill_tag_checksum(ea.tag, &ea);
	root_fe.iuea_udf_free.impl_ident.flags = 0;
	strncpy((char*)root_fe.iuea_udf_free.impl_ident.ident, udf_free_ea, strlen(udf_free_ea));
	pad_string((char*)root_fe.iuea_udf_free.impl_ident.ident, strlen(udf_free_ea), 23, '\0');
	strncpy((char*)root_fe.iuea_udf_free.impl_ident.ident_suffix, id_suff, 2);
	pad_string((char*)root_fe.iuea_udf_free.impl_ident.ident_suffix, 2, 8, '\0');
	strncpy((char*)root_fe.iuea_udf_free.impl_use, iuea_free_impl, 4);
	root_fe.iuea_udf_cgms.impl_ident.flags = 0;
	strncpy((char*)root_fe.iuea_udf_cgms.impl_ident.ident, udf_cgms_info, strlen(udf_cgms_info));
	pad_string((char*)root_fe.iuea_udf_cgms.impl_ident.ident, strlen(udf_cgms_info), 23, '\0');
	strncpy((char*)root_fe.iuea_udf_cgms.impl_ident.ident_suffix, id_suff, 2);
	pad_string((char*)root_fe.iuea_udf_cgms.impl_ident.ident_suffix, 2, 8, '\0');
	strncpy((char*)root_fe.iuea_udf_cgms.impl_use, iuea_cgms_impl, 8);
	root_fe.alloc_desc.info_len = root_fe.info_len; // Size of file identifier descriptor for folders, size of file for files
	root_fe.alloc_desc.log_block_num = 2; // Always 2 for root, local sector for files
	fill_tag_checksum(fe_tag, &root_fe);
	sm.write_sector<FileEntry>(f, &root_fe);
	cur_spec_lba++;
	auto dirs = sm.get_directories();
	ulong unique_id = 0x10;
	auto log_block_num = 3;
	for (auto dir : dirs) {
		FileEntry fe;
		DescriptorTag& fe_tag = fe.tag;
		fe_tag.tag_ident = 0x105;
		fe_tag.desc_version = 2;
		fe_tag.desc_crc_len = 0x12C;
		fe_tag.tag_location = cur_spec_lba;
		ICBTag& fe_icb = fe.icb_tag;
		fe_icb.prior_rec_num_of_direct_entries = 0;
		fe_icb.strategy_type = 4;
		pad_string((char*)fe_icb.strat_param, 0, 2, '\0');
		fe_icb.num_of_entries = 1;
		fe_icb.file_type = 4;
		fe_icb.parent_icb_loc.log_block_num = 0;
		fe_icb.parent_icb_loc.part_ref_num = 0;
		fe_icb.flags = 0x630;
		fe.uid = -1;
		fe.gid = -1;
		fe.permissions = 0x14A5;
		fe.file_link_cnt = dir->next->get_dir_links();
		fe.record_format = 0;
		fe.record_disp_attrib = 0;
		fe.record_len = 0;
		fe.info_len = dir_file_ident_size_map.at(dir->next);
		fe.log_blocks_rec = 1;
		fe.access_time = twins_creation_time;
		fe.mod_time = twins_creation_time;
		fe.attrib_time = twins_creation_time;
		fe.checkpoint = 1;
		fe.ext_attrib_icb.extent_len = 0;
		fe.ext_attrib_icb.extent_loc.log_block_num = 0;
		fe.ext_attrib_icb.extent_loc.part_ref_num = 0;
		pad_string((char*)fe.ext_attrib_icb.impl_use, 0, 6, '\0');
		fe.impl_ident.flags = 0;
		strncpy(fe.impl_ident.ident, dvd_gen, strlen(dvd_gen));
		pad_string(fe.impl_ident.ident, strlen(dvd_gen), 23, '\0');
		pad_string(fe.impl_ident.ident_suffix, 0, 8, '\0');
		fe.unique_id = unique_id; // 0 for root, rest start from 0x10 and count up
		EA_HeaderDescriptor& ea = fe.ext_attrib_hd;
		ea.tag.tag_ident = 0x106;
		ea.tag.desc_version = 2;
		ea.tag.desc_crc_len = 8;
		ea.tag.tag_location = cur_spec_lba;
		fill_tag_checksum(ea.tag, &ea);
		fe.iuea_udf_free.impl_ident.flags = 0;
		strncpy((char*)fe.iuea_udf_free.impl_ident.ident, udf_free_ea, strlen(udf_free_ea));
		pad_string((char*)fe.iuea_udf_free.impl_ident.ident, strlen(udf_free_ea), 23, '\0');
		strncpy((char*)fe.iuea_udf_free.impl_ident.ident_suffix, id_suff, 2);
		pad_string((char*)fe.iuea_udf_free.impl_ident.ident_suffix, 2, 8, '\0');
		strncpy((char*)fe.iuea_udf_free.impl_use, iuea_free_impl, 4);
		fe.iuea_udf_cgms.impl_ident.flags = 0;
		strncpy((char*)fe.iuea_udf_cgms.impl_ident.ident, udf_cgms_info, strlen(udf_cgms_info));
		pad_string((char*)fe.iuea_udf_cgms.impl_ident.ident, strlen(udf_cgms_info), 23, '\0');
		strncpy((char*)fe.iuea_udf_cgms.impl_ident.ident_suffix, id_suff, 2);
		pad_string((char*)fe.iuea_udf_cgms.impl_ident.ident_suffix, 2, 8, '\0');
		strncpy((char*)fe.iuea_udf_cgms.impl_use, iuea_cgms_impl, 8);
		fe.alloc_desc.info_len = fe.info_len; // Size of file identifier descriptor for folders, size of file for files
		fe.alloc_desc.log_block_num = log_block_num++; // Always 2 for root, local sector for files
		fill_tag_checksum(fe_tag, &fe);
		sm.write_sector<FileEntry>(f, &fe);
		cur_spec_lba++;
		unique_id++;
	}
#pragma endregion

	// Write file entries for files
	auto im_cxt = ImageContext();
	im_cxt.twins_creation_time = twins_creation_time;
	fill_file_fe(f, sm, unique_id, cur_spec_lba, im_cxt);

	write_file_tree(sm, f);
	
	update_progress(ProgressState::WRITE_END, program_progress.progress);
	// Write special pad sectors
	auto pad_sec = '\0';
	auto pad_secs = sm.get_pad_sectors();
	for (int i = 0; i < pad_secs; ++i) {
		sm.write_sector(f, &pad_sec);
	}

	// Write a special end of session descriptor?
	EndOfSessionDescriptor eos;
	DescriptorTag& eos_tag = eos.tag;
	eos_tag.tag_ident = 2;
	eos_tag.desc_version = 2;
	eos_tag.desc_crc_len = 2032;
	eos_tag.tag_location = sm.get_total_sectors() - 1;
	eos.alloc_desc1.info_len = 0x800;
	eos.alloc_desc1.log_block_num = 0x20;
	eos.alloc_desc2.info_len = 0x800;
	eos.alloc_desc2.log_block_num = 0x30;
	fill_tag_checksum(eos_tag, &eos);
	sm.write_sector(f, &eos);
}

void write_file_tree(SectorManager& sm, std::ofstream& f) {
	auto files = sm.get_files();
	auto progress_increment = 0.8 / sm.get_total_files();
	for (auto node : files) {
		update_progress(ProgressState::WRITE_FILES, program_progress.progress + progress_increment, node->file->GetName().c_str());
		std::ifstream in_f;
		in_f.open(node->file->GetPath(), std::ios_base::in | std::ios_base::binary);
		sm.write_file(f, in_f.rdbuf(), node->file->GetSize());
	}
}

void update_progress(ProgressState state, float progress, const char* file_name, bool finished) {
	std::lock_guard<std::mutex> guard(progress_mut);
	program_progress.new_file = false;
	program_progress.new_state = false;
	progress_dirty = true;
	if (state != program_progress.state) {
		program_progress.state = state;
		program_progress.new_state = true;
	}
	if (strlen(file_name) != 0) {
		program_progress.size = strlen(file_name);
		program_progress.file_name[program_progress.size] = '\0';
		strncpy(program_progress.file_name, file_name, program_progress.size);
		program_progress.size += 1;
		program_progress.new_file = true;
	}
	if (program_progress.progress != progress) {
		program_progress.progress = progress;
	}
	if ((program_progress.finished ^ finished) == 1) {
		program_progress.finished = finished;
	}
}

void _get_path_table_size(FileTreeNode* node, unsigned int& size) {
	for (auto node : node->next->tree) {
		if (node->file->IsDirectory()) {
			_get_path_table_size(node, size);
			size += 8 + node->file->GetName().size() + node->file->GetName().size() % 2;
		}
	}
}

unsigned int get_path_table_size(FileTree* ft) {
	auto size = 10U; // Start with 10 because Root directory is also included
	for (auto node : ft->tree) {
		if (node->file->IsDirectory()) {
			_get_path_table_size(node, size);
			size += 8 + node->file->GetName().size() + node->file->GetName().size() % 2;
		}
	}
	return size;
}

// Helper function for filling a string
void pad_string(char* str, int offset, int size, const char pad) {
	for (int i = 0; i < size - offset; ++i) {
		strncpy(str + offset + i, &pad, 1);
	}
}

// Offset and start_lba are changing due to their constant calling, yeah yeah C-like code in C++ shut up :P
void _fill_path_table(char* buffer, FileTreeNode* node, int& offset, uint& start_lba, ushort par_index, bool msb) {
	buffer[offset++] = node->file->GetName().size();
	buffer[offset++] = 0;
	uint* lba = (uint*)(buffer + offset);
	*lba = msb ? changeEndianness32(start_lba) : start_lba;
	start_lba++;
	offset += 4;
	ushort par_dir_num = msb ? changeEndianness16(par_index) : par_index;
	ushort* par_dir_num_ptr = (ushort*)(buffer + offset);
	*par_dir_num_ptr = par_dir_num;
	offset += 2;
	std::string upper = node->file->GetName();
	std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
	strncpy(buffer + offset, upper.c_str(), node->file->GetName().size());
	offset += node->file->GetName().size();
	// Add padding if dir name length is odd
	if (node->file->GetName().size() % 2 == 1) {
		buffer[offset++] = '\0';
	}
}

struct DirectoryDepth {
	FileTreeNode* node;
	int depth;
	int par_index;
};

void _explore_tree(FileTreeNode* node, std::vector<DirectoryDepth>& vec, int depth) {
	for (auto node : node->next->tree) {
		if (node->file->IsDirectory()) {
			_explore_tree(node, vec, depth + 1);
			DirectoryDepth dir_depth;
			dir_depth.depth = depth;
			dir_depth.node = node;
			vec.push_back(dir_depth);
		}
	}
}

void fill_path_table(char* buffer, FileTree* ft, bool msb) {
	// Fill in the root table
	buffer[0] = 1; // Ident len
	buffer[1] = 0; // ext_rec_attrib_len
	uint lba = msb ? changeEndianness32(261) : 261;
	uint* lba_ptr = (uint*)(buffer + 2);
	*lba_ptr = lba;
	ushort par_dir_num = msb ? changeEndianness16(1) : 1;
	ushort* par_dir_num_ptr = (ushort*)(buffer + 6);
	*par_dir_num_ptr = par_dir_num;
	buffer[8] = 0; // Root ident
	buffer[9] = 0; // Pad
	std::vector<DirectoryDepth> depths;
	std::map<FileTreeNode*, ushort> par_index_map;
	int depth = 0;
	// Explore the tree
	for (auto node : ft->tree) {
		if (node->file->IsDirectory()) {
			_explore_tree(node, depths, depth + 1);
			DirectoryDepth dir_depth;
			dir_depth.depth = depth;
			dir_depth.par_index = 1;
			dir_depth.node = node;
			depths.push_back(dir_depth);
			par_index_map.emplace(std::pair<FileTreeNode*, int>(node, 1));
		}
	}
	// Sort by depth
	std::sort(depths.begin(), depths.end(), [](DirectoryDepth& dir1, DirectoryDepth& dir2) {
		return dir1.depth < dir2.depth;
	});
	// Map node's children to a parent index
	if (depths.size() != 0) {
		ushort par_index = 2;
		for (const auto& depth : depths) {
			// Check for having children
			if (depth.node->next != nullptr) {
				for (const auto& node : depth.node->next->tree) {
					if (node->file->IsDirectory()) {
						par_index_map.emplace(std::pair<FileTreeNode*, int>(node, par_index));
					}
				}
			}
			par_index++;
		}
	}
	// Fill table based on depth
	if (depths.size() != 0) {
		int offset = 10;
		uint init_lba = 262;
		for (const auto& depth : depths) {
			_fill_path_table(buffer, depth.node, offset, init_lba, par_index_map.at(depth.node), msb);
		}
	}
}

// Helper function to fill FileIdentifierDescriptor struct
unsigned int fill_fid(SectorManager& sm, FileIdentifierDescriptor& fi, FileTreeNode* node, unsigned int cur_spec_lba, std::vector<std::pair<char*, unsigned int>>& buffers) {
	DescriptorTag& tag = fi.tag;
	auto file_name_size = node->file->GetName().size();
	auto file_str_len = file_name_size * 2 + 1; // 1 additional byte for unicode compression id
	auto struct_size = sizeof(FileIdentifierDescriptor) - 1 + file_str_len + (file_name_size % 2) * 2;
	tag.tag_ident = 0x101;
	tag.desc_version = 2;
	tag.tag_location = cur_spec_lba;
	tag.desc_crc_len = struct_size - sizeof(DescriptorTag);
	fi.file_ver_num = 1;
	fi.file_chars = node->file->IsDirectory() ? 2 : 0;
	fi.len_of_file_ident = file_str_len;
	fi.icb.extent_len = 0x13C;
	fi.icb.extent_loc.log_block_num = sm.get_file_lba(node);
	fi.icb.extent_loc.part_ref_num = 0;
	pad_string((char*)fi.icb.impl_use, 0, 6, '\0');
	fi.len_of_impl_use = 0;
	fi.impl_use = 0x10;
	fi.file_ident = '\0';
	// needed_memory += struct_size;
	auto f_buf_len = (file_name_size + file_name_size % 2) * 2;
	auto f_name_buf = new char[f_buf_len];
	std::string udf_comp({ 0x8 });
	udf_comp.append(node->file->GetName());
	if (file_name_size % 2 != 0) {
		udf_comp.append({'\0'});
	}
	UncompressUnicode(udf_comp.size(), (unsigned char*)udf_comp.c_str(), (unicode_t*)f_name_buf);
	char* str_buf = (char*)malloc(struct_size);
	// First write the whole main struct without descriptor tag to allow tag calculate the checksum
	memcpy(str_buf + sizeof(DescriptorTag), ((char*)&fi) + sizeof(DescriptorTag), sizeof(FileIdentifierDescriptor) - sizeof(DescriptorTag));
	memcpy(str_buf + sizeof(FileIdentifierDescriptor), f_name_buf, f_buf_len);
	fill_tag_checksum(tag, str_buf, struct_size);
	// Write the tag at the end
	memcpy(str_buf, &tag, sizeof(DescriptorTag));
	buffers.push_back(std::pair<char*, unsigned int>(str_buf, struct_size));
	// At the moment the only buffer we can free because it's copied everywhere necessary and not needed anymore
	delete[] f_name_buf;
	return struct_size;
}

// Helper function for writing File Entries for files
void fill_file_fe(std::ofstream& f, SectorManager& sm, ulong unique_id, ushort cur_spec_lba, ImageContext& context)
{
	auto files = sm.get_files();
	for (auto file : files) {
		FileEntry fe;
		DescriptorTag& fe_tag = fe.tag;
		fe_tag.tag_ident = 0x105;
		fe_tag.desc_version = 2;
		fe_tag.desc_crc_len = sizeof(FileEntry) - sizeof(DescriptorTag);
		fe_tag.tag_location = cur_spec_lba;
		ICBTag& fe_icb = fe.icb_tag;
		fe_icb.prior_rec_num_of_direct_entries = 0;
		fe_icb.strategy_type = 4;
		pad_string((char*)fe_icb.strat_param, 0, 2, '\0');
		fe_icb.num_of_entries = 1;
		fe_icb.file_type = 5;
		fe_icb.parent_icb_loc.log_block_num = 0;
		fe_icb.parent_icb_loc.part_ref_num = 0;
		fe_icb.flags = 0x630;
		fe.uid = -1;
		fe.gid = -1;
		fe.permissions = 0x14A5;
		fe.file_link_cnt = 1; // Always 1 for files
		fe.record_format = 0;
		fe.record_disp_attrib = 0;
		fe.record_len = 0;
		fe.info_len = file->file->GetSize();
		fe.log_blocks_rec = 1;
		fe.access_time = context.twins_creation_time;
		fe.mod_time = context.twins_creation_time;
		fe.attrib_time = context.twins_creation_time;
		fe.checkpoint = 1;
		fe.ext_attrib_icb.extent_len = 0;
		fe.ext_attrib_icb.extent_loc.log_block_num = 0;
		fe.ext_attrib_icb.extent_loc.part_ref_num = 0;
		pad_string((char*)fe.ext_attrib_icb.impl_use, 0, 6, '\0');
		fe.impl_ident.flags = 0;
		strncpy(fe.impl_ident.ident, context.dvd_gen, strlen(context.dvd_gen));
		pad_string(fe.impl_ident.ident, strlen(context.dvd_gen), 23, '\0');
		pad_string(fe.impl_ident.ident_suffix, 0, 8, '\0');
		fe.unique_id = unique_id; // 0 for root, rest start from 0x10 and count up
		EA_HeaderDescriptor& ea = fe.ext_attrib_hd;
		ea.tag.tag_ident = 0x106;
		ea.tag.desc_version = 2;
		ea.tag.desc_crc_len = 8;
		ea.tag.tag_location = cur_spec_lba;
		fill_tag_checksum(ea.tag, &ea);
		fe.iuea_udf_free.impl_ident.flags = 0;
		strncpy((char*)fe.iuea_udf_free.impl_ident.ident, context.udf_free_ea, strlen(context.udf_free_ea));
		pad_string((char*)fe.iuea_udf_free.impl_ident.ident, strlen(context.udf_free_ea), 23, '\0');
		strncpy((char*)fe.iuea_udf_free.impl_ident.ident_suffix, context.id_suff, 2);
		pad_string((char*)fe.iuea_udf_free.impl_ident.ident_suffix, 2, 8, '\0');
		strncpy((char*)fe.iuea_udf_free.impl_use, context.iuea_free_impl, 4);
		fe.iuea_udf_cgms.impl_ident.flags = 0;
		strncpy((char*)fe.iuea_udf_cgms.impl_ident.ident, context.udf_cgms_info, strlen(context.udf_cgms_info));
		pad_string((char*)fe.iuea_udf_cgms.impl_ident.ident, strlen(context.udf_cgms_info), 23, '\0');
		strncpy((char*)fe.iuea_udf_cgms.impl_ident.ident_suffix, context.id_suff, 2);
		pad_string((char*)fe.iuea_udf_cgms.impl_ident.ident_suffix, 2, 8, '\0');
		strncpy((char*)fe.iuea_udf_cgms.impl_use, context.iuea_cgms_impl, 8);
		fe.alloc_desc.info_len = fe.info_len; // Size of file identifier descriptor for folders, size of file for files
		fe.alloc_desc.log_block_num = sm.get_file_local_sector(file); // Always 2 for root, local sector for files
		fill_tag_checksum(fe_tag, &fe);
		sm.write_sector<FileEntry>(f, &fe);
		cur_spec_lba++;
		unique_id++;
	}
}

template<typename T>
void fill_tag_checksum(DescriptorTag& tag, T* buffer, unsigned int size)
{
	auto checksum = cksum(((unsigned char*)buffer) + sizeof(DescriptorTag), size - sizeof(DescriptorTag));
	tag.desc_crc = checksum;
	tag.tag_checksum = 0;
	auto tag_cksum = cksum_tag((unsigned char*)&tag, sizeof(DescriptorTag));
	tag.tag_checksum = tag_cksum;
}
