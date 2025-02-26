/*
 *   Packfiles and compression
 *
 */

#include "LZ_UTY.H"
#include <io.h>
#include <numeric>

constexpr auto LZSS_DICT_BITS = 13;
constexpr auto LZSS_SEQ_BITS = 4;
constexpr auto LZSS_SEQ_MIN = 3;
constexpr auto LZSS_DICT_MASK = ((1 << LZSS_DICT_BITS) - 1);
constexpr auto LZSS_SEQ_MAX = (LZSS_SEQ_MIN + ((1 << LZSS_SEQ_BITS) - 1));

std::optional<BYTE_BUFFER_OWNED> FilBufferCreateR(const char *s)
{
	auto fp = fopen(s, "rb");
	if(!fp) {
		return std::nullopt;
	}

	auto size64 = _filelengthi64(fileno(fp));
	if(size64 > std::numeric_limits<fil_size_t>::max()) {
		return std::nullopt;
	}
	const fil_size_t size = size64;

	auto buf = BYTE_BUFFER_OWNED{ size };
	if(!buf) {
		return std::nullopt;
	}

	if(fread(buf.get(), size, 1, fp) != 1) {
		return std::nullopt;
	}
	fclose(fp);

	return std::move(buf);
}

template <typename Container> fil_checksum_t FilChecksumAddFile(
	fil_checksum_t &current_total,
	fil_size_t offset,
	fil_size_t size_uncompressed,
	const Container &compressed
)
{
	auto ret = std::accumulate(
		compressed.begin(), compressed.end(), fil_checksum_t{ 0 }
	);
	current_total += ret;
	current_total += size_uncompressed;
	current_total += offset;
	return ret;
}

std::optional<BYTE_BUFFER_BORROWED> FilFileGetCompressed(
	const BYTE_BUFFER_OWNED& packfile,
	const std::span<const PBG_FILEINFO> info,
	fil_no_t filno
)
{
	if(filno >= info.size()) {
		return std::nullopt;
	}
	const auto start = info[filno].offset;
	const auto end = ((filno == (info.size() - 1))
		? packfile.size()
		: info[filno + 1].offset
	);
	if((start >= packfile.size()) || (end > packfile.size())) {
		return std::nullopt;
	}
	return BYTE_BUFFER_BORROWED{ (packfile.get() + start), (end - start) };
}

uint8_t BIT_DEVICE_READ::GetBit()
{
	if(cursor.byte >= buffer.size()) {
		return 0xFF;
	}
	bool ret = ((buffer[cursor.byte] >> (7 - cursor.bit)) & 1);
	cursor += 1;
	return ret;
}

uint32_t BIT_DEVICE_READ::GetBits(unsigned int bitcount)
{
	const auto bytes_remaining = (buffer.size() - cursor.byte);
	if((bitcount > 24) || (bytes_remaining == 0)) {
		return 0xFFFFFFFF;
	}

	if(((bitcount + 7) / 8) >= bytes_remaining) {
		bitcount = std::min(((bytes_remaining * 8) - cursor.bit), bitcount);
	}
	const auto window_size = (cursor.bit + bitcount);

	uint32_t window = (buffer[cursor.byte + 0] << 24);
	if((bitcount > 1) && (window_size > 8)) {
		window |= (buffer[cursor.byte + 1] << 16);
	}
	if((bitcount > 9) && (window_size > 16)) {
		window |= (buffer[cursor.byte + 2] <<  8);
	}
	if((bitcount > 17) && (window_size > 24)) {
		window |= (buffer[cursor.byte + 3] <<  0);
	}
	window <<= cursor.bit;
	cursor += bitcount;
	return (window >> (32 - bitcount));
}

void BIT_DEVICE_WRITE::PutBit(uint8_t bit)
{
	auto& byte = ((bit_cursor == 0)
		? buffer.emplace_back(0x00)
		: buffer.back()
	);
	byte |= ((bit & 1) << (7 - bit_cursor));
	bit_cursor++;
}

void BIT_DEVICE_WRITE::PutBits(uint32_t bits,unsigned int bitcount)
{
	uint32_t mask = (1 << (bitcount - 1));
	for(decltype(bitcount) i = 0; i < bitcount; i++) {
		PutBit((bits & mask) != 0);
		mask >>= 1;
	}
}

bool BIT_DEVICE_WRITE::Write(const char *s) const
{
	auto fp = fopen(s, "wb");
	if(!fp) {
		return false;
	}
	if(fwrite(buffer.data(), buffer.size(), 1, fp) != 1) {
		return false;
	}
	fclose(fp);
	return true;
}

BYTE_BUFFER_OWNED PACKFILE_READ::MemExpand(fil_no_t filno) const
{
	const auto maybe_compressed = FilFileGetCompressed(packfile, info, filno);
	if(!maybe_compressed) {
		return nullptr;
	}

	BYTE_BUFFER_OWNED uncompressed = { info[filno].size_uncompressed };
	if(!uncompressed) {
		return nullptr;
	}

	// Textbook LZSS.
	std::array<uint8_t, (1 << LZSS_DICT_BITS)> dict;
	fil_size_t out_i = 0;

	auto output = [&uncompressed, &dict, &out_i](uint8_t literal) {
		uncompressed.get()[out_i] = literal;
		dict[out_i & LZSS_DICT_MASK] = literal;
		out_i++;
	};

	BIT_DEVICE_READ device = { maybe_compressed.value() };
	while(out_i < info[filno].size_uncompressed) {
		bool is_literal = device.GetBit();
		if(is_literal) {
			output(device.GetBits(8));
		} else {
			auto seq_offset = device.GetBits(LZSS_DICT_BITS);
			if(seq_offset == 0) {
				break;
			} else {
				seq_offset--;
			}
			auto seq_length = (device.GetBits(LZSS_SEQ_BITS) + LZSS_SEQ_MIN);
			for(decltype(seq_length) i = 0; i < seq_length; i++) {
				output(dict[seq_offset++ & LZSS_DICT_MASK]);
			}
		}
	}

	return std::move(uncompressed);
}

BYTE_BUFFER_GROWABLE Compress(BYTE_BUFFER_BORROWED buffer)
{
	constexpr auto DICT_WINDOW = ((1 << LZSS_DICT_BITS) - LZSS_SEQ_MAX);

	BIT_DEVICE_WRITE device;
	fil_size_t in_i = 0;

	while(in_i < buffer.size()) {
		unsigned int seq_offset = 0;
		unsigned int seq_length = 0;
		unsigned int dict_i = ((in_i > DICT_WINDOW) ? (in_i - DICT_WINDOW) : 1);
		while((dict_i < in_i) && (seq_length < LZSS_SEQ_MAX)) {
			unsigned int length_new = 0;
			while(length_new < LZSS_SEQ_MAX) {
				if((in_i + length_new) >= buffer.size()) {
					break;
				}
				if(buffer[dict_i + length_new] != buffer[in_i + length_new]) {
					break;
				}
				length_new++;
			}
			if((length_new > seq_length)) {
				// ([seq_offset] == LZSS_DICT_MASK) would cause the in-file
				// offset to overflow back to 0, which is interpreted as the
				// "sentinel offset" that causes the original game to stop
				// decompressing.
				// (That's why offsets are 1-based to begin with.)
				if((dict_i & LZSS_DICT_MASK) != LZSS_DICT_MASK) {
					seq_length = length_new;
					seq_offset = dict_i;
				}
			}
			dict_i++;
		}
		if(seq_length < LZSS_SEQ_MIN) {
			auto literal = buffer[in_i];
			device.PutBit(true);
			device.PutBits(literal, 8);
			in_i++;
		} else {
			device.PutBit(false);
			device.PutBits((seq_offset + 1), LZSS_DICT_BITS);
			device.PutBits((seq_length - LZSS_SEQ_MIN), LZSS_SEQ_BITS);
			in_i += seq_length;
		}
	}

	// Write the sentinel offset
	device.PutBit(false);
	device.PutBits(0, LZSS_DICT_BITS);

	return device.buffer;
}

bool PACKFILE_WRITE::Write(const char *s) const
{
	PBG_FILEHEAD head = { .n = files.size() };
	std::vector<PBG_FILEINFO> info(files.size());

	auto write_header = [&head, &info](FILE *fp) {
		return (
			(fwrite(&head, sizeof(head), 1, fp) != 1) ||
			(fwrite(info.data(), (info.size() * sizeof(info[0])), 1, fp) != 1)
		);
	};

	auto fp = fopen(s, "wb");
	if(!fp) {
		return false;
	}

	// Write temporary header
	if(write_header(fp)) {
		return false;
	}

	// Compress and write files
	for(fil_no_t i = 0; i < files.size(); i++) {
		auto compressed = Compress(files[i]);

		info[i].offset = ftell(fp);
		info[i].size_uncompressed = files[i].size();
		info[i].checksum_compressed = FilChecksumAddFile(
			head.sum, info[i].offset, info[i].size_uncompressed, compressed
		);
		if(fwrite(compressed.data(), compressed.size(), 1, fp) != 1) {
			return false;
		}
	}

	// Write real header
	rewind(fp);
	if(write_header(fp)) {
		return false;
	}
	fclose(fp);
	return true;
}

std::optional<BIT_FILE_READ> BitFilCreateR(const char *s)
{
	auto maybe_file = FilBufferCreateR(s);
	if(!maybe_file) {
		return std::nullopt;
	}
	return std::make_optional<BIT_FILE_READ>(std::move(maybe_file.value()));
}

std::optional<PACKFILE_READ> FilStartR(const char *s)
{
	auto maybe_packfile = FilBufferCreateR(s);
	if(!maybe_packfile) {
		return std::nullopt;
	}
	auto& packfile = maybe_packfile.value();

	// PBG_FILEHEAD
	size_t byte_start = 0;
	size_t byte_end = sizeof(PBG_FILEHEAD);
	if(packfile.size() < byte_end) {
		return std::nullopt;
	}
	const auto& head = *reinterpret_cast<PBG_FILEHEAD *>(
		(packfile.get() + byte_start)
	);
	if(head.name != PBG_HEADNAME) {
		return std::nullopt;
	}

	// PBG_FILEINFO
	byte_start = byte_end;
	byte_end = (byte_start + (head.n * sizeof(PBG_FILEINFO)));
	if(packfile.size() < byte_end) {
		return std::nullopt;
	}

	const auto* info_start = (packfile.get() + byte_start);
	decltype(PACKFILE_READ::info) info = {
		reinterpret_cast<const PBG_FILEINFO *>(info_start), head.n
	};
	const BYTE_BUFFER_BORROWED info_bytes = { info_start, info.size_bytes() };

	// Checksums
	fil_checksum_t total_checksum = 0;
	for(fil_no_t i = 0; i < info.size(); i++) {
		const auto maybe_compressed = FilFileGetCompressed(packfile, info, i);
		if(!maybe_compressed) {
			return std::nullopt;
		}
		const auto checksum = FilChecksumAddFile(
			total_checksum,
			info[i].offset,
			info[i].size_uncompressed,
			maybe_compressed.value()
		);
		if(checksum != info[i].checksum_compressed) {
			return std::nullopt;
		}
	}
	if(total_checksum != head.sum) {
		return std::nullopt;
	}

	return std::make_optional<PACKFILE_READ>(std::move(packfile), info);
}
