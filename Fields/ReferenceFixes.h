#ifndef ReferenceFixes_H
#define ReferenceFixes_H

#include <map>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "../Common.h"
#include "../Reference.h"
#include "../Engines/Engine.h"
#include "../Engines/GenericEngine.h"
#include "../Engines/StringEngine.h"
#include "../Streams/GzipStream.h"

struct EditOP {
	size_t start;
	size_t end;
	std::string seq;
	std::string op;

	EditOP() {}
	EditOP(size_t s, const std::string &se, const std::string &o) :
		start(s), seq(se), op(o) {}
};

struct GenomePager {
	static const int GenomePageSize = 256 * KB;

	size_t start;
	char fixes[GenomePageSize];
	int stat[GenomePageSize][5];

	GenomePager *next;

	GenomePager (size_t s): start(s), next(0) {
		memset(stat, 0, sizeof(int) * GenomePageSize * 5);
	}
	~GenomePager (void) {
		// do the cleaning manually in update() and in fixGenome()
	}

	void increase (size_t loc, char ch) {
		if (loc < start + GenomePageSize) {
			/*if (!stat[loc - start]) {
				stat[loc - start] = new int[6];
				memset(stat[loc - start], 0, sizeof(int) * 6);
			}*/
			assert(ch<6);
			assert(loc-start<GenomePageSize);
			stat[loc - start][ch]++;
		//	DEBUG("%d -> %d = %d", loc, ch, stat[loc - start][ch]);
		}
		else {
			if (!next || next->start > loc) {
				GenomePager *g = new GenomePager(loc - loc % GenomePageSize);
				g->next = next;
				next = g;
			}
			next->increase(loc, ch);
		}
	}

	char getFixed (size_t loc) {
		assert(loc >= start);
		if (loc < start + GenomePageSize) 
			return fixes[loc - start];
		else {
			if (!next) throw "error in genomepager";
			return next->getFixed(loc);
		}
	}
};

typedef StringCompressor<GzipCompressionStream<6> > 
	EditOperationCompressor;
typedef StringDecompressor<GzipDecompressionStream> 
	EditOperationDecompressor;

struct GenomeChanges {
	uint32_t loc;
	uint8_t	changed;

	GenomeChanges () {}
	GenomeChanges (uint32_t l, char c): loc(l), changed(c) {}
};

typedef GenericCompressor<GenomeChanges, GzipCompressionStream<6> > 
	FixesCompressor;
typedef GenericDecompressor<GenomeChanges, GzipDecompressionStream> 
	FixesDecompressor;

class ReferenceFixesCompressor: public Compressor {
	Reference reference;
	EditOperationCompressor editOperation;
	FixesCompressor fixes;

	// temporary for the Cigars before the genome fixing
	std::vector<EditOP> records;

	GenomePager *genomePager;

	std::string chromosome; // chromosome index
	size_t blockStart;
	size_t lastFixed;
	size_t nextStart;

public:
	ReferenceFixesCompressor (const std::string &refFile, int bs);
	~ReferenceFixesCompressor (void);

public:
	void addRecord (size_t loc, const std::string &seq, const std::string &cigar);
	void outputRecords (Array<uint8_t> &output);

	void applyFixes (size_t end, size_t&, size_t&, size_t&);
	
public:
	std::string getChromosome (void) const { return chromosome; }
	void scanNextChromosome (void);
	
private:
	void updateGenomeLoc (size_t loc, char ch);
	size_t updateGenome (size_t loc, const std::string &seq, const std::string &op);
	std::string getEditOP (size_t loc, const std::string &seq, const std::string &op);
};

class ReferenceFixesDecompressor: public Decompressor {
	Reference reference;
	EditOperationDecompressor editOperation;
	FixesDecompressor fixes;

	char   *fixed;
	size_t fixed_offset;
	size_t fixed_size;
	size_t nextStart;
	std::string chromosome;

public:
	ReferenceFixesDecompressor (const std::string &refFile, int bs);
	~ReferenceFixesDecompressor (void);

public:
	bool hasRecord (void);
	EditOP getRecord (size_t loc);

	void importRecords (uint8_t *in, size_t in_size);

public:
	void scanNextChromosome (void);
	std::string getChromosome (void) const { return chromosome; }

private:
	void importFixes (uint8_t *in, size_t in_size);
	EditOP getSeqCigar (size_t loc, const std::string &op);	
};

#endif
