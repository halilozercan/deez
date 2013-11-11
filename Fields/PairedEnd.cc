#include "PairedEnd.h"
using namespace std;

PairedEndCompressor::PairedEndCompressor (int blockSize):
	GenericCompressor<PairedEndInfo, GzipCompressionStream<6> >(blockSize)
{
}

PairedEndCompressor::~PairedEndCompressor (void) {
}

void PairedEndCompressor::outputRecords (Array<uint8_t> &out, size_t out_offset, size_t k) {
	if (!records.size()) { 
		out.resize(0);
		return;
	}
	assert(k <= records.size());

	Array<uint8_t> buffer(k * (sizeof(size_t) + sizeof(int32_t) + 1), 1000);
	std::map<std::string, char> chromosomes;
	for (size_t i = 0; i < k; i++) {
		buffer.add((uint8_t*)&records[i].pos, sizeof(size_t));
		buffer.add((uint8_t*)&records[i].tlen, sizeof(int32_t));
		map<string, char>::iterator c = chromosomes.find(records[i].chr);
		if (c != chromosomes.end())
			buffer.add(c->second);
		else {
			buffer.add(-1);
			buffer.add((uint8_t*)this->records[i].chr.c_str(), this->records[i].chr.size() + 1);
			
			char id = chromosomes.size();
			chromosomes[records[i].chr] = id;
		}
	}
	size_t s = 0;
	if (buffer.size()) s = stream->compress(buffer.data(), buffer.size(), out, out_offset + sizeof(size_t));
	out.resize(out_offset + s + sizeof(size_t));
	*(size_t*)(out.data() + out_offset) = buffer.size();
	////
	this->records.remove_first_n(k);
}

PairedEndDecompressor::PairedEndDecompressor (int blockSize):
	GenericDecompressor<PairedEndInfo, GzipDecompressionStream>(blockSize)
{
}

PairedEndDecompressor::~PairedEndDecompressor (void) {
}

void PairedEndDecompressor::importRecords (uint8_t *in, size_t in_size) {
	if (in_size == 0) 
		return;

	// here, we shouldn't have any leftovers, since all blocks are of the same size
	assert(recordCount == records.size());

	// decompress
	assert(in_size >= sizeof(size_t));
	
	size_t uncompressed_size = *(size_t*)in;
	Array<uint8_t> au;
	au.resize(uncompressed_size);
	in += sizeof(size_t);

	size_t s = 0;
	if (in_size) s = stream->decompress(in, in_size, au, 0);
	assert(s == uncompressed_size);

	PairedEndInfo pe;
	
	records.resize(0);
	std::map<char, std::string> chromosomes;
	char chr;
	for (size_t i = 0; i < s; ) {
		pe.pos = *(size_t*)(au.data() + i), i += sizeof(size_t);
		pe.tlen = *(int32_t*)(au.data() + i), i += sizeof(int32_t);
		chr = *(au.data() + i), i++;
		if (chr == -1) {
			string sx = "";
			while (au.data()[i])
				sx += au.data()[i++];
			i++;
			chr = chromosomes.size();
		//	DEBUG("%d->%s",chr,sx.c_str());
			pe.chr = chromosomes[chr] = sx;
		}
		else pe.chr = chromosomes[chr];
		records.add(pe);
	}
	
	recordCount = 0;
}

