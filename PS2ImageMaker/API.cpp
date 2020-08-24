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

void pack(Progress* pr, const char* game_path, const char* dest_path);
void write_sectors(Progress* pr, std::ofstream& f, FileTree* ft);
void write_file_tree(Progress* pr, SectorManager& sm, std::ofstream& f, FileTree* ft);
unsigned int get_path_table_size(FileTree* ft);
void pad_string(char* str, int offset, int size, const char pad = ' ');

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
// biggest reason is because all sectors need a very strict ordering
void write_sectors(Progress* pr, std::ofstream& f, FileTree* ft) {
	SectorManager sm(ft);
	const char pad = ' '; // For padding with spaces
	auto sys_ident = "PLAYSTATION";
	auto vol_ident = "CRASH";
	auto publisher_ident = "VUG";
	auto data_prep = "P.GENDREAU";
	auto app_ident = "PLAYSTATION";
	auto cop_ident = "VUG";
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

	// Write twice the same descriptors for Main and RSRV
	for (int i = 0; i < 2; ++i) {
		// Predetermined strings by UDF and PS2 standard
		auto osta = "OSTA Compressed Unicode";
		auto dvd_gen = "DVD-ROM GENERATOR";
		auto udf_lv_info = "*UDF LV Info";
		ushort cur_tag_ident = 1;
		ushort cur_tag_desc_ver = 2;
		uint cur_vol_desc_seq_num = 0;
		// Write primary volume descriptor UDF
		PrimaryVolumeDescriptor_UDF pvd;
		DescriptorTag& tag = pvd.tag; // Reference to currently written to descriptor tag
#pragma region PrimaryVolumeDescriptor_UDF writing
		tag.tag_ident = cur_tag_ident;
		tag.desc_version = cur_tag_desc_ver;
		// Tag checksum and its desc crc fields are written after descriptor has been filled
		tag.desc_crc_len = 2032;
		tag.tag_location = sm.get_current_sector();
		pvd.vol_desc_seq_num = cur_vol_desc_seq_num++;
		pvd.prim_vol_desc_num = 0;
		char unkNum = 0x8; // This is put as the first char in vol_ident of every PS2 game, no clue why
		strncpy(pvd.vol_ident, &unkNum, 1);
		strncpy(pvd.vol_ident + 1, vol_ident, strlen(vol_ident));
		pad_string(pvd.vol_ident, strlen(vol_ident) + 1, 32, '\0');
		char strLen = strlen(vol_ident) + 1; // Size of the identifier is also written at the end, no clue why nothing is mentioned about this in UDF standard
		strncpy(pvd.vol_ident + 31, &strLen, 1);
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
		auto pvd_checksum = cksum((unsigned char*)&pvd, sizeof(PrimaryVolumeDescriptor_UDF));
		tag.desc_crc = pvd_checksum;
		tag.tag_checksum = 0;
		auto tag_cksum = cksum((unsigned char*)&tag, sizeof(DescriptorTag));
		tag.tag_checksum = tag_cksum;
		sm.write_sector<PrimaryVolumeDescriptor_UDF>(f, &pvd, sizeof(PrimaryVolumeDescriptor_UDF));
#pragma endregion
		cur_tag_ident = 4; // Other identifiers start couting from here

		// Write implementation use volume descriptor
#pragma region ImplUseVolumeDescriptor writing
		ImplUseVolumeDescriptor iuv;
		DescriptorTag& iuv_tag = iuv.tag;
		iuv_tag.tag_ident = cur_tag_ident++;
		iuv_tag.desc_crc_len = 2032;
		iuv_tag.desc_version = 2;
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
		char str_len = strlen(vol_ident) + 1;
		strncpy(iuv.impl_use.log_vol_ident + 127, &str_len, 1);
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
		auto iuv_checksum = cksum((unsigned char*)&iuv, sizeof(ImplUseVolumeDescriptor));
		iuv_tag.desc_crc = iuv_checksum;
		iuv_tag.tag_checksum = 0;
		tag_cksum = cksum((unsigned char*)&iuv_tag, sizeof(DescriptorTag));
		iuv_tag.tag_checksum = tag_cksum;
		sm.write_sector<ImplUseVolumeDescriptor>(f, &iuv, sizeof(ImplUseVolumeDescriptor));
#pragma endregion

		// Write partition descriptor

		// Write logical volume descriptor

		// Write unallocated space descriptor

		// Write terminating descriptor

		// Write trailing logical sectors
		for (int j = 0; j < 10; ++j) {
			auto pad = '\0';
			sm.write_sector<char>(f, &pad, 1);
		}
	}

	// Write logical volume integrity descriptor

	// Write terminating descriptor

	// Skip reserved sectors until sector 256
	for (int i = 0; i < 190; ++i) {
		auto pad = '\0';
		sm.write_sector<char>(f, &pad, 1);
	}

	// Write anchor volume descriptor pointer

	// Write path table L and option L

	// Write path table M and option M

	// Write directory records

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