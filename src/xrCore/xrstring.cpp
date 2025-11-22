#include "stdafx.h"
#pragma hdrstop

#include "xrstring.h"
#include <sstream>

#include "FS_impl.h"

XRCORE_API extern str_container* g_pStringContainer = NULL;

struct str_container_impl
{
	static const u32 buffer_size = 1024 * 256;
	xr_unordered_set<str_value, str_value_hash> buffer;
	xrSRWLock rwlock;

	str_container_impl()
	{
		buffer.reserve(buffer_size);
	}

	str_value* find(str_value& value)
	{
		xrSRWLockGuard guard(&rwlock, true);
		auto it = buffer.find(value);
		if (it == buffer.end())
			return nullptr;

		return &(*it);
	}

	str_value* insert(str_value& value)
	{
		xrSRWLockGuard guard(&rwlock, false);
		auto p = buffer.insert(value);
		return &(*p.first);
	}

	void erase(str_value& value)
	{
		xrSRWLockGuard guard(&rwlock, false);
		buffer.erase(value);
	}

	void clean()
	{
		xrSRWLockGuard guard(&rwlock, false);
		buffer.clear();
		buffer.rehash(buffer_size);
	}

	void verify()
	{
		Msg("strings verify started");
		// do something
		Msg("strings verify completed");
	}

	void dump(FILE* f)
	{
		xrSRWLockGuard guard(&rwlock, true);
		for (const auto& s : buffer)
		{
			fprintf(f, "ref[%d]-len[%d] : %s\n", s.dwReference.load(), (u32)s.value.length(), s.value.c_str());
		}
	}

	void dump(IWriter* f)
	{
		xrSRWLockGuard guard(&rwlock, true);
		for (const auto& s : buffer)
		{
			string4096 temp;
			xr_sprintf(temp, sizeof(temp), "ref[%d]-len[%d] : %s\n", s.dwReference.load(), (u32)s.value.length(), s.value.c_str());
			f->w_string(temp);
		}
	}

	void dump_console()
	{
		xrSRWLockGuard guard(&rwlock, true);
		xr_set<str_value> set;
		for (const auto& s : buffer)
		{
			set.insert(s);
		}
		Msg("* [x-ray]: strings: count[%lu], unique[%lu]", buffer.size(), set.size());
		for (const auto& s : set)
		{
			Msg("ref[%d]-len[%d] : %s\n", s.dwReference.load(), (u32)s.value.length(), s.value.c_str());
		}
	}

	u32 stat_economy(u32& count, u32& unique)
	{
		xrSRWLockGuard guard(&rwlock, true);
		count = buffer.size();
		u32 size = sizeof(buffer);
		xr_unordered_set<xr_string> strings;
		for (const auto& s : buffer)
		{
			size += sizeof(str_value) + s.value.length();
			strings.insert(s.value);
		}
		unique = strings.size();
		return size;
	}
};

str_container::str_container()
{
	impl = xr_new<str_container_impl>();
}

str_value* str_container::dock(str_c value)
{
	if (!value) return nullptr;

	str_value s(value);
	return impl->insert(s);
}

void str_container::erase(str_c value)
{
	str_value s(value);
	impl->erase(s);
}

void str_container::clean()
{
	impl->clean();
}

void str_container::verify()
{
	impl->verify();
}

void str_container::dump()
{
	FILE* F = fopen("d:\\$str_dump$.txt", "w");
	impl->dump(F);
	fclose(F);
}

void str_container::dump(IWriter* W)
{
	impl->dump(W);
}

void str_container::dump_console()
{
	impl->dump_console();
}

u32 str_container::stat_economy(u32& count, u32& unique)
{
	return impl->stat_economy(count, unique);
}

str_container::~str_container()
{
	clean();
	xr_delete(impl);
}

//xr_string class
xr_vector<xr_string> xr_string::Split(char splitCh) const
{
	xr_vector<xr_string> Result;

	u32 SubStrBeginCursor = 0;
	u32 Len = 0;

	u32 StrCursor = 0;
	for (; StrCursor < size(); ++StrCursor)
	{
		if (at(StrCursor) == splitCh)
		{
			if ((StrCursor - SubStrBeginCursor) > 0)
			{
				Len = StrCursor - SubStrBeginCursor;
				Result.emplace_back(xr_string(&at(SubStrBeginCursor), Len));
				SubStrBeginCursor = StrCursor + 1;
			}
			else
			{
				Result.emplace_back("");
				SubStrBeginCursor = StrCursor + 1;
			}
		}
	}

	if (StrCursor > SubStrBeginCursor)
	{
		Len = StrCursor - SubStrBeginCursor;
		Result.emplace_back(xr_string(&at(SubStrBeginCursor), Len));
	}
	return Result;
}

xr_string::xr_string(LPCSTR Str, u32 Size)
	: Super(Str, Size)
{
}

xr_string::xr_string(Super&& other)
	: Super(other)
{
}

xr_string::xr_string(LPCSTR Str)
	: Super(Str)
{
}

xr_string& xr_string::operator=(LPCSTR Str)
{
	Super::operator=(Str);
	return *this;
}

xr_string& xr_string::operator=(const Super& other)
{
	Super::operator=(other);
	return *this;
}

xr_vector<xr_string> xr_string::Split(u32 NumberOfSplits, ...) const
{
	xr_vector<xr_string> intermediateTokens;
	xr_vector<xr_string> Result;

	va_list args;
	va_start(args, NumberOfSplits);

	for (u32 i = 0; i < NumberOfSplits; ++i)
	{
		char splitCh = va_arg(args, char);

		//special case for first try
		if (i == 0)
		{
			Result = Split(splitCh);
		}

		for (xr_string& str : Result)
		{
			xr_vector<xr_string> TokenStrResult = str.Split(splitCh);
			intermediateTokens.insert(intermediateTokens.end(), TokenStrResult.begin(), TokenStrResult.end());
		}

		if (!intermediateTokens.empty())
		{
			Result.clear();
			Result.insert(Result.begin(), intermediateTokens.begin(), intermediateTokens.end());
			intermediateTokens.clear();
		}
	}

	va_end(args);

	return Result;
}


xr_string xr_string::RemoveWhitespaces() const
{
	size_t Size = size();
	if (Size == 0) return xr_string();

	xr_string Result;
	Result.reserve(Size);

	const char* OrigStr = data();

	for (size_t i = 0; i < Size; ++i)
	{
		if (*OrigStr != ' ')
		{
			Result.push_back(OrigStr[i]);
		}
	}

	return Result;
}

bool xr_string::StartWith(const xr_string& Other) const
{
	return StartWith(Other.data(), Other.size());
}


bool xr_string::StartWith(LPCSTR Str) const
{
	u32 StrLen = xr_strlen(Str);
	return StartWith(Str, (int)StrLen);
}

bool xr_string::StartWith(LPCSTR Str, size_t Size) const
{
	size_t OurSize = size();

	//String is greater then our, we can't success
	if (OurSize < Size) return false;

	const char* OurStr = data();

	for (int i = 0; i < Size; ++i)
	{
		if (OurStr[i] != Str[i])
		{
			return false;
		}
	}

	return true;
}

bool xr_string::Contains(const xr_string& SubStr) const
{
	return find(SubStr) != npos;
}

xr_string xr_string::ToString(int Value)
{
	string64 buf = { 0 };
	itoa(Value, &buf[0], 10);

	return xr_string(buf);
}

xr_string xr_string::ToString(unsigned int Value)
{
	string64 buf = { 0 };
	sprintf(buf, "%u", Value);

	return xr_string(buf);
}

xr_string xr_string::ToString(float Value)
{
	string64 buf = { 0 };
	sprintf(buf, "%f", Value);

	return xr_string(buf);
}

xr_string xr_string::ToString(double Value)
{
	string64 buf = { 0 };
	sprintf(buf, "%f", Value);

	return xr_string(buf);
}

xr_string xr_string::Join(xrStringVector::iterator beginIter, xrStringVector::iterator endIter, const char delimeter /*= '\0'*/)
{
	xr_string Result;
	xrStringVector::iterator cursorIter = beginIter;

	while (cursorIter != endIter)
	{
		Result.append(*cursorIter);
		if (delimeter != '\0')
		{
			Result.push_back(delimeter);
		}
		cursorIter++;
	}

	if (delimeter != '\0')
	{
		Result.erase(Result.end() - 1);
	}

	return Result;
}

// String utils
SStringVec xr_string::SplitStringMulti(xr_string separator, bool includeSeparators, bool trimStrings) const {
	std::stringstream stringStream(std::string(this->c_str()));
	xr_string line;
	SStringVec wordVector;

	while (std::getline(stringStream, line))
	{
		std::size_t prev = 0, pos;
		while ((pos = line.find_first_of(separator, prev)) != xr_string::npos)
		{
			if (pos > prev)
				wordVector.push_back(line.substr(prev, pos - prev));

			if (includeSeparators)
				wordVector.push_back(line.substr(pos, 1));

			prev = pos + 1;
		}
		if (prev < line.length())
			wordVector.push_back(line.substr(prev, xr_string::npos));
	}
	if (trimStrings) {
		for (auto& s : wordVector) {
			s = s.Trim();
		}
	}
	return wordVector;
}

SStringVec xr_string::SplitStringLimit(xr_string separator, int limit, bool trimStrings) const
{
	std::stringstream stringStream(std::string(this->c_str()));
	xr_string line;
	SStringVec wordVector;

	while (std::getline(stringStream, line))
	{
		std::size_t prev = 0, pos;
		while ((pos = line.find_first_of(separator, prev)) != xr_string::npos)
		{
			if (pos > prev)
				wordVector.push_back(line.substr(prev, pos - prev));

			prev = pos + 1;
			if (limit > 0) {
				if (wordVector.size() >= limit) {
					wordVector.push_back(line.substr(prev, xr_string::npos));
					return wordVector;
				}
			}
		}
		if (prev < line.length())
			wordVector.push_back(line.substr(prev, xr_string::npos));
	}
	if (trimStrings) {
		for (auto& s : wordVector) {
			s = s.Trim();
		}
	}
	return wordVector;
}

xr_string xr_string::Trim(const char* t) const
{
	xr_string result = *this;
	result.erase(result.find_last_not_of(t) + 1);
	result.erase(0, result.find_first_not_of(t));
	return result;
};


xr_string xr_string::ToLowerCase() const
{
	xr_string result = *this;
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c)
	{
		return std::tolower(c);
	});
	return result;
}

xr_string xr_string::ReplaceAll(const xr_string& from, const xr_string& to) const
{
	xr_string result = *this;
	if (from.empty())
		return result;

	size_t start_pos = 0;
	while ((start_pos = result.find(from, start_pos)) != xr_string::npos)
	{
		result.replace(start_pos, from.length(), to);
		start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
	}

	return result;
}
