#ifndef GenericEngine_H
#define GenericEngine_H

#include <vector>

#include "../Common.h"
#include "Engine.h"

template<typename T, typename TStream>
class GenericCompressor: public Compressor {
protected:
	std::vector<T> records;

public:
	GenericCompressor (int blockSize);
	virtual ~GenericCompressor (void);

public:
	virtual void addRecord (const T &rec);
	virtual void outputRecords (std::vector<char> &output);
};

template<typename T, typename TStream>
class GenericDecompressor: public Decompressor {
protected:
	std::vector<T> records;
	size_t recordCount;

public:
	GenericDecompressor (int blockSize);
	virtual ~GenericDecompressor (void);

public:
	virtual const T &getRecord (void);
	virtual bool hasRecord (void);
	virtual void importRecords (const std::vector<char> &input);
};

#include "GenericEngine.tcc"

#endif