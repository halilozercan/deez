#include "Decompress.h"
#include <thread>
using namespace std;

static const char *NAMES[8] = {
	"SQ","RN","MF","ML", 
	"MQ","QQ","PE","OF" 
};

void FileDecompressor::printStats (const string &path, int filterFlag) {
	File *inFile = OpenFile(path.c_str(), "rb");

	if (inFile == NULL)
		throw DZException("Cannot open the file %s", path.c_str());

	size_t inFileSz = inFile->size();

	// seek to index
	inFile->seek(inFileSz - sizeof(size_t));
	size_t statPos = inFile->readU64();

	char magic[10] = {0};
	inFile->read(magic, 7, statPos);
	if (strcmp(magic, "DZSTATS"))
		throw DZException("Stats are corrupted ...%s", magic);

	size_t sz = inFile->readU64();
	Array<uint8_t> in(sz);
	in.resize(sz);
	inFile->read(in.data(), sz);
	Stats *stats = new Stats(in);

	LOG("Index size %'lu bytes", inFileSz - statPos);
	LOG("%'16lu reads", stats->getReadCount());
	LOG("%'16lu mapped reads", stats->getStats(-4));
	LOG("%'16lu unmapped reads", stats->getStats(4));
	LOG("%'16lu chromosomes in reference file", stats->getChromosomeCount());
	if (filterFlag) {
		size_t p = stats->getStats(filterFlag);
		if (filterFlag > 0)
			LOG("%'16lu records with flag %d(0x%x)", p, filterFlag, filterFlag);
		else
			LOG("%'16lu records without flag %d(0x%x)", p, -filterFlag, -filterFlag);
	}
	else {
		for (int i = 0; i < Stats::FLAGCOUNT; i++) {
			size_t p = stats->getFlagCount(i);
			if (p) LOG("%4d 0x%04x: %'16lu", i, i, p);
		}
	}

	inFile->close();
	delete inFile;
}

FileDecompressor::FileDecompressor (const string &inFilePath, const string &outFile, const string &genomeFile, int bs): 
	blockSize(bs), genomeFile(genomeFile), outFile(outFile)
{
	string name1 = inFilePath;
	this->inFile = OpenFile(name1.c_str(), "rb");
	if (this->inFile == NULL)
		throw DZException("Cannot open the file %s", name1.c_str());

	inFileSz = inFile->size();

	// seek to index
	inFile->seek(inFileSz - sizeof(size_t));
	size_t statPos = inFile->readU64();
	char magic[10] = {0};
	inFile->read(magic, 7, statPos);
	if (strcmp(magic, "DZSTATS"))
		throw DZException("Stats are corrupted ...%c%c%c%c%c%c%c", magic[0], magic[1], magic[2], magic[3], magic[4], magic[5], magic[6]);

	size_t sz = inFile->readU64();
	Array<uint8_t> in(sz);
	in.resize(sz);
	inFile->read(in.data(), sz);
	stats = new Stats(in);

	magic[5] = 0;
	inFile->read(magic, 5);
	if (strcmp(magic, "DZIDX"))
		throw DZException("Index is corrupted ...%s", magic);

	size_t idxToRead = inFileSz - inFile->tell() - sizeof(size_t);
	FILE *tmp = tmpfile();
	char *buffer = (char*)malloc(MB);

	while (idxToRead && (sz = inFile->read(buffer, min(uint64_t(MB), (uint64_t)idxToRead)))) {
		fwrite(buffer, 1, sz, tmp);
		idxToRead -= sz;
	}
	free(buffer);
	
	int idx = dup(fileno(tmp));

	//fseek(tmp, 0, SEEK_END);
	//LOG("Index gz'd sz=%'lu", ftell(tmp));
	fseek(tmp, 0, SEEK_SET);
	lseek(idx, 0, SEEK_SET); // needed for gzdopen
	idxFile = gzdopen(idx, "rb");
	//idxFile = gzopen((inFile+".dzi").c_str(), "rb");
	if (idxFile == Z_NULL)
		throw DZException("Cannot open the index");
		
	inFile->seek(0);
}

FileDecompressor::~FileDecompressor (void) {
	for (int f = 0; f < fileNames.size(); f++) {
		delete sequence[f];
		delete editOp[f];
		delete readName[f];
		delete mapFlag[f];
		delete mapQual[f];
		delete quality[f];
		delete pairedEnd[f];
		delete optField[f];
		if (samFiles[f]) fclose(samFiles[f]);
	}
	if (idxFile) gzclose(idxFile);
	if (inFile) {
		inFile->close();
		delete inFile;
	}
}

void FileDecompressor::getMagic (void) {
	magic = inFile->readU32();
	// LOG("File format: %c%c v%d.%d", 
	// 	(magic >> 16) & 0xff, 
	// 	(magic >> 8) & 0xff, 
	// 	(magic >> 4) & 0xf,
	// 	magic & 0xf
	// );
	LOG("File version: 0x%x", magic & 0xff);
	optQuality = inFile->readU8();

	uint16_t numFiles = 1;
	if ((magic & 0xff) >= 0x11)
		numFiles = inFile->readU16();

	for (int f = 0; f < numFiles; f++) {
		sequence.push_back(new SequenceDecompressor(genomeFile, blockSize));
		editOp.push_back(new EditOperationDecompressor(blockSize));
		readName.push_back(new ReadNameDecompressor(blockSize));
		mapFlag.push_back(new MappingFlagDecompressor(blockSize));
		mapQual.push_back(new MappingQualityDecompressor(blockSize));
		pairedEnd.push_back(new PairedEndDecompressor(blockSize));
		optField.push_back(new OptionalFieldDecompressor(blockSize));
		quality.push_back(new QualityScoreDecompressor(blockSize));
	}
	fileNames.resize(numFiles);

	if ((magic & 0xff) >= 0x11) {
		size_t arcsz = inFile->readU64();
		if (arcsz) {
			Array<uint8_t> arc;
			arc.resize(arcsz);
			arcsz = inFile->readU64();
			Array<uint8_t> files;
			files.resize(arcsz);
			inFile->read(files.data(), arcsz);
			
			GzipDecompressionStream gzc;
			gzc.decompress(files.data(), files.size(), arc, 0);

			arcsz = 0;
			for (int f = 0; f < fileNames.size(); f++) {
				fileNames[f] = string((const char*)(arc.data()) + arcsz);
				arcsz += fileNames[f].size();
			}
		}
	}
	samFiles.resize(numFiles);
	for (int f = 0; f < numFiles; f++) {
		if (optStdout)
			samFiles[f] = stdout;
		else {
			string fn = outFile;
			if (numFiles > 1) fn += S("_%d", f + 1);
			samFiles[f] = fopen(fn.c_str(), "wb");
			if (samFiles[f] == NULL)
				throw DZException("Cannot open the file %s", fn.c_str());
		}
	}
}

void FileDecompressor::getComment (bool output) {
	for (int f = 0; f < fileNames.size(); f++) {
		size_t arcsz = inFile->readU64();
		if (arcsz) {
			Array<uint8_t> arc;
			arc.resize(arcsz);
			arcsz = inFile->readU64();
			Array<uint8_t> comment;
			comment.resize(arcsz);
			inFile->read(comment.data(), arcsz);
			
			GzipDecompressionStream gzc;
			gzc.decompress(comment.data(), comment.size(), arc, 0);

			if (output) 
				fwrite(arc.data(), 1, arc.size(), samFiles[f]);
		}
	}
}

void FileDecompressor::readBlock (Decompressor *d, Array<uint8_t> &in) {
	size_t sz = inFile->readU64();
	in.resize(sz);
	if (sz) inFile->read(in.data(), sz);
	d->importRecords(in.data(), in.size());
}

void readBlockThread (Decompressor *d, Array<uint8_t> &in) {
	d->importRecords(in.data(), in.size());
}

size_t FileDecompressor::getBlock (int f, const string &chromosome, 
	size_t start, size_t end, int filterFlag) 
{
	static string chr;
	static bool done(false);
	if (done)
		return 0;
	if (chromosome != "")
		chr = chromosome;

	char chflag;
	if (inFile->read(&chflag, 1) != 1)
		return 0;
	if (chflag > 1) // index!
		return 0;
	if (chflag) {
		chr = "";
		chflag = inFile->readU8();
		while (chflag)
			chr += chflag, chflag = inFile->readU8();
		if (chromosome != "" && chr != chromosome)
			return 0;
	}

	while (chr != sequence[f]->getChromosome())
		sequence[f]->scanChromosome(chr);

	Array<uint8_t> in[8];
	readBlock(sequence[f], in[7]);
	sequence[f]->setFixed(*(editOp[f]));

	Decompressor *di[] = { editOp[f], readName[f], mapFlag[f], mapQual[f], quality[f], pairedEnd[f], optField[f] };
	thread t[7];
	for (int ti = 0; ti < 7; ti++) {
		size_t sz = inFile->readU64();
		in[ti].resize(sz);
		if (sz) inFile->read(in[ti].data(), sz);
		t[ti] = thread(readBlockThread, di[ti], ref(in[ti]));
	}
	for (int ti = 0; ti < 7; ti++)
		t[ti].join();

	size_t count = 0;
	while (editOp[f]->hasRecord()) {
		string rname = readName[f]->getRecord();
		int flag = mapFlag[f]->getRecord();
		EditOperation eo = editOp[f]->getRecord();
		int mqual = mapQual[f]->getRecord();
		string qual = quality[f]->getRecord(eo.seq.size(), flag);
		string optional = optField[f]->getRecord();
		PairedEndInfo pe = pairedEnd[f]->getRecord(chr, eo.start);

		if (filterFlag) {
			if (filterFlag > 0 && (flag & filterFlag) != filterFlag)
				continue;
			if (filterFlag < 0 && (flag & -filterFlag) == -filterFlag)
				continue;
		}
		if (eo.start < start)
			continue;
		if (eo.start > end) {
			done = true;
			return count;
		}

		if (chr != "*") eo.start++;
		if (pe.chr != "*") pe.pos++;

		fprintf(samFiles[f], "%s\t%d\t%s\t%zu\t%d\t%s\t%s\t%lu\t%d\t%s\t%s",
		 	rname.c_str(),
		 	flag,
		 	chr.c_str(),
		 	eo.start,
		 	mqual,
		 	eo.op.c_str(),
		 	pe.chr.c_str(),
		 	pe.pos,
		 	pe.tlen,
		 	eo.seq.c_str(),
		 	qual.c_str()
		);
		if (optional.size())
			fprintf(samFiles[f], "\t%s", optional.c_str());
		fprintf(samFiles[f], "\n");

		if (count % (1 << 16) == 0) 
			LOGN("\r   Chr %-6s %5.2lf%%", chr.c_str(), (100.0 * inFile->tell()) / inFileSz);
		count++;
	}
	return count;
}

vector<int> FileDecompressor::loadIndex (bool inMemory = false) 
{
	vector<int> files;
	bool firstRead = 1;
	indices.resize(fileNames.size());
	while (1) {
		index_t idx;
		string chr;
		
		size_t sz;
		if (firstRead) 
			firstRead = 0;
		else for (int i = 0; i < 8; i++) {
			gzread(idxFile, &sz, sizeof(size_t));
			idx.fieldData[i].resize(sz); 
			if (sz) gzread(idxFile, idx.fieldData[i].data(), sz); 
		}

		int16_t f = 0;
		if ((this->magic & 0xff) >= 0x11) {
			if (gzread(idxFile, &f, sizeof(int16_t)) != sizeof(int16_t)) 
				break;
			gzread(idxFile, &idx.zpos, sizeof(size_t));
		}
		else {
			if (gzread(idxFile, &idx.zpos, sizeof(size_t)) != sizeof(size_t))
				break;
		}
		files.push_back(f);
		
		gzread(idxFile, &idx.currentBlockCount, sizeof(size_t));
		char c; while (gzread(idxFile, &c, 1) && c) chr += c;
		gzread(idxFile, &idx.startPos, sizeof(size_t));
		gzread(idxFile, &idx.endPos, sizeof(size_t));
		gzread(idxFile, &idx.fS, sizeof(size_t));
		gzread(idxFile, &idx.fE, sizeof(size_t));

		if (inMemory)
			indices[f][chr][idx.startPos] = idx;
	}
	return files;
}

vector<pair<pair<int, string>, pair<size_t, size_t>>> FileDecompressor::getRanges (string range)
{
	if (range.size() && range[range.size() - 1] != ';') 
		range += ';';
	vector<pair<pair<int, string>, pair<size_t, size_t>>> ranges;

	size_t p;
	while ((p = range.find(';')) != string::npos) {
		string chr;
		size_t start, end;
		char *dup = strdup(range.substr(0, p).c_str());
		char *tok = strtok(dup, ":-");
		
		if (tok) {
			chr = tok, tok = strtok(0, ":-");
			if (tok) {
				start = atol(tok), tok = strtok(0, ":-");
				if (tok) 
					end = atol(tok), tok = strtok(0, ":-");
				else 
					end = -1;
			}
			else {
				start = 1; end = -1;
			}
		}
		else throw DZException("Range string %s invalid", range.substr(0, p - 1).c_str());
		if (end < start)
			swap(start, end);
		if (start) start--; 
		if (end) end--;

		range = range.substr(p + 1);
		int f = 0;
		if ((p = chr.find(',')) != string::npos) {
			f = atoi(chr.substr(0, p).c_str());
			chr = chr.substr(p + 1);
		}

		ranges.push_back(make_pair(make_pair(f, chr), make_pair(start, end)));
	}

	return ranges;
}

void FileDecompressor::decompress (int filterFlag) 
{
	getMagic();
	getComment(true);
	auto fv = loadIndex();

	size_t blockSz = 0, 
		   totalSz = 0, 
		   blockCount = 0;
	
	while ((blockSz = getBlock(fv[blockCount], "", 0, -1, filterFlag)) != 0) {
		totalSz += blockSz;
		blockCount++;
	}
	LOGN("\nDecompressed %'lu records, %'lu blocks\n", totalSz, blockCount);
}

void FileDecompressor::decompress (const string &idxFilePath, 
	const string &range, int filterFlag) 
{
	getMagic();
	getComment(false);
	loadIndex(true);
	auto ranges = getRanges(range);

	size_t 	blockSz = 0, 
			totalSz = 0, 
			blockCount = 0;

	foreach (r, ranges) {
		int f = r->first.first;
		string chr = r->first.second;
		if (f < 0 || f >= fileNames.size())
			throw DZException("Invalid sample ID %d", f);
		if (indices[f].find(chr) == indices[f].end())
			throw DZException("Invalid chromosome %s for sample ID %d", chr.c_str(), f);

		auto idx = indices[f][chr];

		auto i = idx.upper_bound(r->second.first);
		if (i == idx.end() || i == idx.begin())
			throw DZException("Region %s:%d-%d not found for sample ID %d", 
				chr.c_str(), r->second.first, r->second.second, f);
		i--;
		
		// prepare reference
		foreach (j, idx) { // TODO speed up
			if (j == i) break;
			if (r->second.first >= j->second.fS && r->second.first <= j->second.fE) {
				inFile->seek(j->second.zpos);
				char chflag = inFile->readU8();
				while (chflag) chflag = inFile->readU8();
				while (chr != sequence[f]->getChromosome())
					sequence[f]->scanChromosome(chr);

				Array<uint8_t> in;
				readBlock(sequence[f], in);
			}
		}
		// set up field data
		while (r->second.first >= i->second.startPos && r->second.first <= i->second.endPos) {		
			inFile->seek(i->second.zpos);
			Decompressor *di[] = { 
				sequence[f], editOp[f], readName[f], mapFlag[f], 
				mapQual[f], quality[f], pairedEnd[f], optField[f] 
			};
			for (int ti = 0; ti < 8; ti++) if (i->second.fieldData[ti].size()) 
				di[ti]->setIndexData(i->second.fieldData[ti].data(), i->second.fieldData[ti].size());

			size_t blockSz = getBlock(f, chr, r->second.first, r->second.second, filterFlag);
			if (!blockSz) break;
			totalSz += blockSz;
			blockCount++;

			i++;
		}
	}

	LOGN("\nDecompressed %'lu records, %'lu blocks\n", totalSz, blockCount);
}

void FileDecompressor::query (const string &query, const string &range)
{
	if (query == "allele") {
		/*getMagic();
		getComment(false);
		loadIndex(true);
		auto ranges = getRanges(range);

		foreach (r, ranges) {
			LOG("Range %s:%zu-%zu", r->first.second.c_str(), r->second.first+1, r->second.second+1);
			for (int f = 0; f < fileNames.size(); f++) { 
				string chr = r->first.second;
				if (indices[f].find(chr) == indices[f].end())
					throw DZException("Invalid chromosome %s for sample ID %d", chr.c_str(), f);

				auto idx = indices[f][chr];

				auto i = idx.upper_bound(r->second.first);
				if (i == idx.end() || i == idx.begin())
					throw DZException("Region %s:%d-%d not found for sample ID %d", 
						chr.c_str(), r->second.first, r->second.second, f);
				i--;
				
				// prepare reference
				foreach (j, idx) { // TODO speed up
					if (j == i) break;
					if (r->second.first >= j->second.fS && r->second.first <= j->second.fE) {
						fseek(inFile, j->second.zpos, SEEK_SET);

						if (j->second.fieldData[0].size())
							sequence[f]->setIndexData(j->second.fieldData[0].data(), j->second.fieldData[0].size());

						char chflag;
						fread(&chflag, 1, 1, inFile);
						while (chflag) fread(&chflag, 1, 1, inFile);
						while (chr != sequence[f]->getChromosome())
							sequence[f]->scanChromosome(chr);

						Array<uint8_t> in;
						readBlock(sequence[f], in);
					}
				}
				LOG("File %s:", fileNames[f].c_str());
				while (1) {
					fseek(inFile, i->second.zpos, SEEK_SET);
					char chflag;
					fread(&chflag, 1, 1, inFile);
					while (chflag) fread(&chflag, 1, 1, inFile);
					while (chr != sequence[f]->getChromosome())
						sequence[f]->scanChromosome(chr);
					Array<uint8_t> in;
					readBlock(sequence[f], in);

					auto ch = sequence[f]->getChanges();
					auto pi = ch.lower_bound(r->second.first);
					if (pi == ch.end()) goto end;
					for (; pi != ch.end(); pi++)
					{
						if (pi->first < r->second.first)
							continue;
						if (r->second.second < pi->first)
							goto end;
						LOGN("\t%d:", pi->first+1);
						if (pi->second >= 85) LOGN("%c/%c", ".ACGTN"[(pi->second-85)/6], ".ACGTN"[(pi->second-85)%6]);
						else LOGN("%c", pi->second);
					}
					i++;
				}
				end:;
				LOG("");
			}
		}*/
	}
	else {
		throw DZException("Sorry, but query %s is not supported.", query.c_str());
	}
}
