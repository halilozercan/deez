#ifndef EditOperation_H
#define EditOperation_H

#include <string>

#include "../Common.h"
#include "../Streams/GzipStream.h"
#include "../Streams/BzipStream.h"
#include "../Streams/ACGTStream.h"
#include "../Streams/ArithmeticOrder0Stream.h"
#include "../Engines/GenericEngine.h"
#include "../Engines/StringEngine.h"
#include "../Reference.h"

class SequenceCompressor;
class SequenceDecompressor;

struct EditOperation 
{
	size_t start;
	size_t end;
	std::string seq, MD, op;
	int NM;

	Array<pair<char, int>> ops;

	EditOperation(): NM(-1) {}
	EditOperation(size_t s, const std::string &se, const std::string &op);

	void calculateTags(Reference &reference);
};
template<>
size_t sizeInMemory(EditOperation t);

class EditOperationCompressor: 
	public GenericCompressor<EditOperation, GzipCompressionStream<6> >
{
	const SequenceCompressor &sequence;

public:
	EditOperationCompressor(const SequenceCompressor &seq);

public:
	void outputRecords (const Array<Record> &records, Array<uint8_t> &out, size_t out_offset, size_t k, const Array<EditOperation> &editOps);
	void getIndexData (Array<uint8_t> &out);
	void printDetails(void);

private:	
	void addOperation(char op, int seqPos, int size, vector<Array<uint8_t>> &out);
	void addEditOperation(const Record &record, const EditOperation &eo, ACTGStream *nucleotides, vector<Array<uint8_t>> &out);

public:
	friend class FileCompressor;
	enum Fields {
		LOCATION,
		STITCH,
		OPCODES,
		SEQPOS,
		SEQEND,
		XLEN,
		HSLEN,
		LEN,
		OPLEN,
		ACGT,
		ACGT_IS,
		ACGT_U,
		// ACGT_N,
		ENUM_COUNT
	};
};

class EditOperationDecompressor: 
	public GenericDecompressor<EditOperation, GzipDecompressionStream>  
{
	const SequenceDecompressor &sequence;
	ACTGStream nucleotides[3];

public:
	EditOperationDecompressor(int blockSize, const SequenceDecompressor &seq);

public:
	void importRecords (uint8_t *in, size_t in_size);
	void setIndexData (uint8_t *in, size_t in_size);
	EditOperation &getRecord(size_t i);

private:
	EditOperation getEditOperation (size_t loc, vector<uint8_t*> &fields);
};

#endif // EditOperation_H
