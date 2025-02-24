//Copyright (C) 2011  Carl Rogers
//Released under MIT License
//license available in LICENSE file, or at http://www.opensource.org/licenses/mit-license.php

#include "cnpy.h"
#include <complex>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <stdint.h>
#include <stdexcept>
#include <regex>
#include <cassert>

namespace {
	const size_t LOCAL_HEADER_SIZE = 30;
	const size_t ZIP_FOOTER_SIZE = 22;
	const size_t BUFFER_SIZE = 256;
}

char cnpy::BigEndianTest() {
	int x = 1;
	return (((char*)&x)[0]) ? '<' : '>';
}

char cnpy::map_type(const std::type_info& t) {
	if (t == typeid(float) || t == typeid(double) || t == typeid(long double)) return 'f';
	if (t == typeid(int) || t == typeid(char) || t == typeid(short) || t == typeid(long) || t == typeid(long long)) return 'i';
	if (t == typeid(unsigned char) || t == typeid(unsigned short) || t == typeid(unsigned long) || t == typeid(unsigned long long) || t == typeid(unsigned int)) return 'u';
	if (t == typeid(bool)) return 'b';
	if (t == typeid(std::complex<float>) || t == typeid(std::complex<double>) || t == typeid(std::complex<long double>)) return 'c';
	return '?';
}

template<> std::vector<char>& cnpy::operator+=(std::vector<char>& lhs, const std::string rhs) {
	lhs.insert(lhs.end(), rhs.begin(), rhs.end());
	return lhs;
}

template<> std::vector<char>& cnpy::operator+=(std::vector<char>& lhs, const char* rhs) {
	size_t len = strlen(rhs);
	lhs.reserve(lhs.size() + len);
	for (size_t byte = 0; byte < len; byte++) {
		lhs.push_back(rhs[byte]);
	}
	return lhs;
}

void cnpy::parse_npy_header(unsigned char* buffer, size_t& word_size, std::vector<size_t>& shape, bool& fortran_order) {
	uint8_t major_version = buffer[6];
	uint8_t minor_version = buffer[7];
	uint16_t header_len = *reinterpret_cast<uint16_t*>(buffer + 8);
	std::string header(reinterpret_cast<char*>(buffer + 9), header_len);

	size_t loc1 = header.find("fortran_order") + 16;
	fortran_order = (header.substr(loc1, 4) == "True");

	loc1 = header.find("(");
	size_t loc2 = header.find(")");

	std::regex num_regex("[0-9][0-9]*");
	std::smatch sm;
	shape.clear();

	std::string str_shape = header.substr(loc1 + 1, loc2 - loc1 - 1);
	while (std::regex_search(str_shape, sm, num_regex)) {
		shape.push_back(std::stoul(sm[0].str()));
		str_shape = sm.suffix().str();
	}

	loc1 = header.find("descr") + 9;
	bool littleEndian = (header[loc1] == '<' || header[loc1] == '|');
	assert(littleEndian);

	std::string str_ws = header.substr(loc1 + 2);
	loc2 = str_ws.find("'");
	word_size = std::stoul(str_ws.substr(0, loc2));
}

void cnpy::parse_npy_header(FILE* fp, size_t& word_size, std::vector<size_t>& shape, bool& fortran_order) {
	char buffer[BUFFER_SIZE];
	size_t res = fread(buffer, sizeof(char), 11, fp);
	if (res != 11) throw std::runtime_error("parse_npy_header: failed fread");

	std::string header = fgets(buffer, BUFFER_SIZE, fp);
	if (header.empty() || header.back() != '\n') throw std::runtime_error("parse_npy_header: invalid header");

	size_t loc1 = header.find("fortran_order");
	if (loc1 == std::string::npos) throw std::runtime_error("parse_npy_header: failed to find header keyword: 'fortran_order'");
	loc1 += 16;
	fortran_order = (header.substr(loc1, 4) == "True");

	loc1 = header.find("(");
	size_t loc2 = header.find(")");
	if (loc1 == std::string::npos || loc2 == std::string::npos) throw std::runtime_error("parse_npy_header: failed to find header keyword: '(' or ')'");

	std::regex num_regex("[0-9][0-9]*");
	std::smatch sm;
	shape.clear();

	std::string str_shape = header.substr(loc1 + 1, loc2 - loc1 - 1);
	while (std::regex_search(str_shape, sm, num_regex)) {
		shape.push_back(std::stoul(sm[0].str()));
		str_shape = sm.suffix().str();
	}

	loc1 = header.find("descr");
	if (loc1 == std::string::npos) throw std::runtime_error("parse_npy_header: failed to find header keyword: 'descr'");
	loc1 += 9;
	bool littleEndian = (header[loc1] == '<' || header[loc1] == '|');
	assert(littleEndian);

	std::string str_ws = header.substr(loc1 + 2);
	loc2 = str_ws.find("'");
	word_size = std::stoul(str_ws.substr(0, loc2));
}

void cnpy::parse_zip_footer(FILE* fp, uint16_t& nrecs, size_t& global_header_size, size_t& global_header_offset) {
	std::vector<char> footer(ZIP_FOOTER_SIZE);
	fseek(fp, -ZIP_FOOTER_SIZE, SEEK_END);
	size_t res = fread(footer.data(), sizeof(char), ZIP_FOOTER_SIZE, fp);
	if (res != ZIP_FOOTER_SIZE) throw std::runtime_error("parse_zip_footer: failed fread");

	uint16_t disk_no = *reinterpret_cast<uint16_t*>(&footer[4]);
	uint16_t disk_start = *reinterpret_cast<uint16_t*>(&footer[6]);
	uint16_t nrecs_on_disk = *reinterpret_cast<uint16_t*>(&footer[8]);
	nrecs = *reinterpret_cast<uint16_t*>(&footer[10]);
	global_header_size = *reinterpret_cast<uint32_t*>(&footer[12]);
	global_header_offset = *reinterpret_cast<uint32_t*>(&footer[16]);
	uint16_t comment_len = *reinterpret_cast<uint16_t*>(&footer[20]);

	assert(disk_no == 0);
	assert(disk_start == 0);
	assert(nrecs_on_disk == nrecs);
	assert(comment_len == 0);
}

cnpy::NpyArray load_the_npy_file(FILE* fp) {
	std::vector<size_t> shape;
	size_t word_size;
	bool fortran_order;
	cnpy::parse_npy_header(fp, word_size, shape, fortran_order);

	cnpy::NpyArray arr(shape, word_size, fortran_order);
	size_t nread = fread(arr.data<char>(), 1, arr.num_bytes(), fp);
	if (nread != arr.num_bytes()) throw std::runtime_error("load_the_npy_file: failed fread");
	return arr;
}

cnpy::NpyArray load_the_npz_array(FILE* fp, uint32_t compr_bytes, uint32_t uncompr_bytes) {
	std::vector<unsigned char> buffer_compr(compr_bytes);
	std::vector<unsigned char> buffer_uncompr(uncompr_bytes);
	size_t nread = fread(buffer_compr.data(), 1, compr_bytes, fp);
	if (nread != compr_bytes) throw std::runtime_error("load_the_npz_array: failed fread");

	z_stream d_stream;
	d_stream.zalloc = Z_NULL;
	d_stream.zfree = Z_NULL;
	d_stream.opaque = Z_NULL;
	d_stream.avail_in = 0;
	d_stream.next_in = Z_NULL;
	int err = inflateInit2(&d_stream, -MAX_WBITS);
	if (err != Z_OK) throw std::runtime_error("load_the_npz_array: failed to initialize zlib");

	d_stream.avail_in = compr_bytes;
	d_stream.next_in = buffer_compr.data();
	d_stream.avail_out = uncompr_bytes;
	d_stream.next_out = buffer_uncompr.data();

	err = inflate(&d_stream, Z_FINISH);
	if (err != Z_STREAM_END) throw std::runtime_error("load_the_npz_array: failed to inflate data");
	inflateEnd(&d_stream);

	std::vector<size_t> shape;
	size_t word_size;
	bool fortran_order;
	cnpy::parse_npy_header(buffer_uncompr.data(), word_size, shape, fortran_order);

	cnpy::NpyArray array(shape, word_size, fortran_order);
	size_t offset = uncompr_bytes - array.num_bytes();
	memcpy(array.data<unsigned char>(), buffer_uncompr.data() + offset, array.num_bytes());

	return array;
}

cnpy::npz_t cnpy::npz_load(const std::string& fname) {
	FILE* fp = fopen(fname.c_str(), "rb");
	if (!fp) throw std::runtime_error("npz_load: Error! Unable to open file " + fname + "!");

	cnpy::npz_t arrays;
	while (true) {
		std::vector<char> local_header(LOCAL_HEADER_SIZE);
		size_t headerres = fread(local_header.data(), sizeof(char), LOCAL_HEADER_SIZE, fp);
		if (headerres != LOCAL_HEADER_SIZE) throw std::runtime_error("npz_load: failed fread");

		if (local_header[2] != 0x03 || local_header[3] != 0x04) break;

		uint16_t name_len = *reinterpret_cast<uint16_t*>(&local_header[26]);
		std::string varname(name_len, ' ');
		size_t vname_res = fread(&varname[0], sizeof(char), name_len, fp);
		if (vname_res != name_len) throw std::runtime_error("npz_load: failed fread");

		varname.erase(varname.end() - 4, varname.end());

		uint16_t extra_field_len = *reinterpret_cast<uint16_t*>(&local_header[28]);
		if (extra_field_len > 0) {
			std::vector<char> buff(extra_field_len);
			size_t efield_res = fread(buff.data(), sizeof(char), extra_field_len, fp);
			if (efield_res != extra_field_len) throw std::runtime_error("npz_load: failed fread");
		}

		uint16_t compr_method = *reinterpret_cast<uint16_t*>(&local_header[8]);
		uint32_t compr_bytes = *reinterpret_cast<uint32_t*>(&local_header[18]);
		uint32_t uncompr_bytes = *reinterpret_cast<uint32_t*>(&local_header[22]);

		if (compr_method == 0) {
			arrays[varname] = load_the_npy_file(fp);
		}
		else {
			arrays[varname] = load_the_npz_array(fp, compr_bytes, uncompr_bytes);
		}
	}

	fclose(fp);
	return arrays;
}

cnpy::NpyArray cnpy::npz_load(const std::string& fname, const std::string& varname) {
	FILE* fp = fopen(fname.c_str(), "rb");
	if (!fp) throw std::runtime_error("npz_load: Unable to open file " + fname);

	while (true) {
		std::vector<char> local_header(LOCAL_HEADER_SIZE);
		size_t header_res = fread(local_header.data(), sizeof(char), LOCAL_HEADER_SIZE, fp);
		if (header_res != LOCAL_HEADER_SIZE) throw std::runtime_error("npz_load: failed fread");

		if (local_header[2] != 0x03 || local_header[3] != 0x04) break;

		uint16_t name_len = *reinterpret_cast<uint16_t*>(&local_header[26]);
		std::string vname(name_len, ' ');
		size_t vname_res = fread(&vname[0], sizeof(char), name_len, fp);
		if (vname_res != name_len) throw std::runtime_error("npz_load: failed fread");
		vname.erase(vname.end() - 4, vname.end());

		uint16_t extra_field_len = *reinterpret_cast<uint16_t*>(&local_header[28]);
		fseek(fp, extra_field_len, SEEK_CUR);

		uint16_t compr_method = *reinterpret_cast<uint16_t*>(&local_header[8]);
		uint32_t compr_bytes = *reinterpret_cast<uint32_t*>(&local_header[18]);
		uint32_t uncompr_bytes = *reinterpret_cast<uint32_t*>(&local_header[22]);

		if (vname == varname) {
			NpyArray array = (compr_method == 0) ? load_the_npy_file(fp) : load_the_npz_array(fp, compr_bytes, uncompr_bytes);
			fclose(fp);
			return array;
		}
		else {
			uint32_t size = *reinterpret_cast<uint32_t*>(&local_header[22]);
			fseek(fp, size, SEEK_CUR);
		}
	}

	fclose(fp);
	throw std::runtime_error("npz_load: Variable name " + varname + " not found in " + fname);
}

cnpy::NpyArray cnpy::npy_load(const std::string& fname) {
	FILE* fp = fopen(fname.c_str(), "rb");
	if (!fp) throw std::runtime_error("npy_load: Unable to open file " + fname);

	NpyArray arr = load_the_npy_file(fp);
	fclose(fp);
	return arr;
}