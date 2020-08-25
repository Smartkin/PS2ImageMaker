#include "pch.h"
#include "API.h"
#include "Directory.h"
#include "File.h"
#include "SectorManager.h"
#include "SectorDescriptors.h"
#include "Util.h"
#include <vector>
#include <thread>
#include <fstream>
#include <algorithm>
#include <map>
#include <cassert>

void pack(Progress* pr, const char* game_path, const char* dest_path);
void write_sectors(Progress* pr, std::ofstream& f, FileTree* ft);
void write_file_tree(Progress* pr, SectorManager& sm, std::ofstream& f, FileTree* ft);
unsigned int get_path_table_size(FileTree* ft);
void pad_string(char* str, int offset, int size, const char pad = ' ');
void fill_path_table(char* buffer, FileTree* ft, bool msb = false);

extern "C" Progress* start_packing(const char* game_path, const char* dest_path) {
	Progress* pr = new Progress();
	std::thread* thr = new std::thread(pack, pr, game_path, dest_path);
	return pr;
}

void pack(Progress* pr, const char* game_path, const char* dest_path) {
	Directory dir(game_path);
	FileTree* ft = dir.get_files(pr);
	std::sort(ft->tree.begin(), ft->tree.end(), [](FileTreeNode* a, FileTreeNode* b) {
		auto a_is_sys = strcmp(a->file->GetName().c_str(), "System.cnf") == 0;
		auto b_is_sys = strcmp(b->file->GetName().c_str(), "System.cnf") == 0;
		auto a_is_dir = a->file->IsDirectory();
		auto b_is_dir = b->file->IsDirectory();
		if (a_is_sys) {
			return true;
		}
		if (b_is_sys) {
			return false;
		}
		if (b_is_dir && a_is_dir) {
			return false;
		}
		if (!a_is_dir) {
			return true;
		}
		if (!b_is_dir) {
			return false;
		}
		return false;
	});
	update_progress_message(pr, "Finished enumerating files");
	std::ofstream image;
	image.open(dest_path, std::ios_base::binary | std::ios_base::out);
	write_sectors(pr, image, ft);
	pr->progress = 1.0;
}

// All the writing for each sector is packed into this single function instead of having each sector to be in its separate function
// biggest reason is because all sectors need a very strict ordering so instead of creating a seperate function for each
// they are just divided into section, additionally certain sector's data can depend on others
void write_sectors(Progress* pr, std::ofstream& f, FileTree* ft) {
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
	// Fill first 16 sectors with nothing as they are system stuff
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
	pvd.vol_set_size_lsb = 1;
	pvd.vol_set_size_msb = changeEndianness16(1);
	pvd.vol_seq_num_lsb = 1;
	pvd.vol_seq_num_msb = changeEndianness16(1);
	pvd.log_block_size_lsb = 2048;
	pvd.log_block_size_msb = changeEndianness16(2048);
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
	auto root_len = 0x30 * 2U;
	for (auto node : ft->tree) {
		// Directory record length is calculated as 0x30 * 2 + 0x30 * files + sum_of_the_names_lengths_of_all_files in that directory
		// Actual files need two extra characters because of having ;1 appended
		root_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32);
	}
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
	root_rec.flags = 0x2;
	root_rec.file_size_in_inter = '\0';
	root_rec.interleave_gap = '\0';
	root_rec.vol_seq_num_lsb = 1;
	root_rec.vol_seq_num_msb = changeEndianness16(1);
	root_rec.file_ident_len = 1;
	root_rec.file_ident = '\0';
	pad_string(pvd.volume_set_desc, 0, 128);
	
	strncpy(pvd.publisher_ident, publisher_ident, strlen(publisher_ident));
	pad_string(pvd.publisher_ident, strlen(publisher_ident), 128);
	
	strncpy(pvd.data_preparer_ident, data_prep, strlen(data_prep));
	pad_string(pvd.data_preparer_ident, strlen(data_prep), 128);
	
	strncpy(pvd.app_ident, app_ident, strlen(app_ident));
	pad_string(pvd.app_ident, strlen(app_ident), 128);
	
	strncpy(pvd.cop_ident, cop_ident, strlen(cop_ident));
	pad_string(pvd.cop_ident, strlen(cop_ident), 38);
	pad_string(pvd.abstract_ident, 0, 36);
	pad_string(pvd.bibl_ident, 0, 37);
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
	// Write twice the same descriptors for Main and RSRV
	for (int i = 0; i < 2; ++i) {
		ushort cur_tag_ident = 1;
		ushort cur_tag_desc_ver = 2;
		uint cur_vol_desc_seq_num = 0;
		// Write primary volume descriptor UDF	
#pragma region PrimaryVolumeDescriptor_UDF writing
		PrimaryVolumeDescriptor_UDF pvd;
		DescriptorTag& pvd_tag = pvd.tag; // Reference to currently written to descriptor tag
		pvd_tag.tag_ident = cur_tag_ident;
		pvd_tag.desc_version = cur_tag_desc_ver;
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		pvd_tag.desc_crc_len = 2032;
		pvd_tag.tag_location = sm.get_current_sector();
		pvd.vol_desc_seq_num = cur_vol_desc_seq_num++;
		pvd.prim_vol_desc_num = 0;
		char unkNum = 0x8; // This is put as the first char in vol_ident of every PS2 game, no clue why
		strncpy(pvd.vol_ident, &unkNum, 1);
		strncpy(pvd.vol_ident + 1, vol_ident, strlen(vol_ident));
		pad_string(pvd.vol_ident, strlen(vol_ident) + 1, 32, '\0');
		strncpy(pvd.vol_ident + 31, &vol_ident_len, 1);
		pvd.vol_seq_num = 1;
		pvd.max_vol_seq_num = 1;
		pvd.interchange_level = 2;
		pvd.max_inter_level = 2;
		pvd.character_set_list = 1;
		pvd.max_char_set_list = 1;
		strncpy(pvd.vol_set_ident, &unkNum, 1);
		auto encoded_date = "=0<58115SCEI"; // This is an encoded date of 11:35AM 24/08/2020 with SCEI appended
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
		// Just record date and time whenever Twinsanity PAL was released
		pvd.rec_date_and_time.microseconds = 0;
		pvd.rec_date_and_time.milliseconds = 0;
		pvd.rec_date_and_time.centiseconds = 0;
		pvd.rec_date_and_time.second = 47;
		pvd.rec_date_and_time.minute = 33;
		pvd.rec_date_and_time.hour = 7;
		pvd.rec_date_and_time.day = 3;
		pvd.rec_date_and_time.month = 9;
		pvd.rec_date_and_time.year = 2004;
		pvd.rec_date_and_time.type_and_timezone = 0x121C;
		pvd.impl_ident.flags = 0;
		strncpy(pvd.impl_ident.ident, dvd_gen, strlen(dvd_gen));
		pad_string(pvd.impl_ident.ident, strlen(dvd_gen), 23, '\0');
		pad_string(pvd.impl_ident.ident_suffix, 0, 8, '\0');
		pad_string((char*)pvd.impl_use, 0, 64, '\0');
		pvd.pred_vol_desc_seq_loc = 0;
		pad_string((char*)pvd.reserved, 0, 22, '\0');
		auto pvd_checksum = cksum(((unsigned char*)&pvd) + sizeof(DescriptorTag), sizeof(PrimaryVolumeDescriptor_UDF) - sizeof(DescriptorTag));
		pvd_tag.desc_crc = pvd_checksum;
		pvd_tag.tag_checksum = 0;
		auto tag_cksum = cksum_tag((unsigned char*)&pvd_tag, sizeof(DescriptorTag));
		pvd_tag.tag_checksum = tag_cksum;
		sm.write_sector<PrimaryVolumeDescriptor_UDF>(f, &pvd, sizeof(PrimaryVolumeDescriptor_UDF));
#pragma endregion
		cur_tag_ident = 4; // Other identifiers just start couting up from here

		// Write implementation use volume descriptor
#pragma region ImplUseVolumeDescriptor writing
		ImplUseVolumeDescriptor iuv;
		DescriptorTag& iuv_tag = iuv.tag;
		iuv_tag.tag_ident = cur_tag_ident++;
		iuv_tag.desc_crc_len = 2032;
		iuv_tag.desc_version = cur_tag_desc_ver;
		iuv_tag.tag_location = sm.get_current_sector();
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		iuv.vol_desc_seq_num = cur_vol_desc_seq_num++;
		iuv.impl_ident.flags = 0;
		strncpy(iuv.impl_ident.ident, udf_lv_info, strlen(udf_lv_info));
		pad_string(iuv.impl_ident.ident, strlen(udf_lv_info), 23, '\0');
		pad_string(iuv.impl_ident.ident_suffix, 0, 8, '\0');
		iuv.impl_use.lvi_charset.char_set_type = 0;
		strncpy(iuv.impl_use.lvi_charset.char_set_info, osta, strlen(osta));
		pad_string(iuv.impl_use.lvi_charset.char_set_info, strlen(osta), 63, '\0');
		strncpy(iuv.impl_use.log_vol_ident, &unkNum, 1);
		strncpy(iuv.impl_use.log_vol_ident + 1, vol_ident, strlen(vol_ident));
		pad_string(iuv.impl_use.log_vol_ident, strlen(vol_ident) + 1, 128, '\0');
		// Honestly what's wrong with these standard designers?
		strncpy(iuv.impl_use.log_vol_ident + 127, &vol_ident_len, 1);
		strncpy(iuv.impl_use.lvinfo1, &unkNum, 1);
		strncpy(iuv.impl_use.lvinfo2, &unkNum, 1);
		strncpy(iuv.impl_use.lvinfo3, &unkNum, 1);
		pad_string(iuv.impl_use.lvinfo1, 1, 36, '\0');
		pad_string(iuv.impl_use.lvinfo2, 1, 36, '\0');
		pad_string(iuv.impl_use.lvinfo3, 1, 36, '\0');
		char c = 1;
		strncpy(iuv.impl_use.lvinfo1, &c, 1);
		strncpy(iuv.impl_use.lvinfo2, &c, 1);
		strncpy(iuv.impl_use.lvinfo3, &c, 1);
		iuv.impl_use.impl_id.flags = 0;
		strncpy(iuv.impl_use.impl_id.ident, dvd_gen, strlen(dvd_gen));
		pad_string(iuv.impl_use.impl_id.ident, strlen(dvd_gen), 23, '\0');
		pad_string(iuv.impl_use.impl_id.ident_suffix, 0, 8, '\0');
		pad_string((char*)iuv.impl_use.impl_use, 0, 128, '\0');
		auto iuv_checksum = cksum(((unsigned char*)&iuv) + sizeof(DescriptorTag), sizeof(ImplUseVolumeDescriptor) - sizeof(DescriptorTag));
		iuv_tag.desc_crc = iuv_checksum;
		iuv_tag.tag_checksum = 0;
		tag_cksum = cksum_tag((unsigned char*)&iuv_tag, sizeof(DescriptorTag));
		iuv_tag.tag_checksum = tag_cksum;
		sm.write_sector<ImplUseVolumeDescriptor>(f, &iuv, sizeof(ImplUseVolumeDescriptor));
#pragma endregion

		// Write partition descriptor
#pragma region PartitionDescriptor writing
		PartitionDescriptor pd;
		DescriptorTag& pd_tag = pd.tag;
		pd_tag.tag_ident = cur_tag_ident++;
		pd_tag.desc_version = cur_tag_desc_ver;
		pd_tag.desc_crc_len = 2032;
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
		auto pd_checksum = cksum(((unsigned char*)&pd) + sizeof(DescriptorTag), sizeof(PartitionDescriptor) - sizeof(DescriptorTag));
		pd_tag.desc_crc = pd_checksum;
		pd_tag.tag_checksum = 0;
		tag_cksum = cksum_tag((unsigned char*)&pd_tag, sizeof(DescriptorTag));
		pd_tag.tag_checksum = tag_cksum;
		sm.write_sector<PartitionDescriptor>(f, &pd, sizeof(PartitionDescriptor));
#pragma endregion

		// Write logical volume descriptor
#pragma region LogicalVolumeDescriptor writing
		LogicalVolumeDescriptor lv;
		DescriptorTag& lv_tag = lv.tag;
		lv_tag.tag_ident = cur_tag_ident++;
		lv_tag.desc_version = cur_tag_desc_ver;
		lv_tag.desc_crc_len = 2032;
		lv_tag.tag_location = sm.get_current_sector();
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		lv.vol_desc_seq_num = cur_vol_desc_seq_num++;
		lv.desc_char_set.char_set_type = 0;
		strncpy(lv.desc_char_set.char_set_info, osta, strlen(osta));
		pad_string(lv.desc_char_set.char_set_info, strlen(osta), 63, '\0');
		strncpy(lv.log_vol_ident, &unkNum, 1);
		strncpy(lv.log_vol_ident + 1, vol_ident, strlen(vol_ident));
		pad_string(lv.log_vol_ident, strlen(vol_ident) + 1, 128, '\0');
		strncpy(lv.log_vol_ident + 127, &vol_ident_len, 1);
		lv.log_block_size = 2048;
		lv.domain_ident.flags = 0;
		strncpy(lv.domain_ident.ident, osta_complient, strlen(osta_complient));
		pad_string(lv.domain_ident.ident, strlen(osta_complient), 23, '\0');
		pad_string(lv.domain_ident.ident_suffix, 0, 8, '\0');
		// This unknown number is set in the log_vol_cont_use[1], according to the UDF standard this is not the way this field should be used :P
		char unkNum2 = 0x10;
		lv.log_vol_cont_use[0] = '\0';
		strncpy(lv.log_vol_cont_use + 1, &unkNum2, 1);
		pad_string(lv.log_vol_cont_use, 2, 16, '\0');
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
		auto lv_checksum = cksum(((unsigned char*)&lv) + sizeof(DescriptorTag), sizeof(LogicalVolumeDescriptor) - sizeof(DescriptorTag));
		lv_tag.desc_crc = lv_checksum;
		lv_tag.tag_checksum = 0;
		tag_cksum = cksum_tag((unsigned char*)&lv_tag, sizeof(DescriptorTag));
		lv_tag.tag_checksum = tag_cksum;
		sm.write_sector<LogicalVolumeDescriptor>(f, &lv, sizeof(LogicalVolumeDescriptor));
#pragma endregion

		// Write unallocated space descriptor
#pragma region UnallocatedSpaceDescriptor
		UnallocatedSpaceDescriptor usd;
		DescriptorTag& usd_tag = usd.tag;
		usd_tag.tag_ident = cur_tag_ident++;
		usd_tag.desc_version = cur_tag_desc_ver;
		usd_tag.desc_crc_len = 2032;
		usd_tag.tag_location = sm.get_current_sector();
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		usd.vol_desc_seq_num = cur_vol_desc_seq_num++;
		usd.alloc_desc.length = 0;
		usd.alloc_desc.location = 0;
		usd.num_of_alloc_desc = 0;
		auto usd_checksum = cksum(((unsigned char*)&usd) + sizeof(DescriptorTag), sizeof(UnallocatedSpaceDescriptor) - sizeof(DescriptorTag));
		usd_tag.desc_crc = usd_checksum;
		usd_tag.tag_checksum = 0;
		tag_cksum = cksum_tag((unsigned char*)&usd_tag, sizeof(DescriptorTag));
		usd_tag.tag_checksum = tag_cksum;
		sm.write_sector<UnallocatedSpaceDescriptor>(f, &usd, sizeof(UnallocatedSpaceDescriptor));
#pragma endregion

		// Write terminating descriptor
		TerminatingDescriptor td;
		DescriptorTag& td_tag = td.tag;
		td_tag.tag_ident = cur_tag_ident++;
		td_tag.desc_version = cur_tag_desc_ver;
		td_tag.desc_crc_len = 2032;
		td_tag.tag_location = sm.get_current_sector();
		auto td_checksum = cksum(((unsigned char*)&td) + sizeof(DescriptorTag), sizeof(TerminatingDescriptor) - sizeof(DescriptorTag));
		td_tag.desc_crc = td_checksum;
		td_tag.tag_checksum = 0;
		tag_cksum = cksum_tag((unsigned char*)&td_tag, sizeof(DescriptorTag));
		td_tag.tag_checksum = tag_cksum;
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
	lvi_tag.desc_crc_len = 2032;
	lvi_tag.tag_location = sm.get_current_sector();
	// Tag checksum and its desc crc fields are written after descriptor has been filled
	// Just record date and time whenever Twinsanity PAL was released
	lvi.rec_date_and_time.microseconds = 0;
	lvi.rec_date_and_time.milliseconds = 0;
	lvi.rec_date_and_time.centiseconds = 0;
	lvi.rec_date_and_time.second = 47;
	lvi.rec_date_and_time.minute = 33;
	lvi.rec_date_and_time.hour = 7;
	lvi.rec_date_and_time.day = 3;
	lvi.rec_date_and_time.month = 9;
	lvi.rec_date_and_time.year = 2004;
	lvi.rec_date_and_time.type_and_timezone = 0x121C;
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
	auto lvi_checksum = cksum(((unsigned char*)&lvi) + sizeof(DescriptorTag), sizeof(LogicalVolumeIntegrityDescriptor) - sizeof(DescriptorTag));
	lvi_tag.desc_crc = lvi_checksum;
	lvi_tag.tag_checksum = 0;
	auto tag_cksum = cksum_tag((unsigned char*)&lvi_tag, sizeof(DescriptorTag));
	lvi_tag.tag_checksum = tag_cksum;
	sm.write_sector<LogicalVolumeIntegrityDescriptor>(f, &lvi, sizeof(LogicalVolumeIntegrityDescriptor));
#pragma endregion


	// Write terminating descriptor
	TerminatingDescriptor td;
	DescriptorTag& td_tag = td.tag;
	td_tag.tag_ident = 8;
	td_tag.desc_version = 2;
	td_tag.desc_crc_len = 2032;
	td_tag.tag_location = sm.get_current_sector();
	auto td_checksum = cksum(((unsigned char*)&td) + sizeof(DescriptorTag), sizeof(TerminatingDescriptor) - sizeof(DescriptorTag));
	td_tag.desc_crc = td_checksum;
	td_tag.tag_checksum = 0;
	tag_cksum = cksum_tag((unsigned char*)&td_tag, sizeof(DescriptorTag));
	td_tag.tag_checksum = tag_cksum;
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
	avd_tag.desc_crc_len = 2032;
	avd_tag.tag_location = sm.get_current_sector();
	// Tag checksum and its desc crc fields are written after descriptor has been filled
	avd.main_vol_desc_seq_extent.length = 0x8000;
	avd.main_vol_desc_seq_extent.location = 32;
	avd.reserve_vol_desc_seq_extent.length = 0x8000;
	avd.reserve_vol_desc_seq_extent.location = 48;
	pad_string((char*)avd.reserved, 0, 480, '\0');
	auto avd_checksum = cksum(((unsigned char*)&avd) + sizeof(DescriptorTag), sizeof(AnchorVolumeDescriptorPointer) - sizeof(DescriptorTag));
	avd_tag.desc_crc = avd_checksum;
	avd_tag.tag_checksum = 0;
	tag_cksum = cksum_tag((unsigned char*)&avd_tag, sizeof(DescriptorTag));
	avd_tag.tag_checksum = tag_cksum;
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
		auto dir_len = 0x30 * 2U;
		// Recalculate the data len of the current directory tree for nav dirs as well as set their new LBA
		if (cur_dir != nullptr) {
			for (auto node : cur_tree->tree) {
				dir_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32);
			}
			nav_this.data_len_lsb = dir_len;
			nav_this.data_len_msb = changeEndianness32(dir_len);
			nav_this.loc_of_ext_lsb = sm.get_file_sector(cur_dir);
			nav_this.loc_of_ext_msb = changeEndianness32(sm.get_file_sector(cur_dir));
			if (cur_dir->parent != nullptr) {
				auto par_dir_len = 0x30 * 2U;
				for (auto node : cur_dir->parent->next->tree) {
					par_dir_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32);
				}
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
		for (auto node : cur_tree->tree) {
			// Need to process directories in root first, kick files in a buffer for now
			if (node->file->IsDirectory()) {
				auto file = node->file;
				DirectoryRecord& rec = dir_rec[index++];
				rec.dir_rec_len = 48 + file->GetName().size() + file->GetName().size() % 2;
				needed_memory += rec.dir_rec_len;
				rec.loc_of_ext_lsb = sm.get_file_sector(node);
				rec.loc_of_ext_msb = changeEndianness32(sm.get_file_sector(node));
				auto rec_len = 0x30 * 2U;
				for (auto node : node->next->tree) {
					rec_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32);
				}
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
			else {
				file_buf.push_back(node);
			}
		}
		// Fill in the files
		for (auto node : file_buf) {
			auto file = node->file;
			DirectoryRecord& rec = dir_rec[index++];
			rec.dir_rec_len = 50 + file->GetName().size() + file->GetName().size() % 2;
			needed_memory += rec.dir_rec_len;
			rec.data_len_lsb = file->GetSize();
			rec.data_len_msb = changeEndianness32(file->GetSize());
			rec.loc_of_ext_lsb = sm.get_file_sector(node);
			rec.loc_of_ext_msb = changeEndianness32(sm.get_file_sector(node));
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
			memset(buffer + offset, 0, rec.dir_rec_len - sizeof(DirectoryRecord) - rec.file_ident_len);
			offset += rec.dir_rec_len - sizeof(DirectoryRecord) - rec.file_ident_len;
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

	// Write file set descriptor

	// Write terminating descriptor

	// Write file identifier descriptor

	// Write file entries for directories

	// Write file entries for files


	update_progress_message(pr, "Finished writing sectors");
	write_file_tree(pr, sm, f, ft);
}

void write_file_tree(Progress* pr, SectorManager& sm, std::ofstream& f, FileTree* ft) {
	for (auto node : ft->tree) {
		if (node->file->IsDirectory()) {
			write_file_tree(pr, sm, f, node->next);
		}
		else {
			std::ifstream in_f;
			in_f.open(node->file->GetPath(), std::ios_base::in | std::ios_base::binary);
			sm.write_file(f, in_f.rdbuf(), node->file->GetSize());
			update_progress_message(pr, "Writing file...");
		}
	}
}

void update_progress_message(Progress* pr, const char* message) {
	auto msg = message;
	pr->message.size = strlen(msg);
	pr->message.str[pr->message.size] = '\0';
	strncpy(pr->message.str, msg, pr->message.size);
	pr->message.size += 1;
	pr->new_message = true;
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