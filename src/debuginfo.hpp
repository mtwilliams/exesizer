// Executable size report utility.
// Aras Pranckevicius, http://aras-p.info/projSizer.html
// Based on code by Fabian "ryg" Giesen, http://farbrausch.com/~fg/

#ifndef __DEBUGINFO_HPP__
#define __DEBUGINFO_HPP__

#include "types.hpp"
#include <map>
#include "HtmlTable.h"

using std::string;

/****************************************************************************/

#define DIC_END     0
#define DIC_CODE    1
#define DIC_DATA    2
#define DIC_BSS			3 // uninitialized data
#define DIC_UNKNOWN 4

struct DISymFile // File
{
	sInt	fileName;
	sU32	codeSize;
	sU32	dataSize;
};

struct DISymNameSp // Namespace
{
	sInt	name;
	sU32	codeSize;
	sU32	dataSize;
};

struct DISymbol
{
	sInt name;
	sInt mangledName;
	sInt NameSpNum;
	sInt objFileNum;
	sU32 VA;
	sU32 Size;
	sInt Class;
};

struct TemplateSymbol
{
	string	name;
	sU32	size;
	sU32	count;
};


class FunctionReport
{
public:
	FunctionReport(const std::string &type,NVSHARE::HtmlDocument *document)
	{
		char scratch[1024];
		sprintf(scratch,"Functions by size for code region '%s'", type.c_str() );
		mTable = document->createHtmlTable(scratch);
		mTable->addHeader("Function/Name,Function/Size,Object/File");
		mTable->addSort(scratch,2,false,1,true);
		mTable->computeTotals();
		mTotalFunctionSize = 0;
		mTotalFunctionCount = 0;
	}

	~FunctionReport(void)
	{
	}

	void addFunction(const char *function,const char *objectFile,size_t functionSize)
	{
		mTable->addColumn(function);
		mTable->addColumn(functionSize);
		mTable->addColumn(objectFile);
		mTable->nextRow();
		mTotalFunctionSize+=functionSize;
		mTotalFunctionCount++;
	}


	NVSHARE::HtmlTable	*mTable;
	size_t mTotalFunctionSize;
	size_t mTotalFunctionCount;
};

typedef std::map< std::string, FunctionReport * > FunctionReportMap;

class ByObject
{
public:
	ByObject(const std::string &oname,NVSHARE::HtmlDocument *document)
	{
		char scratch[1024];
		sprintf(scratch,"Functions by size for object file '%s'", oname.c_str() );
		mTable = document->createHtmlTable(scratch);
		mTable->addHeader("Function/Name,Code/Size");
		mTable->addSort(scratch,2,false,1,true);
		mTable->computeTotals();
		mFunctionCount = 0;
		mCodeSize = 0;
	}

	void addFunction(const char *function,size_t codeSize)
	{
		mTable->addColumn(function);
		mTable->addColumn(codeSize);
		mTable->nextRow();
		mFunctionCount++;
		mCodeSize+=codeSize;
	}

	size_t				mFunctionCount;
	size_t				mCodeSize;
	NVSHARE::HtmlTable	*mTable;
};

typedef std::map< std::string, ByObject * > ByObjectMap;

class ObjectReport
{
public:
	ObjectReport(const std::string &type,NVSHARE::HtmlDocument *document)
	{
		char scratch[1024];
		sprintf(scratch,"Object files by size for code region '%s'", type.c_str() );
		mTable = document->createHtmlTable(scratch);
		mTable->addHeader("Object/File,Function/Count,Code/Size");
		mTable->addSort(scratch,3,false,2,false);
		mTable->computeTotals();
	}

	~ObjectReport(void)
	{
	}

	void addFunction(const char *function,const char *objectFile,size_t functionSize)
	{
		std::string oname = objectFile;
		ByObject *bo;
		ByObjectMap::iterator found = mObjects.find(oname);
		if ( found == mObjects.end() )
		{
			bo = new ByObject(oname,mTable->getDocument());
			mObjects[oname] = bo;
		}
		else
		{
			bo = (*found).second;
		}
		bo->addFunction(function,functionSize);
	}

	void finalReport(NVSHARE::HtmlTable *table)
	{
		for (ByObjectMap::iterator i=mObjects.begin(); i!=mObjects.end(); i++)
		{
			const char *oname = (*i).first.c_str();
			ByObject &bo = *(*i).second;
			mTable->addColumn(oname);
			mTable->addColumn(bo.mFunctionCount);
			mTable->addColumn(bo.mCodeSize);
			mTable->nextRow();

			table->addColumn(oname);
			table->addColumn(bo.mFunctionCount);
			table->addColumn(bo.mCodeSize);
			table->nextRow();
		}
	}

	ByObjectMap			mObjects;
	NVSHARE::HtmlTable	*mTable;

};

typedef std::map< std::string, ObjectReport * > ObjectReportMap;

class DebugInfo
{
	typedef std::vector<string>		StringByIndexVector;
	typedef std::map<string,sInt>	IndexByStringMap;

	StringByIndexVector	m_StringByIndex;
	IndexByStringMap	m_IndexByString;
	sU32 BaseAddress;

	sU32 CountSizeInClass(sInt type) const;

public:
  sArray<DISymbol>			Symbols;
  sArray<TemplateSymbol>	Templates;
  sArray<DISymFile>			m_Files;
  sArray<DISymNameSp>		NameSps;

  void Init();
  void Exit();

  // only use those before reading is finished!!
  sInt MakeString(sChar *s);
  const char* GetStringPrep( sInt index ) const { return m_StringByIndex[index].c_str(); }
  void SetBaseAddress(sU32 base)            { BaseAddress = base; }

  void FinishedReading();

  sInt GetFile( sInt fileName );
  sInt GetFileByName( sChar *objName );

  sInt GetNameSpace(sInt name);
  sInt GetNameSpaceByName(sChar *name);

  void StartAnalyze();
  void FinishAnalyze();
  sBool FindSymbol(sU32 VA,DISymbol **sym);

  std::string WriteReport();

	void addFunctionReport(const char *function,const char *objectFile,size_t functionSize);

	FunctionReportMap	mFunctions;
	ObjectReportMap		mObjects;

	NVSHARE::HtmlDocument *mDocument;

};

class DebugInfoReader
{
public:
  virtual sBool ReadDebugInfo(sChar *fileName,DebugInfo &to) = 0;
};


#endif
