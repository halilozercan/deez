#include "SAMFile.h"
using namespace std;

SAMFileCompressor::SAMFileCompressor (const string &outFile, const string &samFile, const string &genomeFile, int bs): 
	parser(samFile), 
	reference(outFile, genomeFile, bs),
	readName(bs),
	mappingFlag(bs),
	mappingOperation(bs),
	mappingQuality(bs),
	queryQual(bs), 
	pairedEnd(bs),
	optionalField(bs),
	blockSize(bs) 
{
	string name1 = outFile + ".dz";
	outputFile = fopen(name1.c_str(), "wb");
	if (outputFile == NULL)
		throw DZException("Cannot open the file %s", name1.c_str());

	reference.debugStream 			= fopen((outFile + ".Reference").c_str(), "wb");
	readName.debugStream 			= fopen((outFile + ".ReadName").c_str(), "wb");
	mappingFlag.debugStream 		= fopen((outFile + ".MappingFlag").c_str(), "wb");
	mappingOperation.debugStream 	= fopen((outFile + ".MappingOperation").c_str(), "wb");
	mappingQuality.debugStream 		= fopen((outFile + ".MappingQuality").c_str(), "wb");
	queryQual.debugStream 			= fopen((outFile + ".QualityScore").c_str(), "wb");
	pairedEnd.debugStream 			= fopen((outFile + ".PairedEnd").c_str(), "wb");
	optionalField.debugStream 		= fopen((outFile + ".OptionalField").c_str(), "wb");
}

SAMFileCompressor::~SAMFileCompressor (void) {
	fclose(outputFile);
}

uint64_t wasted_block = 0;
void SAMFileCompressor::outputBlock (Compressor *c) {
	vector<char> out;
	c->outputRecords(out);
	size_t i = out.size();
	fwrite(&i, sizeof(size_t), 1, outputFile);
	if(c->debugStream) fwrite(&i, sizeof(size_t), 1, c->debugStream);

	wasted_block += sizeof(size_t);
	if (out.size()) {
		fwrite(&out[0], sizeof(char), out.size(), outputFile);

		if(c->debugStream) fwrite(&out[0], sizeof(char), out.size(), c->debugStream);
	}
}

void SAMFileCompressor::compress (void) {
	int lastFixedLocation = 0;
	int lastStart;
	int64_t total = 0;

	while (parser.hasNext()) {
		while (reference.getName() != parser.head())
			reference.getNext();

		LOG("Loading records ...");
		int i;
		for (i = 0; i < blockSize && parser.hasNext() && parser.head() == reference.getName(); i++) {
			total++;
			const Record &rc = parser.next();

			EditOP e;
			e.start = rc.getMappingLocation();
			e.seq   = rc.getQuerySeq();
			e.op 	= rc.getMappingOperation();

			reference.addRecord(e);
		//	readName.addRecord(rc.getQueryName());
		//	mappingFlag.addRecord(rc.getMappingFlag());
		//	mappingOperation.addRecord(rc.getMappingLocation(), reference.getChromosome(rc.getMappingReference()));
		//	mappingQuality.addRecord(rc.getMappingQuality());
		//	queryQual.addRecord((rc.getMappingFlag() & 16) ? rc.getQueryQualRev() : rc.getQueryQual());
		//	pairedEnd.addRecord(PairedEndInfo(reference.getChromosome(rc.getMateMappingReference()), rc.getMateMappingLocation(), rc.getTemplateLenght()));
		//	optionalField.addRecord(rc.getOptional());

			lastStart = rc.getMappingLocation();
			parser.readNext();
		}
		LOG("%d records are loaded.", i);

		if ((!parser.hasNext() && parser.head() != "*") || parser.head() != reference.getName()) {
			reference.fixGenome(lastFixedLocation, reference.getLength());
			lastFixedLocation = reference.getLength() - 1;
		}
		else if (parser.head() != "*") {
			reference.fixGenome(lastFixedLocation, lastStart - 2);
			lastFixedLocation = lastStart - 2;
		}
		else 
			lastFixedLocation = 1; 

		LOG("Writing to disk ...");

		reference.getBlockBoundary(lastFixedLocation);

		outputBlock(&reference);
	/*	outputBlock(&readName);
		outputBlock(&mappingFlag);
		outputBlock(&mappingOperation);
		outputBlock(&mappingQuality);
		outputBlock(&queryQual);
		outputBlock(&pairedEnd);*/
	//	outputBlock(optionalField);
	}

	LOG("Wasted block info %'llu", wasted_block);
}


SAMFileDecompressor::SAMFileDecompressor (const string &inFile, const string &outFile, const string &genomeFile, int bs): 
	reference(inFile, genomeFile, bs),
	readName(bs),
	mappingFlag(bs),
	mappingOperation(bs),
	mappingQuality(bs),
	editOperation(bs),
	queryQual(bs), 
	pairedEnd(bs),
	optionalField(bs),
	blockSize(bs)
{
	string name1 = inFile + ".dz";
	this->inFile = fopen(name1.c_str(), "rb");
	if (this->inFile == NULL)
		throw DZException("Cannot open the file %s", name1.c_str());

	samFile = fopen(outFile.c_str(), "wb");
	if (samFile == NULL)
		throw DZException("Cannot open the file %s", outFile.c_str());
}

SAMFileDecompressor::~SAMFileDecompressor (void) {
	fclose(samFile);
}

bool SAMFileDecompressor::getSingleBlock (vector<char> &in) {
	int i;
	if (fread(&i, sizeof(int), 1, inFile) != 1)
		return false;
	if (i) {
		in.resize(i);
		fread(&in[0], sizeof(char), i, inFile);
	}

	return true;
}

bool SAMFileDecompressor::getBlock (void) {
	vector<char> in;

	if (!getSingleBlock(in))
		return false;
	reference.importRecords(in);

	getSingleBlock(in);
	readName.importRecords(in);

	getSingleBlock(in);
	mappingFlag.importRecords(in);

	getSingleBlock(in);
	mappingOperation.importRecords(in);

	getSingleBlock(in);
	mappingQuality.importRecords(in);

	getSingleBlock(in);
	queryQual.importRecords(in);

	getSingleBlock(in);
	pairedEnd.importRecords(in);

	getSingleBlock(in);
	optionalField.importRecords(in);

	return true;
}

void SAMFileDecompressor::decompress (void) {
	while (getBlock()) {		
		while (1) {
			if (!reference.hasRecord() ||
				!mappingOperation.hasRecord() || 
				!readName.hasRecord() ||
				!mappingFlag.hasRecord() ||
				!mappingQuality.hasRecord() ||
				!queryQual.hasRecord() ||
				!pairedEnd.hasRecord()) 
				break;

			Locs dtmp = mappingOperation.getRecord();
			while (reference.getChromosome(dtmp.ref) != reference.getName()) {
				reference.getNext();
			}
			string  dname = readName.getRecord();
			short   dflag = mappingFlag.getRecord();
			uint8_t dmq   = mappingQuality.getRecord();
			EditOP  eo    = reference.getRecord(dtmp.loc - 1);
			string  dqual = queryQual.getRecord();
			PairedEndInfo mate = pairedEnd.getRecord();
			
			/*if (tl & (1 << 31)) {
			mateRef = reference.getChromosome(tl & ~(1 << 31));
			mateLoc = pairedEnd.getRecord();
			tl = 0;
			}
			else { // not realy!
			if (tl & (1 << 30)) 
			tl = -(tl & ~(1 << 30));
			mateLoc = (int)dtmp.loc + tl - eo.end + eo.start - 2;
			mateRef = "=";
			}*/

			fprintf(samFile, "%s\t%d\t%s\t%d\t%d\t%s\t%s\t%d\t%d\t%s\t%s",
				dname.c_str(),
				dflag,
				reference.getChromosome(dtmp.ref).c_str(),
				dtmp.loc,
				dmq,
				eo.op.c_str(),
				(mate.chr == dtmp.ref && reference.getChromosome(dtmp.ref) != "*") ? "=" : reference.getChromosome(mate.chr).c_str(), 
				mate.pos,
				mate.tlen,
				eo.seq.c_str(),
				dqual.c_str()
			);

			string optional = optionalField.getRecord();
			if (optional.size())
				fprintf(samFile, "\t%s", optional.c_str());
			fprintf(samFile, "\n");
		}
	}
}

