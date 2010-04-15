// Executable size report utility.
// Aras Pranckevicius, http://aras-p.info/projSizer.html
// Based on code by Fabian "ryg" Giesen, http://farbrausch.com/~fg/
#pragma warning(disable:4996)
#include "types.hpp"
#include "debuginfo.hpp"
#include <stdarg.h>
#include <algorithm>
#include <map>

#include <windows.h>
#include <DbgHelp.h>

#include "sutil.h"

#define ONLY_APEX 1

#pragma comment(lib,"DbgHelp.lib")

/****************************************************************************/

sU32 DebugInfo::CountSizeInClass(sInt type) const
{
	sU32 size = 0;
	for(sInt i=0;i<Symbols.size();i++)
		size += (Symbols[i].Class == type) ? Symbols[i].Size : 0;

	return size;
}

void DebugInfo::Init()
{
  BaseAddress = 0;
}

void DebugInfo::Exit()
{
}

sInt DebugInfo::MakeString( sChar *s )
{
	string str( s );
	IndexByStringMap::iterator it = m_IndexByString.find( str );
	if( it != m_IndexByString.end() )
		return it->second;

	sInt index = m_IndexByString.size();
	m_IndexByString.insert( std::make_pair(str,index) );
	m_StringByIndex.push_back( str );
	return index;
}

bool virtAddressComp(const DISymbol &a,const DISymbol &b)
{
  return a.VA < b.VA;
}

static bool StripTemplateParams( std::string& str )
{
	bool isTemplate = false;
	int start = str.find( '<', 0 );
	while( start != std::string::npos )
	{
		isTemplate = true;
		// scan to matching closing '>'
		int i = start + 1;
		int depth = 1;
		while( i < str.size() )
		{
			char ch = str[i];
			if( ch == '<' )
				++depth;
			if( ch == '>' )
			{
				--depth;
				if( depth == 0 )
					break;
			}
			++i;
		}
		if( depth != 0 )
			return isTemplate; // no matching '>', just return

		str = str.erase( start, i-start+1 );

		start = str.find( '<', start );
	}

	return isTemplate;
}

void DebugInfo::FinishedReading()
{
	// fix strings and aggregate templates
	typedef std::map<std::string, int> StringIntMap;
	StringIntMap templateToIndex;

	for(sInt i=0;i<Symbols.size();i++)
	{
		DISymbol *sym = &Symbols[i];

		std::string templateName = GetStringPrep( sym->name );
		bool isTemplate = StripTemplateParams( templateName );
		if( isTemplate )
		{
			StringIntMap::iterator it = templateToIndex.find( templateName );
			int index;
			if( it != templateToIndex.end() )
			{
				index = it->second;
				Templates[index].size += sym->Size;
				Templates[index].count++;
			}
			else
			{
				index = Templates.size();
				templateToIndex.insert( std::make_pair(templateName, index) );
				TemplateSymbol tsym;
				tsym.name = templateName;
				tsym.count = 1;
				tsym.size = sym->Size;
				Templates.push_back( tsym );
			}
		}
	}

  // sort symbols by virtual address
  std::sort(Symbols.begin(),Symbols.end(),virtAddressComp);

  // remove address double-covers
  sInt symCount = Symbols.size();
  DISymbol *syms = new DISymbol[symCount];
  sCopyMem(syms,&Symbols[0],symCount * sizeof(DISymbol));

  Symbols.clear();
  sU32 oldVA = 0;
  sInt oldSize = 0;

  for(sInt i=0;i<symCount;i++)
  {
    DISymbol *in = &syms[i];
    sU32 newVA = in->VA;
    sU32 newSize = in->Size;

    if(oldVA != 0)
    {
      sInt adjust = newVA - oldVA;
      if(adjust < 0) // we have to shorten
      {
        newVA = oldVA;
        if(newSize >= -adjust)
          newSize += adjust;
      }
    }

    if(newSize || in->Class == DIC_END)
    {
		Symbols.push_back(DISymbol());
		DISymbol *out = &Symbols.back();
		*out = *in;
		out->VA = newVA;
		out->Size = newSize;
      
		oldVA = newVA + newSize;
		oldSize = newSize;
    }
  }

  delete[] syms;
}

sInt DebugInfo::GetFile( sInt fileName )
{
	for( sInt i=0;i<m_Files.size();i++ )
		if( m_Files[i].fileName == fileName )
			return i;

	m_Files.push_back( DISymFile() );
	DISymFile *file = &m_Files.back();
	file->fileName = fileName;
	file->codeSize = file->dataSize = 0;

	return m_Files.size() - 1;
}

sInt DebugInfo::GetFileByName( sChar *objName )
{
	return GetFile( MakeString(objName) );
}

sInt DebugInfo::GetNameSpace(sInt name)
{
  for(sInt i=0;i<NameSps.size();i++)
    if(NameSps[i].name == name)
      return i;

  DISymNameSp namesp;
  namesp.name = name;
  namesp.codeSize = namesp.dataSize = 0;
  NameSps.push_back(namesp);

  return NameSps.size() - 1;
}

sInt DebugInfo::GetNameSpaceByName(sChar *name)
{
  sChar *pp = name - 2;
  sChar *p;
  sInt cname;

  while((p = (sChar *) sFindString(pp+2,"::")))
    pp = p;

  while((p = (sChar *) sFindString(pp+1,".")))
    pp = p;

  if(pp != name - 2)
  {
    sChar buffer[2048];
    sCopyString(buffer,name,2048);

    if(pp - name < 2048)
      buffer[pp - name] = 0;

    cname = MakeString(buffer);
  }
  else
    cname = MakeString("<global>");

  return GetNameSpace(cname);
}

void DebugInfo::StartAnalyze()
{
  sInt i;

  for(i=0;i<m_Files.size();i++)
  {
    m_Files[i].codeSize = m_Files[i].dataSize = 0;
  }

  for(i=0;i<NameSps.size();i++)
  {
    NameSps[i].codeSize = NameSps[i].dataSize = 0;
  }
}

void DebugInfo::FinishAnalyze()
{
	sInt i;

	for(i=0;i<Symbols.size();i++)
	{
		if( Symbols[i].Class == DIC_CODE )
		{
			m_Files[Symbols[i].objFileNum].codeSize += Symbols[i].Size;
			NameSps[Symbols[i].NameSpNum].codeSize += Symbols[i].Size;
		}
		else if( Symbols[i].Class == DIC_DATA )
		{
			m_Files[Symbols[i].objFileNum].dataSize += Symbols[i].Size;
			NameSps[Symbols[i].NameSpNum].dataSize += Symbols[i].Size;
		}
	}
}

sBool DebugInfo::FindSymbol(sU32 VA,DISymbol **sym)
{
  sInt l,r,x;

  l = 0;
  r = Symbols.size();
  while(l<r)
  {
    x = (l + r) / 2;

    if(VA < Symbols[x].VA)
      r = x; // continue in left half
    else if(VA >= Symbols[x].VA + Symbols[x].Size)
      l = x + 1; // continue in left half
    else
    {
      *sym = &Symbols[x]; // we found a match
      return true;
    }
  }

  *sym = (l + 1 < Symbols.size()) ? &Symbols[l+1] : 0;
  return false;
}

static bool symSizeComp(const DISymbol &a,const DISymbol &b)
{
  return a.Size > b.Size;
}

static bool templateSizeComp(const TemplateSymbol& a, const TemplateSymbol& b)
{
	return a.size > b.size;
}

static bool nameCodeSizeComp( const DISymNameSp &a,const DISymNameSp &b )
{
	return a.codeSize > b.codeSize;
}

static bool fileCodeSizeComp(const DISymFile &a,const DISymFile &b)
{
	return a.codeSize > b.codeSize;
}

static void sAppendPrintF(std::string &str,const char *format,...)
{
	static const int bufferSize = 512; // cut off after this
	char buffer[bufferSize];
	va_list arg;

	va_start(arg,format);
	_vsnprintf(buffer,bufferSize-1,format,arg);
	va_end(arg);

	strcpy(&buffer[bufferSize-4],"...");
	str += buffer;
}

const char * GetUndecorate(const char *str)
{
	static std::string temp;
	temp = str;
	if ( *str == '?' )
	{
		char scratch[1024];
		UnDecorateSymbolName( str, scratch, 1024, 0);
		temp = scratch;
	}
	return temp.c_str();
}

std::string DebugInfo::WriteReport()
{

	NVSHARE::HtmlTableInterface *iface = NVSHARE::getHtmlTableInterface();
	mDocument = iface->createHtmlDocument("Executable Size Report");

	NVSHARE::HtmlTable *groupTable = mDocument->createHtmlTable("Code Size By Group");
	groupTable->addHeader("Group/Name,Function/Count,Code/Size");
	groupTable->computeTotals();
	groupTable->addSort("Sorted by code size",3,false,2,false);

	NVSHARE::HtmlTable *objectTable = mDocument->createHtmlTable("Code Size By Object File");
	objectTable->addHeader("Object/Name,Function/Count,Code/Size");
	objectTable->computeTotals();
	objectTable->addSort("Sorted by code size",3,false,2,false);

	NVSHARE::HtmlTable *dataTable = mDocument->createHtmlTable("Data Size");
	dataTable->addHeader("Data/Name,Data/Size,Object/File");
	dataTable->computeTotals();
	dataTable->addSort("Sorted by data size",2,false,1,true);

  const int kMinSymbolSize = 512;
  const int kMinTemplateSize = 512;
  const int kMinDataSize = 1024;
  const int kMinClassSize = 2048;
  const int kMinFileSize = 2048;

	std::string Report;
  sInt i; //,j;
  sU32 size;

	Report.reserve(16384); // start out with 16k space

  // symbols
  sAppendPrintF(Report,"Functions by size\n");
	std::sort(Symbols.begin(),Symbols.end(),symSizeComp);

  for(i=0;i<Symbols.size();i++)
  {
	if( Symbols[i].Size < kMinSymbolSize )
		break;
    if(Symbols[i].Class == DIC_CODE)
    {
    	addFunctionReport(GetUndecorate(GetStringPrep(Symbols[i].name)), GetStringPrep(m_Files[Symbols[i].objFileNum].fileName),Symbols[i].Size );
      sAppendPrintF(Report,"%15s: %-50s %s\n",
	  NVSHARE::formatNumber(Symbols[i].Size),
        GetUndecorate(GetStringPrep(Symbols[i].name)), GetStringPrep(m_Files[Symbols[i].objFileNum].fileName));
    }
  }

  // templates
  sAppendPrintF(Report,"\nAggregated templates by size bytes:\n");

	std::sort(Templates.begin(),Templates.end(),templateSizeComp);

  for(i=0;i<Templates.size();i++)
  {
	  if( Templates[i].size < kMinTemplateSize )
		  break;
	  sAppendPrintF(Report,"%15s #%5d: %s\n",
		  NVSHARE::formatNumber(Templates[i].size),
		  Templates[i].count,
		  Templates[i].name.c_str() );
  }

  sAppendPrintF(Report,"\nData by size bytes:\n");
  for(i=0;i<Symbols.size();i++)
  {
    if( Symbols[i].Size < kMinDataSize )
      break;
    if(Symbols[i].Class == DIC_DATA)
    {

		dataTable->addColumn(GetUndecorate(GetStringPrep(Symbols[i].name)));
		dataTable->addColumn(Symbols[i].Size);
		dataTable->addColumn(GetStringPrep(m_Files[Symbols[i].objFileNum].fileName));
		dataTable->nextRow();
      sAppendPrintF(Report,"%15s: %-50s %s\n",
		  NVSHARE::formatNumber(Symbols[i].Size),
        GetStringPrep(Symbols[i].name), GetStringPrep(m_Files[Symbols[i].objFileNum].fileName));
    }
  }



	sAppendPrintF(Report,"\nBSS by size bytes:\n");
  for(i=0;i<Symbols.size();i++)
  {
    if( Symbols[i].Size < kMinDataSize )
      break;
    if(Symbols[i].Class == DIC_BSS)
    {
      sAppendPrintF(Report,"%15s: %-50s %s\n",
		  NVSHARE::formatNumber(Symbols[i].Size),
        GetStringPrep(Symbols[i].name), GetStringPrep(m_Files[Symbols[i].objFileNum].fileName));
    }
  }

  /*
  sSPrintF(Report,512,"\nFunctions by object file and size:\n");
  Report += sGetStringLen(Report);

  for(i=1;i<Symbols.size();i++)
    for(j=i;j>0;j--)
    {
      sInt f1 = Symbols[j].FileNum;
      sInt f2 = Symbols[j-1].FileNum;

      if(f1 == -1 || f2 != -1 && sCmpStringI(Files[f1].Name.String,Files[f2].Name.String) < 0)
        sSwap(Symbols[j],Symbols[j-1]);
    }

  for(i=0;i<Symbols.size();i++)
  {
    if(Symbols[i].Class == DIC_CODE)
    {
      sSPrintF(Report,512,"%5d.%02d: %-50s %s\n",
        Symbols[i].Size/1024,(Symbols[i].Size%1024)*100/1024,
        Symbols[i].Name,Files[Symbols[i].FileNum].Name);

      Report += sGetStringLen(Report);
    }
  }
  */

  sAppendPrintF(Report,"\nClasses/Namespaces by code size bytes:\n");
	std::sort(NameSps.begin(),NameSps.end(),nameCodeSizeComp);

  for(i=0;i<NameSps.size();i++)
  {
	  if( NameSps[i].codeSize < kMinClassSize )
		  break;
    sAppendPrintF(Report,"%15s: %s\n",
		NVSHARE::formatNumber(NameSps[i].codeSize), GetStringPrep(NameSps[i].name) );
  }

  sAppendPrintF(Report,"\nObject files by code size bytes:\n");
	std::sort(m_Files.begin(),m_Files.end(),fileCodeSizeComp);

  for(i=0;i<m_Files.size();i++)
  {
	  if( m_Files[i].codeSize < kMinFileSize )
		  break;
	  sAppendPrintF(Report,"%15s: %s\n",NVSHARE::formatNumber(m_Files[i].codeSize),
      GetStringPrep(m_Files[i].fileName) );
  }

	size = CountSizeInClass(DIC_CODE);
	sAppendPrintF(Report,"\nOverall code: %15s \n",NVSHARE::formatNumber(size));

	size = CountSizeInClass(DIC_DATA);
	sAppendPrintF(Report,"Overall data: %15s\n",NVSHARE::formatNumber(size));

	size = CountSizeInClass(DIC_BSS);
	sAppendPrintF(Report,"Overall BSS:  %15s\n",NVSHARE::formatNumber(size));

	for (FunctionReportMap::iterator i=mFunctions.begin(); i!=mFunctions.end(); ++i)
	{
		FunctionReport *fr = (*i).second;
		const char *typeName = (*i).first.c_str();
		groupTable->addColumn(typeName);
		groupTable->addColumn(fr->mTotalFunctionCount);
		groupTable->addColumn(fr->mTotalFunctionSize);
		groupTable->nextRow();
	}

	for (ObjectReportMap::iterator i=mObjects.begin(); i!=mObjects.end(); ++i)
	{
		(*i).second->finalReport(objectTable);
	}

	size_t len = 0;
	const char *doc = mDocument->saveDocument(len,NVSHARE::HST_SIMPLE_HTML);
	printf("Saving 'exesizer.html'\r\n");
	FILE *fph = fopen("exesizer.html", "wb");
	if ( fph )
	{
		fwrite(doc, len, 1, fph);
		fclose(fph);
	}

	mDocument->releaseDocumentMemory(doc);
	iface->releaseHtmlDocument(mDocument);


  return Report;
}


void DebugInfo::addFunctionReport(const char *function,const char *objectFile,size_t functionSize)
{

	char scratch[512];
	strcpy(scratch,objectFile);

	const char *lastSlash = NULL;
	const char *scan = objectFile;
	while ( *scan )
	{
		if ( *scan == '\\' )
			lastSlash = scan;
		scan++;
	}

	if ( lastSlash )
		objectFile = lastSlash+1;

	const char *prefix = scratch;

    //                    01 234567 890123456
	if ( strncmp(scratch,".\\build\\Xbox 360\\",17) == 0 )
	{
		prefix+=17;
	}
	char *sc = strstr(scratch,"\\release");
	if ( sc )
	{
		*sc = 0;
	}

#if ONLY_APEX
	char temp[512];
	strcpy(temp,prefix);
	strlwr(temp);
	const char *isApex = strstr(temp,"apex");
	if ( isApex == NULL ) return;
	if ( temp[0] == 'c' && temp[1] == ':' ) return;
#endif

	std::string byType = prefix;

	FunctionReportMap::iterator found = mFunctions.find( byType );
	FunctionReport *fr;

	if ( found == mFunctions.end() )
	{
		fr = new FunctionReport(byType,mDocument);
		mFunctions[byType] = fr;
	}
	else
	{
		fr = (*found).second;
	}
	fr->addFunction(function,objectFile,functionSize);



	{
		ObjectReportMap::iterator found = mObjects.find(byType);
		ObjectReport *or;
		if ( found == mObjects.end() )
		{
			or = new ObjectReport(byType,mDocument);
			mObjects[byType] = or;
		}
		else
		{
			or = (*found).second;
		}
		or->addFunction(function,objectFile,functionSize);
	}

}
