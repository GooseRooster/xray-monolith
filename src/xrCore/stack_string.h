#pragma once

/*
 * Stack-allocated string class with std::string-like API
 * Eliminates heap allocations for temporary strings
 *
 * Ported from IXRAY engine (Author: wh1t3lord)
 * Adapted for X-Ray Monolith
 *
 * Usage:
 *   stack_string<char, 256> path;
 *   path = "weapons\\";
 *   path += weapon_name;
 *   path += ".ltx";
 *   LoadConfig(path.c_str());
 */

#include <string.h>
#include <cassert>
#include <algorithm>

template<typename char_t, unsigned int _kStringLength>
class stack_string
{
public:
	using value_type = char_t;
	using pointer = char_t*;
	using reference = char_t&;
	using const_reference = const char_t&;
	using const_pointer = const pointer;
	using size_type = decltype(_kStringLength);

	using iterator = char_t*;
	using const_iterator = const char_t*;

	inline static constexpr size_type Length = _kStringLength;
	inline static constexpr size_type BufferSize = _kStringLength * sizeof(value_type);

	static_assert(_kStringLength > 0, "stack_string length must be > 0");

public:
	// Constructor - only null-terminates first char (faster than memset)
	stack_string()
	{
		m_buffer[0] = char_t(0);
	}

	// Construct from C string
	stack_string(const char_t* p_str)
	{
		m_buffer[0] = char_t(0);
		if (p_str)
			*this = p_str;
	}

	~stack_string() {}

	// Copy constructor
	stack_string(const stack_string& other)
	{
		memcpy(m_buffer, other.m_buffer, sizeof(m_buffer));
	}

	// Iterators
	inline const_iterator begin() const { return m_buffer; }
	inline const_iterator end() const { return m_buffer + size(); }
	inline iterator begin() { return m_buffer; }
	inline iterator end() { return m_buffer + size(); }

	inline const_iterator cbegin() const { return m_buffer; }
	inline const_iterator cend() const { return m_buffer + size(); }

	// Capacity
	inline const char_t* c_str() const { return &m_buffer[0]; }
	inline pointer data() { return &m_buffer[0]; }
	inline constexpr size_type max_size() const { return _kStringLength - 1; }
	inline constexpr size_type capacity() const { return _kStringLength - 1; }
	inline bool empty() const { return m_buffer[0] == char_t(0); }

	inline size_type size() const
	{
		if constexpr (std::is_same<char, char_t>::value)
			return static_cast<size_type>(strlen(m_buffer));
		else if constexpr (std::is_same<wchar_t, char_t>::value)
			return static_cast<size_type>(wcslen(m_buffer));
	}

	inline size_type length() const { return size(); }

	// Element access
	inline value_type at(size_type index) const
	{
		VERIFY2(index < _kStringLength, "stack_string: index out of bounds");
		return m_buffer[index];
	}

	inline reference at(size_type index)
	{
		VERIFY2(index < _kStringLength, "stack_string: index out of bounds");
		return m_buffer[index];
	}

	inline reference operator[](size_t pos) { return m_buffer[pos]; }
	inline const_reference operator[](size_t pos) const { return m_buffer[pos]; }

	inline reference front() { return m_buffer[0]; }
	inline const_reference front() const { return m_buffer[0]; }
	inline reference back() { return m_buffer[size() - 1]; }
	inline const_reference back() const { return m_buffer[size() - 1]; }

	// Modifiers
	inline void clear() { m_buffer[0] = char_t(0); }

	inline void push_back(char_t c)
	{
		const size_type len = length();
		if (len < _kStringLength - 1)
		{
			m_buffer[len] = c;
			m_buffer[len + 1] = char_t(0);
		}
	}

	inline void pop_back()
	{
		const size_type len = length();
		if (len > 0)
			m_buffer[len - 1] = char_t(0);
	}

	inline stack_string& append(const char_t* p_str)
	{
		if (!p_str) return *this;

		const size_type current_length = size();
		const size_type available_length = _kStringLength - current_length - 1;

		if (available_length > 0)
		{
			if constexpr (std::is_same_v<char_t, char>)
				strncat(m_buffer, p_str, available_length);
			else if constexpr (std::is_same_v<char_t, wchar_t>)
				wcsncat(m_buffer, p_str, available_length);
		}

		return *this;
	}

	inline stack_string& append(const char_t* str, size_type count)
	{
		const size_type current_len = length();
		const size_type available = _kStringLength - current_len - 1;
		count = (count > available) ? available : count;

		memcpy(m_buffer + current_len, str, count * sizeof(char_t));
		m_buffer[current_len + count] = char_t(0);
		return *this;
	}

	// Operations
	inline stack_string substr(size_type pos = 0, size_type count = size_type(-1)) const
	{
		stack_string result;
		const size_type len = length();

		if (pos >= len)
			return result;

		count = (count == size_type(-1) || pos + count > len) ? len - pos : count;
		result.append(m_buffer + pos, count);

		return result;
	}

	inline size_type find(const char_t* p_str, size_type pos = 0) const
	{
		if (!p_str) return size_type(-1);

		if constexpr (std::is_same_v<char_t, char>)
		{
			const char* found = strstr(m_buffer + pos, p_str);
			return found ? static_cast<size_type>(found - m_buffer) : size_type(-1);
		}
		else if constexpr (std::is_same_v<char_t, wchar_t>)
		{
			const wchar_t* found = wcsstr(m_buffer + pos, p_str);
			return found ? static_cast<size_type>(found - m_buffer) : size_type(-1);
		}

		return size_type(-1);
	}

	inline size_type find(char_t c, size_type pos = 0) const
	{
		for (size_type i = pos; i < length(); ++i)
			if (m_buffer[i] == c)
				return i;
		return size_type(-1);
	}

	// Operators
	inline stack_string& operator=(const stack_string& other)
	{
		memcpy(m_buffer, other.m_buffer, sizeof(m_buffer));
		return *this;
	}

	inline stack_string& operator=(const char_t* p_str)
	{
		if (p_str)
		{
			if constexpr (std::is_same_v<char_t, char>)
			{
				size_type arg_len = std::min(static_cast<size_type>(strlen(p_str)), _kStringLength - 1);
				memcpy(m_buffer, p_str, arg_len);
				m_buffer[arg_len] = '\0';
			}
			else if constexpr (std::is_same_v<char_t, wchar_t>)
			{
				size_type arg_len = std::min(static_cast<size_type>(wcslen(p_str)), _kStringLength - 1);
				memcpy(m_buffer, p_str, arg_len * sizeof(wchar_t));
				m_buffer[arg_len] = L'\0';
			}
		}
		else
		{
			m_buffer[0] = char_t(0);
		}
		return *this;
	}

	inline stack_string& operator+=(char_t symbol)
	{
		push_back(symbol);
		return *this;
	}

	inline stack_string& operator+=(const char_t* p_str)
	{
		return append(p_str);
	}

	inline stack_string& operator+=(const stack_string& other)
	{
		return append(other.c_str());
	}

	// Comparison
	inline bool operator==(const stack_string& other) const
	{
		if constexpr (std::is_same_v<char_t, char>)
			return strcmp(m_buffer, other.m_buffer) == 0;
		else
			return wcscmp(m_buffer, other.m_buffer) == 0;
	}

	inline bool operator==(const char_t* p_str) const
	{
		if (!p_str) return empty();
		if constexpr (std::is_same_v<char_t, char>)
			return strcmp(m_buffer, p_str) == 0;
		else
			return wcscmp(m_buffer, p_str) == 0;
	}

	inline bool operator!=(const stack_string& other) const { return !(*this == other); }
	inline bool operator!=(const char_t* p_str) const { return !(*this == p_str); }

private:
	char_t m_buffer[_kStringLength];
};

// Convenience aliases
template<unsigned int N>
using xr_stack_string = stack_string<char, N>;

template<unsigned int N>
using xr_stack_wstring = stack_string<wchar_t, N>;

// Common sizes for typical use cases
using stack_string64 = stack_string<char, 64>;
using stack_string128 = stack_string<char, 128>;
using stack_string256 = stack_string<char, 256>;
using stack_string512 = stack_string<char, 512>;
using stack_string1024 = stack_string<char, 1024>;

// Concatenation operator
template<typename char_t, unsigned int N>
inline stack_string<char_t, N> operator+(const stack_string<char_t, N>& lhs, const char_t* rhs)
{
	stack_string<char_t, N> result(lhs);
	result += rhs;
	return result;
}

template<typename char_t, unsigned int N>
inline stack_string<char_t, N> operator+(const char_t* lhs, const stack_string<char_t, N>& rhs)
{
	stack_string<char_t, N> result(lhs);
	result += rhs;
	return result;
}
