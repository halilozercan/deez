#include "OptionalField.h"
using namespace std;

OptionalFieldDecompressor::OptionalFieldDecompressor (int blockSize):
	GenericDecompressor<OptionalField, GzipDecompressionStream>(blockSize)
{
	indexStream = new GzipDecompressionStream();
}

OptionalFieldDecompressor::~OptionalFieldDecompressor (void) 
{
	delete indexStream;
	for (auto &m: fieldStreams)
		delete m.second;
}

OptionalField OptionalFieldDecompressor::parseFields(int size, uint8_t *&tags, uint8_t *&in, 
	vector<Array<uint8_t>>& oa, vector<size_t> &out)
{
	ZAMAN_START(Decompress_OptionalField_ParseFields);

	OptionalField of;
	string &result = of.data;
	while (size--) {
		string key;
		int keyIndex = getEncoded(tags) - 1;
		auto it = fields.find(keyIndex);
		if (it == fields.end()) {
			while (*tags) key += *tags++; tags++;
			fields[keyIndex] = key; 
			assert(oa.size() == keyIndex);
			assert(out.size() == keyIndex);
			if (fieldStreams.find(key) == fieldStreams.end())
				fieldStreams[key] = new GzipDecompressionStream();
			oa.push_back(Array<uint8_t>());
			decompressArray(fieldStreams[key], in, oa[keyIndex]);
			out.push_back(0);
		} else {
			key = it->second;
		}

		if (result != "") result += "\t";
		result += S("%c%c:%c:", key[0], key[1], key[2]);

		if (key == "MDZ" && !oa[keyIndex][out[keyIndex]]) {
			of.posMD = result.size();
			out[keyIndex]++;
		} else if (key == "XDZ" && !oa[keyIndex][out[keyIndex]]) {
			of.posXD = result.size();
			out[keyIndex]++;
		} else if (key == "NMi") {
			of.posNM = result.size();
		} else switch (key[2]) {
			case 'c': case 'C':
			case 's': case 'S':
			case 'i': case 'I': {
				assert(key.size() == 4);
				result[result.size() - 2] = 'i'; // SAM only supports 'i' as tag
				int64_t num = unpackInteger(key[3] - '0', oa[keyIndex], out[keyIndex]);
				result += inttostr(num);
				break;
			}
			case 'f': {
				uint32_t num = 0;
				REPEAT(4) num |= (oa[keyIndex].data()[out[keyIndex]++]) << (8 * _);
				result += S("%g", *((float*)(&num)));
				break;
			}
			case 'A':
				result += (char)(oa[keyIndex].data()[out[keyIndex]++]);
				break;
			default: {
				while (oa[keyIndex].data()[out[keyIndex]])
					result += oa[keyIndex].data()[out[keyIndex]++];
				out[keyIndex]++;
				break;
			}
		}
	}

	ZAMAN_END(Decompress_OptionalField_ParseFields);
	return of;
}

const OptionalField &OptionalFieldDecompressor::getRecord(const EditOperation &eo) 
{
	assert(hasRecord());
	ZAMAN_START(Decompress_OptionalField_Get);
	OptionalField &of = records.data()[recordCount++];

	if (of.posMD != -1) {
		of.data = of.data.substr(0, of.posMD) + eo.MD + of.data.substr(of.posMD);
		if (of.posNM > of.posMD) of.posNM += eo.MD.size();
		if (of.posXD > of.posMD) of.posXD += eo.MD.size();
	}
	if (of.posXD != -1) {
		string XD = OptionalFieldCompressor::getXDfromMD(eo.MD);
		of.data = of.data.substr(0, of.posXD) + XD + of.data.substr(of.posXD);
		if (of.posNM > of.posXD) of.posNM += XD.size();
	}
	if (of.posNM != -1) {
		//LOG("%d %d %s %d", of.posNM, eo.NM, of.data.c_str(), of.data.size());
		of.data = of.data.substr(0, of.posNM) + inttostr(eo.NM) + of.data.substr(of.posNM);
	}
	ZAMAN_END(Decompress_OptionalField_Get);
	return of;
}

void OptionalFieldDecompressor::importRecords (uint8_t *in, size_t in_size) 
{
	if (in_size == 0) return;

	ZAMAN_START(Decompress_OptionalField);

	Array<uint8_t> index, sizes;
	size_t s = decompressArray(indexStream, in, index);
	decompressArray(indexStream, in, sizes);
	uint8_t *tags = index.data();
	uint8_t *szs = sizes.data();

	vector<Array<uint8_t>> oa;
	vector<size_t> out;

	records.resize(0);
	while (szs != sizes.data() + sizes.size()) {
		int size = getEncoded(szs) - 1;
		records.add(parseFields(size, tags, in, oa, out));
	}

	recordCount = 0;
	fields.clear();
	ZAMAN_END(Decompress_OptionalField);
}